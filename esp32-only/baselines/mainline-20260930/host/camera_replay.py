"""Per-connection camera ordering state. Only commit authenticated frames."""
from host.serial_protocol import ProtocolError


class CameraReplayGuard:
    def __init__(self, rekey_every):
        if not 1 <= rekey_every <= 100000:
            raise ValueError('Invalid rekey interval')
        self.limit = rekey_every
        self.epoch = None
        self.last_frame = None
        self.count = 0

    def begin_session(self, epoch):
        # Call only after identity verification and session key confirmation.
        if not 1 <= epoch <= 0xffffffff:
            raise ProtocolError('Invalid session epoch')
        if self.epoch is not None and epoch != self.epoch + 1:
            raise ProtocolError('Rekey epoch must advance by exactly one')
        self.epoch = epoch
        self.count = 0
        # frame_id belongs to the boot, so retain it across rekey.

    def accept_authenticated(self, epoch, frame_id, status):
        # Caller MUST authenticate metadata with GCM before invoking this method.
        # Plaintext status is checked for consistency, never used as proof.
        if self.epoch is None or epoch != self.epoch or status.epoch != self.epoch:
            raise ProtocolError('Camera replay guard: wrong session epoch')
        if not 0 <= frame_id <= 0xffffffff:
            raise ProtocolError('Camera replay guard: invalid frame ID')
        if self.last_frame is not None and frame_id != self.last_frame + 1:
            raise ProtocolError(
                f'Camera replay/order rejected: expected frame {self.last_frame + 1}, received {frame_id}')
        expected_count = self.count + 1
        if expected_count > self.limit or status.count != expected_count:
            raise ProtocolError('Camera replay guard: unexpected session count')
        if status.rekey != (expected_count == self.limit):
            raise ProtocolError('Camera replay guard: inconsistent rekey flag')
        self.last_frame = frame_id
        self.count = expected_count
