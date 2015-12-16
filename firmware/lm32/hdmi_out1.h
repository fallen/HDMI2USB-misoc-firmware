#include <generated/csr.h>
#ifdef CSR_HDMI_OUT1_I2C_W_ADDR

#define EDID_EEPROM_I2C_READ_ADDR  0xa1
#define EDID_EEPROM_I2C_WRITE_ADDR 0xa0

#define EDID_BLOCK_SIZE 128

#define EDID_BLOCK0_EXT_NUMBER_OFFSET 126
#define EDID_BLOCK_CHECKSUM_OFFSET 127

int hdmi_out1_i2c_init(void);
void hdmi_out1_print_edid(void);

#endif