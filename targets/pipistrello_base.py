from gateware.hdmi_out import HDMIOut

from fractions import Fraction
import struct

from migen.fhdl.std import *
from migen.genlib.resetsync import AsyncResetSynchronizer
from migen.bus import wishbone
from mibuild.generic_platform import Pins, IOStandard

from misoclib.mem.sdram.module import MT46H32M16
from misoclib.mem.sdram.phy import s6ddrphy
from misoclib.mem.sdram.core.lasmicon import LASMIconSettings
from misoclib.mem.flash import spiflash
from misoclib.soc.sdram import SDRAMSoC

from gateware import dna


class _CRG(Module):
    def __init__(self, platform, clk_freq):
        self.clock_domains.cd_sys = ClockDomain()
        self.clock_domains.cd_sdram_half = ClockDomain()
        self.clock_domains.cd_sdram_full_wr = ClockDomain()
        self.clock_domains.cd_sdram_full_rd = ClockDomain()
        self.clock_domains.cd_base50 = ClockDomain()
        self.clock_domains.cd_por = ClockDomain()

        self.clk4x_wr_strb = Signal()
        self.clk4x_rd_strb = Signal()

        f0 = Fraction(50, 1)*1000000
        p = 12
        f = Fraction(clk_freq*p, f0)
        n, d = f.numerator, f.denominator
        print("n/d : {}/{}".format(n ,d))
        assert 19e6 <= f0/d <= 500e6  # pfd
        assert 400e6 <= f0*n/d <= 1080e6  # vco

        clk50 = platform.request("clk50")
        clk50a = Signal()
        self.specials += Instance("IBUFG", i_I=clk50, o_O=clk50a)
        clk50b = Signal()
        self.specials += Instance("BUFIO2", p_DIVIDE=1,
                                  p_DIVIDE_BYPASS="TRUE", p_I_INVERT="FALSE",
                                  i_I=clk50a, o_DIVCLK=clk50b)
        pll_lckd = Signal()
        pll_fb = Signal()
        pll = Signal(6)
        self.specials.pll = Instance("PLL_ADV", p_SIM_DEVICE="SPARTAN6",
                                     p_BANDWIDTH="OPTIMIZED", p_COMPENSATION="INTERNAL",
                                     p_REF_JITTER=.01, p_CLK_FEEDBACK="CLKFBOUT",
                                     i_DADDR=0, i_DCLK=0, i_DEN=0, i_DI=0, i_DWE=0, i_RST=0, i_REL=0,
                                     p_DIVCLK_DIVIDE=d, p_CLKFBOUT_MULT=n, p_CLKFBOUT_PHASE=0.,
                                     i_CLKIN1=clk50b, i_CLKIN2=0, i_CLKINSEL=1,
                                     p_CLKIN1_PERIOD=1e9/f0, p_CLKIN2_PERIOD=0.,
                                     i_CLKFBIN=pll_fb, o_CLKFBOUT=pll_fb, o_LOCKED=pll_lckd,
                                     o_CLKOUT0=pll[0], p_CLKOUT0_DUTY_CYCLE=.5,
                                     o_CLKOUT1=pll[1], p_CLKOUT1_DUTY_CYCLE=.5,
                                     o_CLKOUT2=pll[2], p_CLKOUT2_DUTY_CYCLE=.5,
                                     o_CLKOUT3=pll[3], p_CLKOUT3_DUTY_CYCLE=.5,
                                     o_CLKOUT4=pll[4], p_CLKOUT4_DUTY_CYCLE=.5,
                                     o_CLKOUT5=pll[5], p_CLKOUT5_DUTY_CYCLE=.5,
                                     p_CLKOUT0_PHASE=0., p_CLKOUT0_DIVIDE=p//4,  # sdram wr rd
                                     p_CLKOUT1_PHASE=0., p_CLKOUT1_DIVIDE=p//4,
                                     p_CLKOUT2_PHASE=270., p_CLKOUT2_DIVIDE=p//2,  # sdram dqs adr ctrl
                                     p_CLKOUT3_PHASE=250., p_CLKOUT3_DIVIDE=p//2,  # off-chip ddr
                                     p_CLKOUT4_PHASE=0., p_CLKOUT4_DIVIDE=p//1,  # base50
                                     p_CLKOUT5_PHASE=0., p_CLKOUT5_DIVIDE=p//1,  # sys
        )
        self.specials += Instance("BUFG", i_I=pll[5], o_O=self.cd_sys.clk)
        reset = platform.request("user_btn")
        #self.comb += self.cd_base50.clk.eq(clk50a)
        self.specials += Instance("BUFG", i_I=pll[4], o_O=self.cd_base50.clk)
        por = Signal(max=1 << 11, reset=(1 << 11) - 1)
        self.specials += AsyncResetSynchronizer(self.cd_base50, por > 0)
        self.sync.por += If(por != 0, por.eq(por - 1))
        self.comb += self.cd_por.clk.eq(self.cd_sys.clk)
        self.specials += AsyncResetSynchronizer(self.cd_por, reset)
        self.specials += AsyncResetSynchronizer(self.cd_sys, ~pll_lckd | (por > 0))
        self.specials += Instance("BUFG", i_I=pll[2], o_O=self.cd_sdram_half.clk)
        self.specials += Instance("BUFPLL", p_DIVIDE=4,
                                  i_PLLIN=pll[0], i_GCLK=self.cd_sys.clk,
                                  i_LOCKED=pll_lckd, o_IOCLK=self.cd_sdram_full_wr.clk,
                                  o_SERDESSTROBE=self.clk4x_wr_strb)
        self.comb += [
            self.cd_sdram_full_rd.clk.eq(self.cd_sdram_full_wr.clk),
            self.clk4x_rd_strb.eq(self.clk4x_wr_strb),
        ]
        clk_sdram_half_shifted = Signal()
        self.specials += Instance("BUFG", i_I=pll[3], o_O=clk_sdram_half_shifted)
        clk = platform.request("ddram_clock")
        self.specials += Instance("ODDR2", p_DDR_ALIGNMENT="NONE",
                                  p_INIT=0, p_SRTYPE="SYNC",
                                  i_D0=1, i_D1=0, i_S=0, i_R=0, i_CE=1,
                                  i_C0=clk_sdram_half_shifted, i_C1=~clk_sdram_half_shifted,
                                  o_Q=clk.p)
        self.specials += Instance("ODDR2", p_DDR_ALIGNMENT="NONE",
                                  p_INIT=0, p_SRTYPE="SYNC",
                                  i_D0=0, i_D1=1, i_S=0, i_R=0, i_CE=1,
                                  i_C0=clk_sdram_half_shifted, i_C1=~clk_sdram_half_shifted,
                                  o_Q=clk.n)


