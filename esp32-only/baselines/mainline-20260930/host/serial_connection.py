"""Open UART with control-line states configured before acquiring the port."""
from contextlib import contextmanager

import serial


@contextmanager
def camera_connection(port, baud, record_trace):
    connection = serial.Serial(
        port=None, baudrate=baud, timeout=5.0, write_timeout=10.0,
        rtscts=False, dsrdtr=False,
    )
    try:
        # With port=None these setters store states without touching hardware.
        # Drivers can still pulse the physical lines during open; verify boot_id.
        connection.dtr = False
        connection.rts = False
        connection.port = port
        record_trace(dict(event='port_control_lines', phase='before_open',
                          dtr=connection.dtr, rts=connection.rts,
                          rtscts=connection.rtscts, dsrdtr=connection.dsrdtr))
        connection.open()
        record_trace(dict(event='port_opened', dtr=connection.dtr,
                          rts=connection.rts, strategy='control_lines_before_open'))
        yield connection
    finally:
        # Also release the handle if startup, streaming, or open itself fails.
        connection.close()
        record_trace(dict(event='port_closed', is_open=connection.is_open))
