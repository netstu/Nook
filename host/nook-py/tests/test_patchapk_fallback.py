import os
import sys
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest import mock


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)


from tools import nook_patchapk  # noqa: E402


class PatchApkFallbackTests(unittest.TestCase):
    def test_default_gadget_listen_address_uses_localabstract_socket(self):
        self.assertEqual(
            nook_patchapk.derive_default_gadget_listen_address("com.demo.target"),
            "@nook-gadget-com.demo.target",
        )

    def test_rewrite_manifest_injects_internet_permission_when_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = os.path.join(temp_dir, "AndroidManifest.xml")
            with open(manifest_path, "w", encoding="utf-8") as handle:
                handle.write(
                    """<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.demo.target">
    <application android:label="Demo"></application>
</manifest>"""
                )

            nook_patchapk.rewrite_manifest_for_bootstrap(temp_dir)

            tree = ET.parse(manifest_path)
            root = tree.getroot()
            android_ns = "{http://schemas.android.com/apk/res/android}"
            permissions = [
                node.attrib.get(f"{android_ns}name", "")
                for node in root.findall("uses-permission")
            ]
            self.assertIn("android.permission.INTERNET", permissions)

    def test_detects_private_android_resource_failure(self):
        stderr = (
            "error: resource android:color/Teal_800 is private.\n"
            "error: resource android:color/Blue_700 is private.\n"
        )
        self.assertTrue(nook_patchapk.should_retry_apktool_without_aapt2(stderr))

    def test_ignores_unrelated_build_failure(self):
        stderr = "brut.androlib.exceptions.AndrolibException: something else failed"
        self.assertFalse(nook_patchapk.should_retry_apktool_without_aapt2(stderr))

    def test_detects_raw_resources_retry_failure(self):
        stderr = (
            "error: No resource identifier found for attribute 'lStar' in package 'android'\n"
            "error: Error: Resource is not public. (at 'color' with value '@android:color/Purple_800').\n"
        )
        self.assertTrue(nook_patchapk.should_retry_apktool_with_raw_resources(stderr))

    def test_run_apktool_build_retries_without_aapt2_for_private_resource_failure(self):
        failure = subprocess.CalledProcessError(
            1,
            ["apktool", "b", "decoded", "-o", "out.apk", "--use-aapt2"],
            output="",
            stderr="error: resource android:color/Teal_800 is private.\n",
        )
        with mock.patch("tools.nook_patchapk.run_progress_command", side_effect=[failure, None]) as run:
            nook_patchapk.run_apktool_build(
                "apktool",
                "decoded",
                "out.apk",
                use_aapt2=True,
            )

        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                ["apktool", "b", "decoded", "-o", "out.apk", "--use-aapt2"],
                ["apktool", "b", "decoded", "-o", "out.apk"],
            ],
        )
        for call in run.call_args_list:
            self.assertEqual(call.kwargs["progress_label"], "apktool build")

    def test_run_apktool_decode_supports_raw_resources_mode(self):
        with mock.patch("tools.nook_patchapk.run_progress_command") as run:
            nook_patchapk.run_apktool_decode(
                "apktool",
                "input.apk",
                "decoded",
                no_resources=True,
            )

        run.assert_called_once_with(
            ["apktool", "d", "-f", "-r", "-o", "decoded", "input.apk"],
            progress_label="apktool decode",
        )

    def test_restore_decoded_manifest_after_raw_resources_decode(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            first_pass_manifest = os.path.join(temp_dir, "AndroidManifest.first.xml")
            retry_dir = os.path.join(temp_dir, "retry")
            os.makedirs(retry_dir, exist_ok=True)
            retry_manifest = os.path.join(retry_dir, "AndroidManifest.xml")

            expected_manifest = "<manifest package='com.demo.target'></manifest>"
            with open(first_pass_manifest, "w", encoding="utf-8") as handle:
                handle.write(expected_manifest)
            with open(retry_manifest, "wb") as handle:
                handle.write(b"\x03\x00\x08\x00raw")

            preserved = nook_patchapk.read_decoded_manifest_text(first_pass_manifest)
            self.assertEqual(preserved, expected_manifest)

            nook_patchapk.restore_decoded_manifest_text(retry_manifest, preserved)

            with open(retry_manifest, "r", encoding="utf-8") as handle:
                self.assertEqual(handle.read(), expected_manifest)

    def test_restore_binary_manifest_after_temporary_text_override(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = os.path.join(temp_dir, "AndroidManifest.xml")
            original_binary = b"\x03\x00\x08\x00binary-manifest"
            temporary_text = "<manifest package='com.demo.target'></manifest>"

            with open(manifest_path, "wb") as handle:
                handle.write(original_binary)

            preserved_binary = nook_patchapk.read_manifest_bytes(manifest_path)
            self.assertEqual(preserved_binary, original_binary)

            nook_patchapk.restore_decoded_manifest_text(manifest_path, temporary_text)
            with open(manifest_path, "r", encoding="utf-8") as handle:
                self.assertEqual(handle.read(), temporary_text)

            nook_patchapk.restore_manifest_bytes(manifest_path, preserved_binary)
            with open(manifest_path, "rb") as handle:
                self.assertEqual(handle.read(), original_binary)


if __name__ == "__main__":
    unittest.main()
