/* Protocol behaviour tests.
 *
 * channels.c and proto.c run on bare metal, so they are compiled a second
 * time as host code with the two I/O registers they touch replaced by
 * plain variables. Everything else -- the logic, the bounds checks, the
 * polarity maths, the PWM mapping -- is the same source.
 *
 * This is not a mock: the code under test is the code that flashes.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---- ATtiny85 register stubs ------------------------------------------- */

uint8_t PORTB;
uint8_t DDRB;
uint8_t PINB;
uint8_t TCCR0A;
uint8_t TCCR0B;
uint8_t OCR0A;
uint8_t OCR0B;

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))

/* config.h normally pulls in <avr/io.h> for F_CPU. */
#undef F_CPU
#define F_CPU 16500000UL

#include "hoststubs/usbstub.c"

#include "channels.c"
#include "proto.c"
#include "reply.c"

/* ---- test harness ------------------------------------------------------ */

static int failures;
static int checks;

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) == 0) return;
    failures++;
    printf("  FAIL %-34s got  '%s'\n", what, got);
    printf("       %-34s want '%s'\n", "", want);
}

static void expect_eq_int(const char *what, long got, long want)
{
    checks++;
    if (got == want) return;
    failures++;
    printf("  FAIL %-34s got  %ld, want %ld\n", what, got, want);
}

static const char *last_reply(void)
{
    return buf; /* the reply buffer reply.c builds into */
}

/*
 * Run one command line through the real parser.
 *
 * reply.c's buffer is file-static, so the test reaches it by including the
 * .c file above and using buf directly -- reply_commit() NUL-terminates it.
 */
static void send(const char *line)
{
    const char *p;
    reply_begin();
    for (p = line; *p; p++) proto_feed((uint8_t)*p);
    proto_feed((uint8_t)'\n');
    if (proto_pending()) proto_dispatch();
}

