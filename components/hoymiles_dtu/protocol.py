from __future__ import annotations


def parse_serial(value: str | int) -> int:
    text = str(value).strip()
    if not text.isdigit():
        raise ValueError("serial must contain decimal digits only")
    if len(text) != 12:
        raise ValueError("serial must be exactly 12 decimal digits")
    return int(text, 10)


def radio_id_from_serial(value: str | int) -> int:
    serial = parse_serial(value)
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
