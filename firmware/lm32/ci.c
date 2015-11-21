#include <stdio.h>
#include <stdlib.h>
#include <console.h>
#include <string.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/sdram_phy.h>
#include <time.h>

#include "config.h"
#include "hdmi_in0.h"
#include "hdmi_in1.h"
#include "processor.h"
#include "pll.h"
#include "ci.h"
#include "encoder.h"

int status_enabled;

void debug_crc(void) {
    printf("hdmi out CRC error number: 0x%08x\n", hdmi_out0_driver_output_crc_error_number_read());
    printf("hdmi out CRC value: 0x%08X\n", hdmi_out0_driver_output_crc_value_read());
    printf("hdmi out previous CRC value: 0x%08X\n", hdmi_out0_driver_previous_output_crc_value_read());
}

void print_board_dna(void) {
	int i;
	printf("Board's DNA: ");
	for(i=0; i<CSR_DNA_ID_SIZE; i++) {
		printf("%02x", MMPTR(CSR_DNA_ID_ADDR+4*i));
	}
	printf("\n");
}

static void help_video_matrix(void)
{
	puts("video_matrix list              - list available video sinks and sources");
	puts("video_matrix connect <source>  - connect video source to video sink");
	puts("                     <sink>");
}

static void help_video_mode(void)
{
	puts("video_mode list                - list available video modes");
	puts("video_mode <mode>              - select video mode");
}

static void help_hdp_toggle(void)
{
	puts("hdp_toggle <source>            - toggle HDP on source for EDID rescan");
}

#ifdef CSR_HDMI_OUT0_BASE
static void help_output0(void)
{
	puts("output0 on                     - enable output0");
	puts("output0 off                    - disable output0");
}
#endif

#ifdef CSR_HDMI_OUT1_BASE
static void help_output1(void)
{
	puts("output1 on                     - enable output1");
	puts("output1 off                    - disable output1");
}
#endif

#ifdef ENCODER_BASE
static void help_encoder(void)
{
	puts("encoder on                     - enable encoder");
	puts("encoder off                    - disable encoder");
	puts("encoder quality <quality>      - select quality");
}
#endif

static void help_debug(void)
{
	puts("debug pll                      - dump pll configuration");
	puts("debug ddr                      - show DDR bandwidth");
	puts("debug dna                      - show Board's DNA");
}

static void help(void)
{
	puts("help                           - this command");
	puts("version                        - firmware/gateware version");
	puts("reboot                         - reboot CPU");
	puts("status <on/off>                - enable/disable status message (same with by pressing enter)");
	puts("");
	help_video_matrix();
	puts("");
	help_video_mode();
	puts("");
	help_hdp_toggle();
	puts("");
#ifdef CSR_HDMI_OUT0_BASE
	help_output0();
	puts("");
#endif
#ifdef CSR_HDMI_OUT1_BASE
	help_output1();
	puts("");
#endif
#ifdef ENCODER_BASE
	help_encoder();
	puts("");
#endif
	help_debug();
}

static void version(void)
{
	printf("gateware revision: %08x\n", identifier_revision_read());
	printf("firmware revision: %08x, built "__DATE__" "__TIME__"\n", MSC_GIT_ID);
}

static void reboot(void)
{
	asm("call r0");
}

static void status_enable(void)
{
	printf("Enabling status\n");
	status_enabled = 1;
#ifdef ENCODER_BASE
	encoder_bandwidth_nbytes_clear_write(1);
#endif
}

static void status_disable(void)
{
	printf("Disabling status\n");
	status_enabled = 0;
}

static void debug_ddr(void);

