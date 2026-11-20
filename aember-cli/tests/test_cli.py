"""The CLI's pure parts: SDK paths, manifests, argument handling."""

from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aember import build, cli, project, sdk

ROOT = Path(__file__).resolve().parents[2]


class SdkTest(unittest.TestCase):
    def test_every_aember_architecture_has_a_sysroot(self):
        self.assertEqual(len(sdk.ARCHS), 8)
        for arch in sdk.ARCHS:
            base = arch.removesuffix("-musl")
            self.assertIn(base, sdk.ARCHS)
            if arch.endswith("-musl"):
                self.assertIn("musl", sdk.SYSROOTS[arch])

    def test_paths_follow_aember_resources_dir(self):
        with mock.patch.dict(os.environ, {"AEMBER_RESOURCES_DIR": "/res"}):
            self.assertEqual(
                sdk.env_script(ROOT, "x64-poky-linux"),
                Path("/res/x64-poky-linux/sdk/environment-setup-core2-64-poky-linux"),
            )
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(
                sdk.sdk_dir(ROOT, "arm-poky-linux"),
                ROOT / "build/aember-resources/arm-poky-linux/sdk",
            )

    def test_installer_url_names_the_release(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            url = sdk.installer_url("riscv64-poky-linux-musl")
        self.assertTrue(
            url.endswith(
                f"/v{project.RESOURCES_VERSION}/aember-sdk-riscv64-poky-linux-musl"
                f"-v{project.RESOURCES_VERSION}.sh"
            )
        )

    def test_unknown_arch_and_missing_sdk_are_errors(self):
        with self.assertRaises(ValueError):
            sdk.sdk_dir(ROOT, "mips-poky-linux")
        with mock.patch.dict(os.environ, {"AEMBER_RESOURCES_DIR": "/nonexistent"}):
            with self.assertRaises(FileNotFoundError):
                sdk.shell(ROOT, "x64-poky-linux", "true")

    def test_sdk_shell_clears_ld_library_path_and_sources_the_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.dict(os.environ, {"AEMBER_RESOURCES_DIR": tmp}):
                script = sdk.env_script(ROOT, "x64-poky-linux")
                script.parent.mkdir(parents=True)
                script.write_text("export AEMBER_SDK_TEST=1\n")
                cmd = sdk.shell(ROOT, "x64-poky-linux", "echo $AEMBER_SDK_TEST")
        self.assertTrue(cmd.startswith("unset LD_LIBRARY_PATH; . "))
        self.assertTrue(cmd.endswith("&& echo $AEMBER_SDK_TEST"))


class ManifestTest(unittest.TestCase):
    def test_container_manifest_per_arch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "aember-container.json").write_text(
                (ROOT / "aember-container.json").read_text()
            )
            with mock.patch.dict(os.environ, {"AEMBER_RESOURCES_DIR": "/res"}):
                path = build.container_manifest(root, "aarch64-poky-linux-musl")
            m = json.loads(path.read_text())
        self.assertEqual(m["name"], project.NAME)
        self.assertEqual(m["arch"], "aarch64-poky-linux-musl")
        self.assertEqual(
            m["libs"]["sysroot"],
            "/res/aarch64-poky-linux-musl/sdk/sysroots/cortexa53-poky-linux-musl",
        )
        self.assertEqual(
            m["rootfs"]["dir"], str(root / "build/stage/aarch64-poky-linux-musl")
        )
        for f in m.get("files", []):
            self.assertTrue(Path(f["src"]).is_absolute())

    def test_sysroot_files_and_optional_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "aember-container.json").write_text(
                json.dumps(
                    {
                        "name": project.NAME,
                        "command": ["/x"],
                        "files": [
                            {"src": "{sysroot}/etc/a", "dst": "/etc/a"},
                            {"src": "{sysroot}/nope", "dst": "/n", "optional": True},
                        ],
                    }
                )
            )
            with mock.patch.dict(os.environ, {"AEMBER_RESOURCES_DIR": "/res"}):
                m = json.loads(
                    build.container_manifest(root, "x64-poky-linux").read_text()
                )
        self.assertEqual(
            m["files"],
            [
                {
                    "src": "/res/x64-poky-linux/sdk/sysroots/core2-64-poky-linux/etc/a",
                    "dst": "/etc/a",
                }
            ],
        )

    def test_the_repo_manifest_is_valid_json_with_a_command(self):
        m = json.loads((ROOT / "aember-container.json").read_text())
        self.assertEqual(m["name"], project.NAME)
        self.assertTrue(m["command"])


class CliTest(unittest.TestCase):
    def test_jobs_default_and_override(self):
        with mock.patch.dict(os.environ, {"AEMBER_BUILD_JOBS": "3"}):
            self.assertEqual(build.jobs(), 3)
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertGreaterEqual(build.jobs(), 1)

    def test_unknown_arch_is_rejected_by_argparse(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            cli.main(["build", "-a", "mips"])

    def test_pack_without_stage_fails_cleanly(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(cli, "find_root", return_value=Path(tmp)):
                helper = Path(tmp) / build.BUILD_HELPER
                helper.parent.mkdir(parents=True)
                helper.write_text("")
                self.assertEqual(cli.main(["pack", "-a", "x64-poky-linux"]), 1)


if __name__ == "__main__":
    unittest.main()