int main(void)
{
    printf("protocol behaviour (host build of the real firmware sources)\n");

    /* ---- reset state ---- */
    channels_init();
    expect_eq_int("boot: P0 is an output", DDRB & 0x01, 1);
    expect_eq_int("boot: P0 is LOW (active-high OFF)", PORTB & 0x01, 0);
    expect_eq_int("boot: P2 is an output", (DDRB & 0x04) != 0, 1);
    expect_eq_int("boot: P2 is LOW", PORTB & 0x04, 0);

    /* ---- the basic pair ---- */
    send("ON 0");
    expect_str("ON 0", last_reply(), "OK P0 ON HIGH 255");
    expect_eq_int("ON 0 drives P0 high", PORTB & 0x01, 1);

    send("OFF 0");
    expect_str("OFF 0", last_reply(), "OK P0 OFF LOW 0");
    expect_eq_int("OFF 0 drives P0 low", PORTB & 0x01, 0);

    /* ---- idempotence, the whole point of set-state ---- */
    send("ON 0");
    send("ON 0");
    send("ON 0");
    expect_str("ON 0 three times", last_reply(), "OK P0 ON HIGH 255");

    /* ---- case insensitivity ---- */
    send("on 0");
    expect_str("lowercase accepted", last_reply(), "OK P0 ON HIGH 255");
    send("oN 0");
    expect_str("mixed case accepted", last_reply(), "OK P0 ON HIGH 255");

    /* ---- PWM ---- */
    send("ON 0 120");
    expect_str("ON 0 120", last_reply(), "OK P0 ON PWM 120");
    expect_eq_int("duty 120 maps to OCR0A 240", OCR0A, 240);
    expect_eq_int("duty 120 uses non-inverting COM", TCCR0A >> 6 & 3, 2);

    send("ON 0 200");
    /* 200 > 127 so the inverted branch: OCR = (255-200)*2 = 110 */
    expect_eq_int("duty 200 maps to OCR0A 110", OCR0A, 110);
    expect_eq_int("duty 200 uses inverting COM", TCCR0A >> 6 & 3, 3);

    send("ON 0 255");
    expect_str("ON 0 255", last_reply(), "OK P0 ON HIGH 255");
    expect_eq_int("duty 255 is a static level", TCCR0A >> 6 & 3, 0);

    send("ON 0 0");
    expect_str("ON 0 0 is a usage error", last_reply(), "ERR DUTY MUST BE 1-255");

    /* ---- P2 has no compare output ---- */
    send("ON 2 100");
    expect_str("ON 2 100", last_reply(), "ERR P2 NO PWM");
    send("ON 2");
    expect_str("ON 2 full on is fine", last_reply(), "OK P2 ON HIGH 255");

    /* ---- the reserved pins, with reasons ---- */
    send("ON 3");
    expect_str("P3", last_reply(), "ERR P3 USB D-");
    send("ON 4");
    expect_str("P4", last_reply(), "ERR P4 USB D+");
    send("ON 5");
    expect_str("P5", last_reply(), "ERR P5 RESET");
    send("ON 9");
    expect_str("P9", last_reply(), "ERR P9 NO SUCH PIN");

    /* ---- HIGH/LOW are voltages, ON/OFF are decisions ---- */
    channels_init();
    send("HIGH 0");
    expect_str("HIGH 0 on an active-high channel", last_reply(), "OK P0 ON HIGH 255");
    send("LOW 0");
    expect_str("LOW 0 on an active-high channel", last_reply(), "OK P0 OFF LOW 0");

    /* ---- input mode ---- */
    send("IN 1");
    expect_str("IN 1", last_reply(), "OK P1 IN - 0");
    expect_eq_int("IN 1 clears DDRB", DDRB & 0x02, 0);
    expect_eq_int("IN 1 sets the pull-up", PORTB & 0x02, 0x02);
    send("OUT 1");
    expect_str("OUT 1", last_reply(), "OK P1 OFF LOW 0");
    expect_eq_int("OUT 1 sets DDRB", DDRB & 0x02, 2);

    /* ---- ALL OFF ---- */
    send("ON 0");
    send("ON 1 64");
    send("ALL OFF");
    expect_str("ALL OFF", last_reply(), "OK ALL OFF");
    expect_eq_int("ALL OFF: P0 low", PORTB & 0x01, 0);
    expect_eq_int("ALL OFF: P1 low", PORTB & 0x01, 0);
    expect_eq_int("ALL OFF: OCR0A cleared", OCR0A, 0);

    /* ---- PINS, both forms ---- */
    send("ON 0");
    send("ON 1 10");
    send("PINS");
    expect_str("PINS", last_reply(),
              "P0 ON HIGH 255 | P1 ON PWM 10 | P2 OFF LOW 0");
    send("PINS --RAW");
    expect_str("PINS --RAW", last_reply(),
              "P0,ON,HIGH,255;P1,ON,PWM,10;P2,OFF,LOW,0");

    /* ---- usage errors ---- */
    send("ON");
    expect_str("ON with no pin", last_reply(), "ERR NEED A PIN");
    send("ON 0 1 2");
    expect_str("ON with too many args", last_reply(), "ERR USAGE: ON <pin> [duty]");
    send("FROBNICATE");
    expect_str("unknown verb", last_reply(), "ERR UNKNOWN \"FROBNICATE\"");
    send("ON 0 x");
    expect_str("non-numeric duty", last_reply(), "ERR DUTY MUST BE 1-255");
    send("PINS --loud");
    expect_str("bad PINS flag", last_reply(), "ERR USAGE: PINS [--raw]");

    /* ---- CRLF and spacing tolerance ---- */
    {
        const char *p = "ON  0\r\n";
        reply_begin();
        for (p = "ON  0\r\n"; *p; p++) proto_feed((uint8_t)*p);
        if (proto_pending()) proto_dispatch();
        expect_str("CRLF and double spaces", buf, "OK P0 ON HIGH 255");
    }

    /* ---- an over-long line is reported, not silently truncated ---- */
    {
        char junk[LINE_MAX + 40];
        const char *p;
        int i;
        for (i = 0; i < (int)sizeof junk - 1; i++) junk[i] = (char)('a' + (i % 26));
        junk[sizeof junk - 1] = '\0';
        reply_begin();
        for (p = junk; *p; p++) proto_feed((uint8_t)*p);
        proto_feed((uint8_t)'\n');
        if (proto_pending()) proto_dispatch();
        expect_str("over-long line", buf, "ERR LINE TOO LONG");

        /* The stream must recover: the next command is a normal one. */
        send("ON 0");
        expect_str("recovers after an over-long line", buf, "OK P0 ON HIGH 255");
    }

    /*
     * ---- a whitespace-only line still gets an answer ----
     *
     * Silence here is worse than it looks: the host waits for one reply per
     * command, so swallowing a blank line is indistinguishable from a
     * device that has hung.
     */
    {
        const char *p;
        reply_begin();
        for (p = "   \n"; *p; p++) proto_feed((uint8_t)*p);
        expect_eq_int("whitespace line is still a line", proto_pending(), 1);
        proto_dispatch();
        expect_str("whitespace alone", buf, "ERR UNKNOWN \"\"");
    }

    /* ---- a bare newline is genuinely nothing, and says nothing ---- */
    {
        reply_begin();
        proto_feed((uint8_t)'\n');
        expect_eq_int("bare newline is not a command", proto_pending(), 0);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