static void status_print(void)
{
#ifdef CSR_HDMI_IN0_BASE
	printf(
		"input0:  %dx%d",
		hdmi_in0_resdetection_hres_read(),
		hdmi_in0_resdetection_vres_read());
	printf("\n");
#endif

#ifdef CSR_HDMI_IN1_BASE
	printf(
		"input1:  %dx%d",
		hdmi_in1_resdetection_hres_read(),
		hdmi_in1_resdetection_vres_read());
	printf("\n");
#endif

#ifdef CSR_HDMI_OUT0_BASE
	printf("output0: ");
	if(hdmi_out0_fi_enable_read())
		printf(
			"%dx%d@%dHz from %s",
			processor_h_active,
			processor_v_active,
			processor_refresh,
			processor_get_source_name(processor_hdmi_out0_source));
	else
		printf("off");
	printf("\n");
#endif

#ifdef CSR_HDMI_OUT1_BASE
	printf("output1: ");
	if(hdmi_out1_fi_enable_read())
		printf(
			"%dx%d@%uHz from %s",
			processor_h_active,
			processor_v_active,
			processor_refresh,
			processor_get_source_name(processor_hdmi_out1_source));
	else
		printf("off");
	printf("\n");
#endif

#ifdef ENCODER_BASE
	printf("encoder: ");
	if(encoder_enabled) {
		printf(
			"%dx%d @ %dfps (%dMbps) from %s (q: %d)",
			processor_h_active,
			processor_v_active,
			encoder_fps,
			encoder_bandwidth_nbytes_read()*8/1000000,
			processor_get_source_name(processor_encoder_source),
			encoder_quality);
		encoder_bandwidth_nbytes_clear_write(1);
	} else
		printf("off");
	printf("\n");
#endif
	printf("ddr: ");
	debug_ddr();
}

static void status_service(void)
{
	static int last_event;

	if(elapsed(&last_event, identifier_frequency_read())) {
		if(status_enabled) {
			status_print();
			printf("\n");
		}
	}
}

static void video_matrix_list(void)
{
	printf("Video sources:\n");
#ifdef CSR_HDMI_IN0_BASE
	printf("input0: %s\n", HDMI_IN0_MNEMONIC);
	puts(HDMI_IN0_DESCRIPTION);
#endif
#ifdef CSR_HDMI_IN1_BASE
	printf("input1: %s\n", HDMI_IN1_MNEMONIC);
	puts(HDMI_IN1_DESCRIPTION);
#endif
	printf("pattern:\n");
	printf("  Video pattern\n");
	puts(" ");
	printf("Video sinks:\n");
#ifdef CSR_HDMI_OUT0_BASE
	printf("output0: %s\n", HDMI_OUT0_MNEMONIC);
	puts(HDMI_OUT0_DESCRIPTION);
#endif
#ifdef CSR_HDMI_OUT1_BASE
	printf("output1: %s\n", HDMI_OUT1_MNEMONIC);
	puts(HDMI_OUT1_DESCRIPTION);
#endif
#ifdef ENCODER_BASE
	printf("encoder:\n");
	printf("  JPEG Encoder\n");
#endif
	puts(" ");
}

static void video_matrix_connect(int source, int sink)
{
	if(source >= 0 && source <= VIDEO_IN_PATTERN)
	{
		if(sink >= 0 && sink <= VIDEO_OUT_HDMI_OUT1) {
			printf("Connecting %s to output%d\n", processor_get_source_name(source), sink);
			if(sink == VIDEO_OUT_HDMI_OUT0)
#ifdef CSR_HDMI_OUT0_BASE
				processor_set_hdmi_out0_source(source);
#else
				printf("hdmi_out0 is missing.\n");
#endif
			else if(sink == VIDEO_OUT_HDMI_OUT1)
#ifdef CSR_HDMI_OUT1_BASE
				processor_set_hdmi_out1_source(source);
#else
				printf("hdmi_out1 is missing.\n");
#endif
			processor_update();
		}
#ifdef ENCODER_BASE
		else if(sink == VIDEO_OUT_ENCODER) {
			printf("Connecting %s to encoder\n", processor_get_source_name(source));
			processor_set_encoder_source(source);
			processor_update();
		}
#endif
	}
}

static void video_mode_list(void)
{
	char mode_descriptors[PROCESSOR_MODE_COUNT*PROCESSOR_MODE_DESCLEN];
	int i;

	processor_list_modes(mode_descriptors);
	printf("Available video modes:\n");
	for(i=0;i<PROCESSOR_MODE_COUNT;i++)
		printf("mode %d: %s\n", i, &mode_descriptors[i*PROCESSOR_MODE_DESCLEN]);
	printf("\n");
}

