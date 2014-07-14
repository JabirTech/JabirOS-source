/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42) (by Poul-Henning Kamp):
 * <joerg@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.        Joerg Wunsch
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: release/10.0.0/share/examples/libusb20/aux.h 238603 2012-07-18 21:30:17Z joerg $
 */

#include <stdint.h>
#include <libusb20.h>

const char *usb_error(enum libusb20_error r);
void print_formatted(uint8_t *buf, uint32_t len);
