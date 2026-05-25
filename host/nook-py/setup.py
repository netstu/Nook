from pathlib import Path

from setuptools import setup


README = Path(__file__).with_name("README.md").read_text(encoding="utf-8")


setup(
    name="nook-cli",
    version="0.0.1",
    description="Python CLI for the Nook Android instrumentation server",
    long_description=README,
    long_description_content_type="text/markdown",
    author="Nook Contributors",
    author_email="3049155267@qq.com",
    url="https://github.com/x1aon1ng/Nook",
    license="MIT",
    packages=["nook"],
    entry_points={
        "console_scripts": [
            "nook-cli=nook.cli:main",
        ],
    },
)
