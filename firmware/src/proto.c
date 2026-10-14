#include "proto.h"
#include "reply.h"
#include "channels.h"
#include "board.h"

#include <avr/pgmspace.h>
#include <string.h>

#define LINE_MAX    48
#define MAX_TOKENS  4

static char line[LINE_MAX];
static uint8_t line_len;
static uint8_t overflow; /* current line overran; discard its contents */
static uint8_t ready;

static char *tok[MAX_TOKENS];
static uint8_t ntok;

/* ------------------------------------------------------------- input ----- */

uint8_t proto_feed(uint8_t b)
{
    if (b == '\r') return 0; /* tolerate CRLF without a second code path */

    if (b != '\n') {
        if (line_len < (LINE_MAX - 1)) line[line_len++] = (char)b;
        else overflow = 1;
        return 0;
    }

    line[line_len] = '\0';

    /*
     * Note that `overflow` is left set for proto_dispatch() to consume. If
     * we cleared it here, dispatch would go on to parse a line whose tail
     * was thrown away and answer ERR UNKNOWN with a truncated token, which
     * is exactly the silent-corruption failure this is meant to avoid.
     */
    if (overflow) {
        ready = 1;
        return 1;
    }

    if (line_len == 0) return 0; /* a bare newline is not a command */

    ready = 1;
    return 1;
}

uint8_t proto_pending(void)
{
    return ready;
}

/* ------------------------------------------------------------ helpers ---- */

static uint8_t is_space(char c)
{
    return (uint8_t)((c == ' ') || (c == '\t'));
}

/* Tokens were upper-cased on the way in, so this is a plain compare. */
static uint8_t eq(const char *a, const char *b)
{
    return (uint8_t)(strcmp(a, b) == 0);
}

static uint8_t parse_u(const char *s, uint16_t *out)
{
    uint16_t v = 0;

    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        v = (uint16_t)(v * 10u + (uint16_t)(*s - '0'));
        if (v > 999) return 0;
        s++;
    }
    *out = v;
    return 1;
}

/* Splits in place. Returns 0 if there were more tokens than we have room
 * for, in which case the caller should complain rather than guess. */
static uint8_t split(char *s)
{
    ntok = 0;
    for (;;) {
        while (is_space(*s)) s++;
        if (!*s) break;
        if (ntok == MAX_TOKENS) return 0;
        tok[ntok++] = s;
        while (*s && !is_space(*s)) s++;
        if (*s) { *s = '\0'; s++; }
    }
    return 1;
}

/* Turns a pin number into a channel index, or explains why not. The reason
 * matters: "ERR P4 USB D+" says the pin exists and why it is unavailable,
 * which is a completely different problem from a typo. */
static uint8_t resolve_pin(const char *s)
{
    uint16_t v;
    const char *why;

    if (!parse_u(s, &v)) {
        reply_begin();
        reply_lit("ERR PIN ");
        reply_str(s); /* echo the offending text verbatim */
        reply_commit();
        return 0xFF;
    }

    if (v < CH_COUNT) return (uint8_t)v;

    why = (v <= PIN_RESET) ? channel_reject_reason((uint8_t)v) : PSTR("NO SUCH PIN");
    reply_begin();
    reply_lit("ERR P");
    reply_u(v);
    reply_ch(' ');
    reply_strp(why);
    reply_commit();
    return 0xFF;
}

static void append_level(uint8_t lvl)
{
    if (lvl == 0) reply_lit("LOW");
    else if (lvl == 1) reply_lit("HIGH");
    else reply_lit("PWM");
}

/* "OK P0 ON HIGH 255" -- state, level and duty, so a script never has to
 * guess what the device actually did. */
static void append_state(uint8_t i)
{
    channel_t *c = channel_get(i);

    reply_lit("OK P");
    reply_u(i);
    reply_ch(' ');

    if (c->mode == CH_IN) {
        reply_lit("IN - 0");
        return;
    }

    reply_either(c->duty, "ON", "OFF");
    reply_ch(' ');
    append_level(channel_static_level(c));
    reply_ch(' ');
    reply_u(c->duty);
}

/* PINS, human and machine forms. */
static void append_channel(uint8_t i, uint8_t raw)
{
    channel_t *c = channel_get(i);

    if (i) reply_either(raw, ";", " | ");

    reply_lit("P");
    reply_u(i);
    reply_ch(raw ? ',' : ' ');

    if (c->mode == CH_IN) {
        reply_lit("IN - 0");
        return;
    }

    reply_either(c->duty, "ON", "OFF");
    reply_ch(raw ? ',' : ' ');
    append_level(channel_static_level(c));
    reply_ch(raw ? ',' : ' ');
    reply_u(c->duty);
}

static void need_pin(void)
{
    err("ERR NEED A PIN", "");
}

/* ----------------------------------------------------------- commands ---- */

static void cmd_on(void)
{
    uint8_t i;
    uint16_t duty = 255;

    if (ntok < 2) { need_pin(); return; }

    if (ntok == 2) {
        /* bare ON -> duty 255 */
    } else if (ntok == 3) {
        if (!parse_u(tok[2], &duty) || duty < 1 || duty > 255) {
            err("ERR DUTY MUST BE 1-255", "");
            return;
        }
    } else {
        err("ERR USAGE: ON <pin> [duty]", "");
        return;
    }

    i = resolve_pin(tok[1]);
    if (i == 0xFF) return;

    if (duty < 255 && !channel_pwm_capable(i)) {
        reply_begin();
        reply_lit("ERR P");
        reply_u(i);
        reply_lit(" NO PWM");
        reply_commit();
        return;
    }

    channel_set_duty(i, (uint8_t)duty);
    reply_begin();
    append_state(i);
    reply_commit();
}

