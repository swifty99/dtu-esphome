import importlib.util
from pathlib import Path


def load_protocol_module():
    path = Path(__file__).parents[2] / "components" / "hoymiles_dtu" / "protocol.py"
    spec = importlib.util.spec_from_file_location("hoymiles_protocol", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_inverter_config_serial_is_bcd_packed():
    # The inverter `serial:` config (validate_serial in __init__.py) interprets the
    # printed digits as packed BCD, exactly like Ahoy. Guard against a regression
    # back to decimal, which would transmit to the wrong nRF address (01 0D 08 F1 CD).
    protocol = load_protocol_module()
    packed = protocol.parse_serial_bcd("116182806989")
    assert packed == 0x116182806989
    radio_id = protocol.radio_id_from_low32(packed)
    address = bytes((radio_id >> (8 * i)) & 0xFF for i in range(5))
    assert address == bytes.fromhex("01 82 80 69 89")


def test_crc_known_vectors():
    protocol = load_protocol_module()
    assert protocol.crc8(bytes.fromhex("1501020304")) == 0x11
    assert protocol.crc16(bytes.fromhex("0b000102030405")) == 0x7BAF


def test_realtime_request_uses_ahoy_byte_order():
    protocol = load_protocol_module()
    inverter_radio_id = protocol.radio_id_from_low32(
        protocol.parse_serial_bcd("116182806989")
    )
    request = protocol.build_realtime_request(
        inverter_radio_id, dtu_serial=0x83915460, timestamp=0x12345678
    )

    assert len(request) == 27
    assert request[0] == 0x15
    # Inverter id [1..4] and DTU id [5..8] are the address bytes (0x01 + idbytes)
    # in wire order: [1..4] = LSB-first of (radio_id>>8); [5..8] = big-endian
    # dtu_serial. Reversing either makes the inverter ACK but reject the request
    # (it checks [1..4]) / reply to the wrong address ([5..8]).
    assert request[1:5] == (inverter_radio_id >> 8).to_bytes(4, "little")
    assert request[1:5] == bytes.fromhex("82806989")
    assert request[5:9] == bytes.fromhex("83915460")
    assert request[9:12] == bytes.fromhex("800b00")
    # Timestamp is big-endian on the wire (proven from a live Ahoy capture; Ahoy's
    # CP_U32_LittleEndian macro is misnamed and actually writes MSB-first).
    assert request[12:16] == bytes.fromhex("12345678")
    assert request[24:26] == protocol.crc16(request[10:24]).to_bytes(2, "big")
    assert request[26] == protocol.crc8(request[:26])
