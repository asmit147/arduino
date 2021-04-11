/* vi:set tabstop=2: shiftwidth=2: softtabstop=2: expandtab:
 *
 * Pump control system common definitions, structs, functions.
 *
 * SDA and SCL pins as defined in arduino pinout diagram
 * +-----+-----+-----+------+
 * |     | LEO | UNO | NANO |
 * +-----+-----+-----+------+
 * | SDA | 2   | A4  | A4   |
 * +-----+-----+-----+------+
 * | SCL | 3   | A5  | A5   |
 * +-----+-----+-----+------+
*/

#define SERIAL_MON_BPS                  115200
#define RS485_BPS                       115200
#define MASTER_ID                       0x1
#define DISPLAY_NODE_ID                 0x2

struct mastermsgbuf {
  unsigned char node_address_to;
  unsigned char node_address_from;
  unsigned char inputs;
  unsigned char outputs;
  char line[4][20];
	unsigned char checksum;
} __attribute(( packed )); // 8*5 + 4*20*8 = 680 bits

struct slavemsgbuf {
  unsigned char node_address_to;
  unsigned char node_address_from;
  unsigned char inputs;
  unsigned char outputs;
	unsigned char checksum;
} __attribute(( packed )); // 8*5 = 32 bits

