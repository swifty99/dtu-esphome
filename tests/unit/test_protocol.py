import importlib.util
from pathlib import Path


def load_protocol_module():
    path = Path(__file__).parents[2] / "components" / "hoymiles_dtu" / "protocol.py"
    spec = importlib.util.spec_from_file_location("hoymiles_protocol", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_serial_validation_accepts_real_hoymiles_width():
    protocol = load_protocol_module()
    assert protocol.parse_serial("114180000000") == 114180000000


def test_serial_validation_rejects_bad_values():
    protocol = load_protocol_module()
    for value in ["11418000000", "1141800000000", "11418A000000"]:
        try:
            protocol.parse_serial(value)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{value!r} should be rejected")


def test_radio_id_uses_low_serial_bytes_reversed_with_pipe_suffix():
    protocol = load_protocol_module()
    serial = 114180000000
    low32 = serial & 0xFFFFFFFF
    expected = (
        ((low32 & 0xFF) << 32)
        | (((low32 >> 8) & 0xFF) << 24)
        | (((low32 >> 16) & 0xFF) << 16)
        | (((low32 >> 24) & 0xFF) << 8)
        | 0x01
    )
    assert protocol.radio_id_from_serial(serial) == expected


def test_crc_known_vectors():
    protocol = load_protocol_module()
    assert protocol.crc8(bytes.fromhex("1501020304")) == 0x11
    assert protocol.crc16(bytes.fromhex("0b000102030405")) == 0x7BAF