static void video_mode_set(int mode)
{
	char mode_descriptors[PROCESSOR_MODE_COUNT*PROCESSOR_MODE_DESCLEN];
	if(mode < PROCESSOR_MODE_COUNT) {
		processor_list_modes(mode_descriptors);
		printf("Setting video mode to %s\n", &mode_descriptors[mode*PROCESSOR_MODE_DESCLEN]);
		config_set(CONFIG_KEY_RESOLUTION, mode);
		processor_start(mode);
	}
}

static void hdp_toggle(int source)
{
	int i;
	printf("Toggling HDP on output%d\n", source);
#ifdef CSR_HDMI_IN0_BASE
	if(source ==  VIDEO_IN_HDMI_IN0) {
		hdmi_in0_edid_hpd_en_write(0);
		for(i=0; i<65536; i++);
		hdmi_in0_edid_hpd_en_write(1);
	}
#else
	printf("hdmi_in0 is missing.\n");
#endif
#ifdef CSR_HDMI_IN1_BASE
	if(source == VIDEO_IN_HDMI_IN1) {
		hdmi_in1_edid_hpd_en_write(0);
		for(i=0; i<65536; i++);
		hdmi_in1_edid_hpd_en_write(1);
	}
#else
	printf("hdmi_in1 is missing.\n");
#endif
}

#ifdef CSR_HDMI_OUT0_BASE
static void output0_on(void)
{
	printf("Enabling output0\n");
	hdmi_out0_fi_enable_write(1);
}

static void output0_off(void)
{
	printf("Disabling output0\n");
	hdmi_out0_fi_enable_write(0);
}
#endif

#ifdef CSR_HDMI_OUT1_BASE
static void output1_on(void)
{
	printf("Enabling output1\n");
	hdmi_out1_fi_enable_write(1);
}

static void output1_off(void)
{
	printf("Disabling output1\n");
	hdmi_out1_fi_enable_write(0);
}
#endif

#ifdef ENCODER_BASE
static void encoder_on(void)
{
	printf("Enabling encoder\n");
	encoder_enable(1);
}

static void encoder_configure_quality(int quality)
{
	printf("Setting encoder quality to %d\n", quality);
	encoder_set_quality(quality);
}


static void encoder_off(void)
{
	printf("Disabling encoder\n");
	encoder_enable(0);
}
#endif

static void debug_pll(void)
{
	pll_dump();
}

static unsigned int log2(unsigned int v)
{
	unsigned int r = 0;
	while(v>>=1) r++;
	return r;
}

static void debug_ddr(void)
{
	unsigned long long int nr, nw;
	unsigned long long int f;
	unsigned int rdb, wrb;
	unsigned int burstbits;

	sdram_controller_bandwidth_update_write(1);
	nr = sdram_controller_bandwidth_nreads_read();
	nw = sdram_controller_bandwidth_nwrites_read();
	f = identifier_frequency_read();
	burstbits = (2*DFII_NPHASES) << DFII_PIX_DATA_SIZE;
	rdb = (nr*f >> (24 - log2(burstbits)))/1000000ULL;
	wrb = (nw*f >> (24 - log2(burstbits)))/1000000ULL;
	printf("read:%5dMbps  write:%5dMbps  all:%5dMbps\n", rdb, wrb, rdb + wrb);
}

static char *readstr(void)
{
	char c[2];
	static char s[64];
	static int ptr = 0;

	if(readchar_nonblock()) {
		c[0] = readchar();
		c[1] = 0;
		switch(c[0]) {
			case 0x7f:
			case 0x08:
				if(ptr > 0) {
					ptr--;
					putsnonl("\x08 \x08");
				}
				break;
			case 0x07:
				break;
			case '\r':
			case '\n':
				s[ptr] = 0x00;
				putsnonl("\n");
				ptr = 0;
				return s;
			default:
				if(ptr >= (sizeof(s) - 1))
					break;
				putsnonl(c);
				s[ptr] = c[0];
				ptr++;
				break;
		}
	}
	return NULL;
}

static char *get_token(char **str)
{
	char *c, *d;

	c = (char *)strchr(*str, ' ');
	if(c == NULL) {
		d = *str;
		*str = *str+strlen(*str);
		return d;
	}
	*c = 0;
	d = *str;
	*str = c+1;
	return d;
}


void ci_prompt(void)
{
	printf("HDMI2USB>");
}

