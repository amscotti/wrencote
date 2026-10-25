#!/usr/bin/env python3
"""Emit a minimal CycloneDX SBOM for a Wrencote release."""
import json
import sys

version = sys.argv[1] if len(sys.argv) > 1 else "0.2.1"
components = [
    {"type": "library", "name": "wrencote", "version": version},
    {
        "type": "library",
        "name": "wren",
        "version": "0.4.0",
        "description": "Wren scripting language (commit 99d2f0b8fc2686134b32b18166e037639f7e9f2c)",
    },
    {"type": "library", "name": "openssl", "version": "3.0.13", "scope": "required"},
    {"type": "library", "name": "nghttp2", "version": "1.59.0", "scope": "required"},
]
json.dump(
    {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {"component": components[0]},
        "components": components,
    },
    sys.stdout,
    indent=2,
)
sys.stdout.write("\n")
