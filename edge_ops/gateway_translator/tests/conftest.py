"""pytest configuration and fixtures for gateway_translator tests."""

import os
import sys

# Ensure src/ is on sys.path so that source modules are importable
# without package-relative prefixes.
_src_path = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "src"
)
if _src_path not in sys.path:
    sys.path.insert(0, _src_path)
