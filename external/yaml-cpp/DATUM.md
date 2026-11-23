# yaml-cpp in Datum

yaml-cpp 0.8.0 (https://github.com/jbeder/yaml-cpp, tag 0.8.0), MIT licence (see LICENSE), distributed with Datum to
read and write YAML files (HEPData submissions). Only what the library needs is kept: CMakeLists.txt, include/, src/,
the three *.in files, LICENSE and README.md (not test/, util/, docs/).

Changes to the original:
- CMakeLists.txt: cmake_minimum_required 3.4 -> 3.10 (CMake 4 refuses versions below 3.5 and warns below 3.10).
- src/emitterutils.cpp: #include <cstdint> (uint16_t, uint32_t; needed by GCC 13 and later).

Datum builds it as a static library (position-independent, no install, no tests or tools) when DATUM_WITH_YAML is ON
and no yaml-cpp is found on the system (see src/CMakeLists.txt).
