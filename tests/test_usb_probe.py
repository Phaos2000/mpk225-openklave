"""Offline regression tests; no hardware or Windows services required."""
import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "tools" / "mpk225_usb_probe.py"
spec = importlib.util.spec_from_file_location("usb_probe", SCRIPT)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.sample = [
            {"Name": "MPK225", "PNPDeviceID": "USB\\VID_09E8&PID_1234\\SENSITIVE_SERIAL", "Service": "usbccgp", "Status": "OK"},
            {"Name": "USB Audio Device", "PNPDeviceID": "USB\\VID_09E8&PID_1234&MI_00\\6&private&0&0", "Service": "usbaudio", "Status": "OK",
             "HardwareID": ["USB\\VID_09E8&PID_1234&REV_0100&MI_00"]},
            {"Name": "Unrelated Akai device", "PNPDeviceID": "USB\\VID_09E8&PID_ABCD\\PRIVATE", "Service": "usbccgp"},
        ]

    def test_device_grouping(self):
        matches = probe.select_mpk_devices(self.sample)
        self.assertEqual(len(matches), 2)
        self.assertEqual(probe.device_vid_pid(matches[0]), ("09E8", "1234"))

    def test_explicit_vid_pid(self):
        self.assertEqual(len(probe.select_mpk_devices(self.sample, "09e8:abcd")), 1)
        with self.assertRaises(ValueError):
            probe.select_mpk_devices(self.sample, "BAD")

    def test_no_serials_in_report(self):
        report = str(probe.safe_identity(self.sample[0]))
        self.assertNotIn("SENSITIVE_SERIAL", report)
        self.assertIn("09E8:1234", report)
        detail = probe.safe_identity(self.sample[1])
        self.assertEqual(detail["interface_number"], "00")
        self.assertNotIn("private", str(detail))

    def test_json_single_and_list(self):
        self.assertEqual(len(probe.parse_pnp_json('{"Name":"MPK225"}')), 1)
        self.assertEqual(probe.parse_pnp_json("[]"), [])
        with self.assertRaises(ValueError):
            probe.parse_pnp_json('"wrong"')

    def test_midi_decode(self):
        self.assertIn("NOTE_ON", probe.describe_midi([0x90, 60, 99]))
        self.assertIn("NOTE_OFF", probe.describe_midi([0x90, 60, 0]))
        self.assertIn("CC", probe.describe_midi([0xB0, 1, 25]))
        self.assertIn("PITCH_BEND", probe.describe_midi([0xE0, 0, 64]))

    def test_no_transmit_functions(self):
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn("MidiOut", source)
        self.assertNotIn("send_message", source)
        self.assertNotIn("flash_os", source)
        self.assertNotIn("wipe_os", source)


if __name__ == "__main__":
    unittest.main()