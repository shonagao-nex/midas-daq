"""Read link dependencies from the installed MIDAS CMake export; never build MIDAS."""
from pathlib import Path
import re


def link_libraries(midas):
    target = (Path(midas) / 'lib/midas-targets.cmake').read_text()
    matches = re.findall(r'INTERFACE_LINK_LIBRARIES "([^"]+)"', target)
    if not matches:
        raise RuntimeError('Missing installed MIDAS CMake link interface')
    libraries = matches[0].split(';')
    if not all(re.fullmatch(r'[-A-Za-z0-9_/+.]+', lib) for lib in libraries):
        raise RuntimeError('Unsupported MIDAS link interface; review before building')
    return [lib if lib.startswith(('/', '-')) else '-l' + lib for lib in libraries]
