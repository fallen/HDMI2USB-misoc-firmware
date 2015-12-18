# XXX remove this
MSCDIR=../../third_party/misoc
include $(MSCDIR)/software/common.mak

OBJECTS=isr.o processor.o hdmi_in0.o hdmi_in1.o pattern.o edid.o pll.o ci.o config.o encoder.o i2c.o main.o hdmi_out0.o hdmi_out1.o

all: firmware.bin

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	chmod -x $@
	cp $@ boot.bin

firmware.elf: $(OBJECTS) libs

%.elf:
	$(LD) $(LDFLAGS) \
		-T linker.ld \
		-N -o $@ \
		$(MSCDIR)/software/libbase/crt0-$(CPU).o \
		$(OBJECTS) \
		-L$(MSCDIR)/software/libbase \
		-L$(MSCDIR)/software/libcompiler-rt \
		-lbase-nofloat -lcompiler-rt
	chmod -x $@

main.o: main.c
	$(compile-dep)

%.o: %.c
	$(compile-dep)

%.o: %.S
	$(assemble)

define gen0
@echo " GEN " $@
@sed -e "s/CSR_HDMI_OUTX_I2C_W_ADDR/CSR_HDMI_OUT0_I2C_W_ADDR/g;s/hdmi_outX/hdmi_out0/g" $< > $@
endef

define gen1
@echo " GEN " $@
@sed -e "s/CSR_HDMI_OUTX_I2C_W_ADDR/CSR_HDMI_OUT1_I2C_W_ADDR/g;s/hdmi_outX/hdmi_out1/g" $< > $@
endef

hdmi_out0.c: hdmi_outX.c
	$(gen0)

hdmi_out0.h: hdmi_outX.h
	$(gen0)

hdmi_out1.c: hdmi_outX.c
	$(gen1)

hdmi_out1.h: hdmi_outX.h
	$(gen1)

ci.o hdmi_out0.o hdmi_out1.o: hdmi_out0.h hdmi_out1.h

libs:
	$(MAKE) -C $(MSCDIR)/software/libcompiler-rt
	$(MAKE) -C $(MSCDIR)/software/libbase

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) firmware.elf firmware.bin
	$(RM) .*~ *~

.PHONY: all main.o clean libs load
