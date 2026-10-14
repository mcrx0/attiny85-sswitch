/*
 * Host-build stand-in for <avr/pgmspace.h>.
 *
 * On AVR, PROGMEM means "flash, not SRAM" and reading needs pgm_read_byte.
 * In a host build flash and RAM are the same address space, so PSTR is the
 * identity and pgm_read_byte is a plain dereference. The source under test
 * is unchanged; only the storage distinction disappears.
 */
#ifndef SSWITCH_TEST_AVR_PGMSPACE_H
#define SSWITCH_TEST_AVR_PGMSPACE_H

#include <stdint.h>

#define PROGMEM
#define PSTR(s_) (s_)
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))

#endif /* SSWITCH_TEST_AVR_PGMSPACE_H */
