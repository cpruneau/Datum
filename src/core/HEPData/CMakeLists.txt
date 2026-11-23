################################################################################################
# Project Datum/HEPData
#
# Import and export of histograms, data points and functions as HEPData YAML files (see the design document
# "Datum: HEPData YAML import/export and systematic errors"). Built when DATUM_WITH_YAML is ON; links yaml-cpp
# (${DATUM_YAML_TARGET}, see src/CMakeLists.txt).
#
# Step 3 of the plan: yaml-cpp only, checked by TestYamlCpp. The library comes with step 4.
################################################################################################
add_compile_options(-Wall -Wextra -pedantic)

# Tests (run with ctest in the build directory; not installed)
add_executable(TestYamlCpp tests/TestYamlCpp.cpp)
target_link_libraries(TestYamlCpp ${DATUM_YAML_TARGET})
add_test(NAME YamlCpp COMMAND TestYamlCpp)