def _get_firmware_data(firmware_filename):
    data = []
    try:
        with open(firmware_filename, "rb") as firmware_file:
            while True:
                w = firmware_file.read(4)
                if not w:
                    break
                data.append(struct.unpack(">I", w)[0])
    except:
        pass
    return data

papilio_adapter_io = [
    ("debug_ios", 0, Pins("C:0 C:1 C:2 C:3 C:4 C:5 C:6 C:7 C:8 C:9 C:10 C:11 C:12 C:13 C14"), IOStandard("LVTTL")),
]


class BaseSoC(SDRAMSoC):
    default_platform = "pipistrello"

    csr_map = {
        "spiflash": 16,
        "ddrphy": 17,
        "dna": 18,
    }
    csr_map.update(SDRAMSoC.csr_map)

    mem_map = {
        "firmware_ram": 0x20000000,  # (default shadow @0xa0000000)
    }
    mem_map.update(SDRAMSoC.mem_map)

    def __init__(self, platform, clk_freq=(83 + Fraction(1, 3))*1000*1000,
                 sdram_controller_settings=LASMIconSettings(l2_size=32,
                                                            with_bandwidth=True),
                 firmware_ram_size=0xa000, firmware_filename=None, **kwargs):
        SDRAMSoC.__init__(self, platform, clk_freq,
                          integrated_rom_size=0x8000,
                          sdram_controller_settings=sdram_controller_settings,
                          **kwargs)

        self.submodules.crg = _CRG(platform, clk_freq)
        self.submodules.dna = dna.DNA()
        self.submodules.firmware_ram = wishbone.SRAM(firmware_ram_size, init=_get_firmware_data(firmware_filename))
        self.register_mem("firmware_ram", self.mem_map["firmware_ram"], self.firmware_ram.bus, firmware_ram_size)
        self.add_constant("ROM_BOOT_ADDRESS", self.mem_map["firmware_ram"])

        if not self.integrated_main_ram_size:
            self.submodules.ddrphy = s6ddrphy.S6HalfRateDDRPHY(platform.request("ddram"),
                                                               MT46H32M16(self.clk_freq),
                                                               rd_bitslip=1,
                                                               wr_bitslip=3,
                                                               dqs_ddr_alignment="C1")
            self.comb += [
                self.ddrphy.clk4x_wr_strb.eq(self.crg.clk4x_wr_strb),
                self.ddrphy.clk4x_rd_strb.eq(self.crg.clk4x_rd_strb),
            ]
            self.register_sdram_phy(self.ddrphy)

        if not self.integrated_rom_size:
            self.submodules.spiflash = spiflash.SpiFlash(platform.request("spiflash4x"),
                                                         dummy=10, div=4)
            self.add_constant("SPIFLASH_PAGE_SIZE", 256)
            self.add_constant("SPIFLASH_SECTOR_SIZE", 0x10000)
            self.flash_boot_address = 0x180000
            self.register_rom(self.spiflash.bus, 0x1000000)
        platform.add_platform_command("""PIN "hdmi_out_pix_bufg.O" CLOCK_DEDICATED_ROUTE = FALSE;""")
        platform.add_platform_command("""INST "hdmi_out_pix_bufg" LOC = BUFGMUX_X2Y1;""")