void ci_service(void)
{
	char *str;
	char *token;

	status_service();

	str = readstr();
	if(str == NULL) return;

	token = get_token(&str);

	if(strcmp(token, "help") == 0) {
		puts("Available commands:");
		token = get_token(&str);
		if(strcmp(token, "video_matrix") == 0)
			help_video_matrix();
		else if(strcmp(token, "video_mode") == 0)
			help_video_mode();
		else if(strcmp(token, "hdp_toggle") == 0)
			help_hdp_toggle();
#ifdef CSR_HDMI_OUT0_BASE
		else if(strcmp(token, "output0") == 0)
			help_output0();
#endif
#ifdef CSR_HDMI_OUT1_BASE
		else if(strcmp(token, "output1") == 0)
			help_output1();
#endif
#ifdef ENCODER_BASE
		else if(strcmp(token, "encoder") == 0)
			help_encoder();
#endif
		else if(strcmp(token, "debug") == 0)
			help_debug();
		else
			help();
		puts("");
	}
	else if(strcmp(token, "reboot") == 0) reboot();
	else if(strcmp(token, "version") == 0) version();
	else if(strcmp(token, "video_matrix") == 0) {
		token = get_token(&str);
		if(strcmp(token, "list") == 0)
			video_matrix_list();
		else if(strcmp(token, "connect") == 0) {
			int source;
			int sink;
			/* get video source */
			token = get_token(&str);
			source = -1;
			if(strcmp(token, "input0") == 0)
				source = VIDEO_IN_HDMI_IN0;
			else if(strcmp(token, "input1") == 0)
				source = VIDEO_IN_HDMI_IN1;
			else if(strcmp(token, "pattern") == 0)
				source = VIDEO_IN_PATTERN;
			else
				printf("Unknown video source: '%s'\n", token);

			/* get video sink */
			token = get_token(&str);
			sink = -1;
			if(strcmp(token, "output0") == 0)
				sink = VIDEO_OUT_HDMI_OUT0;
			else if(strcmp(token, "output1") == 0)
				sink = VIDEO_OUT_HDMI_OUT1;
			else if(strcmp(token, "encoder") == 0)
				sink = VIDEO_OUT_ENCODER;
			else
				printf("Unknown video sink: '%s'\n", token);

			if (source >= 0 && sink >= 0)
				video_matrix_connect(source, sink);
			else
				help_video_matrix();
		} else {
			help_video_matrix();
		}
	}
	else if(strcmp(token, "video_mode") == 0) {
		token = get_token(&str);
		if(strcmp(token, "list") == 0)
			video_mode_list();
		else
			video_mode_set(atoi(token));
	}
	else if(strcmp(token, "hdp_toggle") == 0) {
		token = get_token(&str);
		hdp_toggle(atoi(token));
	}
#ifdef CSR_HDMI_OUT0_BASE
	else if(strcmp(token, "output0") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			output0_on();
		else if(strcmp(token, "off") == 0)
			output0_off();
		else
			help_output0();
	}
#endif
#ifdef CSR_HDMI_OUT1_BASE
	else if(strcmp(token, "output1") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			output1_on();
		else if(strcmp(token, "off") == 0)
			output1_off();
		else
			help_output1();
	}
#endif
#ifdef ENCODER_BASE
	else if(strcmp(token, "encoder") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			encoder_on();
		else if(strcmp(token, "off") == 0)
			encoder_off();
		else if(strcmp(token, "quality") == 0)
			encoder_configure_quality(atoi(get_token(&str)));
		else
			help_encoder();
	}
#endif
	else if(strcmp(token, "status") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			status_enable();
		else if(strcmp(token, "off") == 0)
			status_disable();
		else
			status_print();
	}
	else if(strcmp(token, "debug") == 0) {
		token = get_token(&str);
		if(strcmp(token, "pll") == 0)
			debug_pll();
		else if(strcmp(token, "ddr") == 0)
			debug_ddr();
		else if(strcmp(token, "dna") == 0)
			print_board_dna();
		else if(strcmp(token, "crc") == 0)
		    debug_crc();
		else if(strcmp(token, "corrupt") == 0)
		    corrupt_pattern();
		else
			help_debug();
	} else {
		if(status_enabled)
			status_disable();
	}

	ci_prompt();
}
