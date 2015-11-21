from migen.fhdl.std import *
from migen.flow.network import *
from migen.genlib.fsm import FSM, NextState
from migen.actorlib.fifo import SyncFIFO
from misoclib.com.liteethmini.mac.core.crc import LiteEthMACCRC32


class CRCChecker(Module):
    """CRC Checker

    Check CRC at the end of each frame.

    Parameters
    ----------
    description : description
        description of the dataflow.

    Attributes
    ----------
    sink : in
        Frame input with CRC.
    source : out
        Frame output without CRC and "error" set to 0
        on eop when CRC OK / set to 1 when CRC KO.
    """
    def __init__(self, crc_class, description):
        self.sink = sink = Sink(EndpointDescription(description, packetized=True))
        self.source = source = Source(EndpointDescription(description, packetized=True))
        self.busy = Signal()

        # # #

        dw = flen(sink.y) + flen(sink.cb_cr)
        self.crc = crc = crc_class(dw)
        self.submodules += crc
        ratio = crc.width//dw

        #error = Signal()
        self.fifo = fifo = InsertReset(SyncFIFO(EndpointDescription(description, packetized=True), ratio + 1))
        self.submodules += fifo

        self.fsm = fsm = FSM(reset_state="RESET")
        self.submodules += fsm

        fifo_in = Signal()
        fifo_out = Signal()
        fifo_full = Signal()
        self.error = Signal()

        self.comb += [
            fifo_full.eq(fifo.fifo.level == ratio),
            fifo_in.eq(sink.stb & (~fifo_full | fifo_out)),
            fifo_out.eq(source.stb & source.ack),

            Record.connect(sink, fifo.sink),
            fifo.sink.stb.eq(fifo_in),
            self.sink.ack.eq(fifo_in),

            source.stb.eq(sink.stb & fifo_full),
            source.sop.eq(fifo.source.sop),
            source.eop.eq(sink.eop),
            fifo.source.ack.eq(fifo_out),
            source.payload.eq(fifo.source.payload),

            self.error.eq(crc.error),
        ]

        self.crc_value = Signal(32)
        self.crc_value_r = Signal(32)

        self.sync += [
            self.crc_value.eq(self.crc_value_r),
            self.crc_value_r.eq(crc.value),
        ]

        self.debug_reset, self.debug_idle, self.debug_copy = (Signal() for i in range(3))

        fsm.act("RESET",
            self.debug_reset.eq(1),
            crc.reset.eq(1),
            fifo.reset.eq(1),
            NextState("IDLE"),
        )
        self.comb += crc.data.eq(Cat(sink.cb_cr, sink.y))
        fsm.act("IDLE",
            self.debug_idle.eq(1),
            If(sink.stb & sink.sop & sink.ack,
                crc.ce.eq(1),
                NextState("COPY")
            )
        )
        fsm.act("COPY",
            self.debug_copy.eq(1),
            If(sink.stb & sink.ack,
                crc.ce.eq(1),
                If(sink.eop,
                    NextState("RESET")
                )
            )
        )
        self.comb += self.busy.eq(~fsm.ongoing("IDLE"))


class CRC32Checker(CRCChecker):
    def __init__(self, description):
        CRCChecker.__init__(self, LiteEthMACCRC32, description)