_hdmi_infos = {
    "HDMI_OUT0_MNEMONIC": "J4",
    "HDMI_OUT0_DESCRIPTION": "XXX",
}


class VideomixerSoC(BaseSoC):

    csr_map = {
        "hdmi_out0":         20,
    }
    csr_map.update(BaseSoC.csr_map)

    def __init__(self, platform, **kwargs):
        BaseSoC.__init__(self, platform, **kwargs)
        self.submodules.hdmi_out0 = HDMIOut(platform.request("hdmi", 0),
                                            self.sdram.crossbar.get_master())

        platform.add_extension(papilio_adapter_io)
        debug_ios = platform.request("debug_ios")

        self.comb += [
            debug_ios[0].eq(self.hdmi_out0.driver.crc_checker.sink.sop),
            debug_ios[1].eq(self.hdmi_out0.driver.crc_checker.sink.stb),
            debug_ios[2].eq(self.hdmi_out0.driver.crc_checker.sink.ack),
            debug_ios[3].eq(self.hdmi_out0.driver.crc_checker.sink.eop),
            debug_ios[4].eq(self.hdmi_out0.driver.crc_checker.fifo.source.sop),
            debug_ios[5].eq(self.hdmi_out0.driver.crc_checker.fifo.source.stb),
            debug_ios[6].eq(self.hdmi_out0.driver.crc_checker.fifo.source.ack),
            debug_ios[7].eq(self.hdmi_out0.driver.crc_checker.fifo.source.eop),
            debug_ios[8].eq(self.hdmi_out0.driver.crc_checker.source.sop),
            debug_ios[9].eq(self.hdmi_out0.driver.crc_checker.source.stb),
            debug_ios[10].eq(self.hdmi_out0.driver.crc_checker.source.ack),
            debug_ios[11].eq(self.hdmi_out0.driver.crc_checker.source.eop),
        ]

        for k, v in _hdmi_infos.items():
            self.add_constant(k, v)

default_subtarget = VideomixerSoC