static void cmd_off(void)
{
    uint8_t i;

    if (ntok != 2) { err("ERR USAGE: OFF <pin>", ""); return; }
    i = resolve_pin(tok[1]);
    if (i == 0xFF) return;

    channel_set_duty(i, 0);
    reply_begin();
    append_state(i);
    reply_commit();
}

/* HIGH and LOW are voltages, not decisions. Which one means "energised"
 * depends on the module, so we pick the duty that puts the pin at that
 * voltage. Either way the result is a static level, never a running timer. */
static void cmd_level(uint8_t high)
{
    uint8_t i;
    channel_t *c;

    if (ntok != 2) {
        if (high) err("ERR USAGE: HIGH <pin>", "");
        else err("ERR USAGE: LOW <pin>", "");
        return;
    }
    i = resolve_pin(tok[1]);
    if (i == 0xFF) return;

    c = channel_get(i);
    channel_set_duty(i, (uint8_t)((high ? !c->active_low : c->active_low) ? 255 : 0));
    reply_begin();
    append_state(i);
    reply_commit();
}

static void cmd_mode(uint8_t input)
{
    uint8_t i;

    if (ntok != 2) {
        if (input) err("ERR USAGE: IN <pin>", "");
        else err("ERR USAGE: OUT <pin>", "");
        return;
    }
    i = resolve_pin(tok[1]);
    if (i == 0xFF) return;

    if (input) channel_set_input(i);
    else channel_set_output(i); /* always re-enters as OFF */

    reply_begin();
    append_state(i);
    reply_commit();
}

static void cmd_read(void)
{
    uint8_t i;

    if (ntok != 2) { err("ERR USAGE: READ <pin>", ""); return; }
    i = resolve_pin(tok[1]);
    if (i == 0xFF) return;

    reply_begin();
    reply_lit("OK P");
    reply_u(i);
    reply_ch(' ');
    append_level(channel_read_level(i));
    reply_commit();
}

static void cmd_pins(void)
{
    uint8_t raw = 0;
    uint8_t i;

    if (ntok == 2) {
        if (!eq(tok[1], "--RAW")) { err("ERR USAGE: PINS [--raw]", ""); return; }
        raw = 1;
    } else if (ntok != 1) {
        err("ERR USAGE: PINS [--raw]", "");
        return;
    }

    reply_begin();
    for (i = 0; i < CH_COUNT; i++) append_channel(i, raw);
    reply_commit();
}

static void cmd_all_off(void)
{
    /* Reached only as "ALL OFF", already validated as exactly two tokens in
     * dispatch. */
    channels_all_off();
    reply_begin();
    reply_lit("OK ALL OFF");
    reply_commit();
}

static void cmd_status(void)
{
    reply_begin();
    reply_lit("ATTINY85-SSWITCH 1.0");
    reply_commit();
}

static void cmd_help(void)
{
    reply_begin();
    reply_lit("ON[ <pin>[ <duty>]] OFF <pin> HIGH <pin> LOW <pin> IN <pin> "
              "OUT <pin> READ <pin> PINS[ --raw] ALL OFF STATUS");
    reply_commit();
}

/* ---------------------------------------------------------- dispatch ----- */

void proto_dispatch(void)
{
    uint8_t i;

    ready = 0;

    if (overflow) {
        overflow = 0;
        line_len = 0;
        err("ERR LINE TOO LONG", "");
        return;
    }

    for (i = 0; i < line_len; i++) {
        if (line[i] >= 'a' && line[i] <= 'z') line[i] = (char)(line[i] - 32);
    }

    if (!split(line)) {
        line_len = 0;
        err("ERR TOO MANY ARGUMENTS", "");
        return;
    }

    if (ntok == 0) {
        /* Whitespace only. Still answer: the host waits one line per
         * command, and silence is indistinguishable from a hung device. */
        line_len = 0;
        err("ERR UNKNOWN \"\"", "");
        return;
    }

    if (eq(tok[0], "ON")) cmd_on();
    else if (eq(tok[0], "OFF")) cmd_off();
    else if (eq(tok[0], "HIGH")) cmd_level(1);
    else if (eq(tok[0], "LOW")) cmd_level(0);
    else if (eq(tok[0], "IN")) cmd_mode(1);
    else if (eq(tok[0], "OUT")) cmd_mode(0);
    else if (eq(tok[0], "READ")) cmd_read();
    else if (eq(tok[0], "PINS")) cmd_pins();
    else if (ntok == 2 && eq(tok[0], "ALL") && eq(tok[1], "OFF")) cmd_all_off();
    else if (eq(tok[0], "STATUS")) cmd_status();
    else if (eq(tok[0], "HELP") || eq(tok[0], "?")) cmd_help();
    else {
        reply_begin();
        reply_lit("ERR UNKNOWN \"");
        reply_str(tok[0]); /* RAM: the token came off the wire */
        reply_ch('"');
        reply_commit();
    }

    line_len = 0;
}
