################################################################################################
# Project Datum/HEPData
#
# Import and export of histograms, data points and functions as HEPData YAML files (see the design document
# "Datum: HEPData YAML import/export and systematic errors"). Built when DATUM_WITH_YAML is ON; links yaml-cpp
# (${DATUM_YAML_TARGET}, see src/CMakeLists.txt), privately: the headers of this library do not include yaml-cpp.
#
#   HEPDataTable       errors, values, qualifiers, variables, keywords, resources, a table and its data file
#   HEPDataSubmission  submission.yaml and the tables: read from a directory, a file or an archive; written
#   HEPDataConverter   tables to Datum objects (Histogram, HistogramSyst, DataPointsVector...) and back
################################################################################################
add_compile_options(-Wall -Wextra -pedantic)
add_library(HEPData SHARED
HEPData.cpp
HEPDataConverter.cpp)

target_link_libraries(HEPData PUBLIC Base Math PRIVATE ${DATUM_YAML_TARGET})
target_include_directories(HEPData PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

datum_install(HEPData LIBRARY "$ENV{DATUM_LIB_PATH}")

# Tests (run with ctest in the build directory; not installed)
add_executable(TestYamlCpp tests/TestYamlCpp.cpp)
target_link_libraries(TestYamlCpp ${DATUM_YAML_TARGET})
add_test(NAME YamlCpp COMMAND TestYamlCpp)
add_executable(TestHEPData tests/TestHEPData.cpp)
target_link_libraries(TestHEPData HEPData Base)
add_test(NAME HEPData COMMAND TestHEPData ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
add_executable(TestHEPDataConverter tests/TestHEPDataConverter.cpp)
target_link_libraries(TestHEPDataConverter HEPData Math Base)
add_test(NAME HEPDataConverter COMMAND TestHEPDataConverter ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
