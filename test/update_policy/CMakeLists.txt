# Host tests for lib/UpdatePolicy (task D4): the self-update slot policy and the Wi-Fi updater's
# asset name and version rule. The slot tests read the repository's real partition tables.

set(UPDATE_POLICY_DIR ${REPO_ROOT}/lib/UpdatePolicy)
set(UPDATE_POLICY_SOURCES
  ${UPDATE_POLICY_DIR}/SlotPolicy.cpp
  ${UPDATE_POLICY_DIR}/ReleaseNaming.cpp
)

add_executable(UpdatePolicyTest
  SlotPolicyTest.cpp
  ReleaseNamingTest.cpp
  ${UPDATE_POLICY_SOURCES}
)

# The library must build without exceptions or RTTI, warning-free, as on the device.
set_source_files_properties(${UPDATE_POLICY_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wconversion;-Wshadow;-Werror"
)

target_include_directories(UpdatePolicyTest PRIVATE ${UPDATE_POLICY_DIR})
target_compile_definitions(UpdatePolicyTest PRIVATE UPDATE_POLICY_REPO_ROOT="${REPO_ROOT}")
target_link_libraries(UpdatePolicyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UpdatePolicyTest)
