from pathlib import Path
import unittest


class PackagingMetadataTests(unittest.TestCase):
    def test_pyproject_uses_nook_cli_distribution_name(self):
        pyproject = Path(__file__).resolve().parents[1] / "pyproject.toml"
        content = pyproject.read_text(encoding="utf-8")

        self.assertIn('name = "nook-cli"', content)
        self.assertIn('nook-cli = "nook.cli:main"', content)
        self.assertIn('Homepage = "https://github.com/x1aon1ng/Nook"', content)
        self.assertIn('email = "3049155267@qq.com"', content)


if __name__ == "__main__":
    unittest.main()
