from pathlib import Path

from setuptools import find_packages, setup


README = Path(__file__).with_name("README.md").read_text(encoding="utf-8")


setup(
    name="nook-cli",
    version="0.1.3",
    description="Python CLI for the Nook Android instrumentation server",
    long_description=README,
    long_description_content_type="text/markdown",
    author="Nook Contributors",
    author_email="3049155267@qq.com",
    url="https://github.com/x1aon1ng/Nook",
    license="MIT",
    packages=find_packages(include=["nook", "nook.*"]),
    package_data={"nook": ["dexdump.js", "sodump.js"]},
    entry_points={
        "console_scripts": [
            "nook-cli=nook.cli:main",
            "nook-gadget=nook.gadget_cli:main",
        ],
    },
)
