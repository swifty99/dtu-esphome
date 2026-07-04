from __future__ import annotations


def parse_serial_bcd(value: str | int) -> int:
    text = str(value).strip()
    if not text.isdigit():
        raise ValueError("BCD serial must contain decimal digits only")
    if len(text) != 12:
        raise ValueError("BCD serial must be exactly 12 decimal digits")
    return int(text, 16)


def radio_id_from_low32(serial: int) -> int:
    b0 = serial & 0xFF
    b1 = (serial >> 8) & 0xFF
    b2 = (serial >> 16) & 0xFF
    b3 = (serial >> 24) & 0xFF
    return (b0 << 32) | (b1 << 24) | (b2 << 16) | (b3 << 8) | 0x01


def crc8(data: bytes | bytearray) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x01) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def crc16(data: bytes | bytearray) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = ((crc >> 1) ^ 0xA001) & 0xFFFF
            else:
                crc = (crc >> 1) & 0xFFFF
    return crc


def build_realtime_request(
    inverter_radio_id: int, dtu_serial: int, timestamp: int
) -> bytes:
    buffer = bytearray(27)
    buffer[0] = 0x15
    # Wire byte order must match Ahoy exactly — see hm_build_realtime_request
    # in protocol.cpp.
    buffer[1:5] = (inverter_radio_id >> 8).to_bytes(4, "little")
    buffer[5:9] = dtu_serial.to_bytes(4, "big")
    buffer[9] = 0x80
    buffer[10] = 0x0B
    # Big-endian on the wire — see hm_build_realtime_request in protocol.cpp.
    buffer[12:16] = timestamp.to_bytes(4, "big")
    length = 24
    crc_16 = crc16(buffer[10:length])
    buffer[length] = (crc_16 >> 8) & 0xFF
    buffer[length + 1] = crc_16 & 0xFF
    buffer[length + 2] = crc8(buffer[: length + 2])
    return bytes(buffer)
