# Host tests for the portable parts of the device test screens (tasks D3a and D3b,
# src/activities/devicetest/): the sweep plan, its counts and the decisions the owner's answers lead
# to, the series runner driven through every step of every profile with the fake display backend, the
# passage and its word count, the ChoiceList highlight rules, the Controls check's key tracker, the
# stored unlock value after a benchmark, the results file names and their JSON encoding; the guided
# acceptance's step table, walk-through and verdicts, its progress file, and the profile choice and
# Settings row rules (D3b). test_results_schema.py checks the encoded files (written by
# DeviceTestSamples) against tools/device/schema/rsvp-bench.schema.json.

set(RSVP_CORE_DIR ${REPO_ROOT}/lib/RsvpCore)
set(DISPLAY_PROFILE_DIR ${REPO_ROOT}/lib/DisplayProfileCore)
set(DEVICE_TEST_DIR ${REPO_ROOT}/src/activities/devicetest)
set(DEVICE_TEST_SOURCES
  ${DEVICE_TEST_DIR}/Acceptance.cpp
  ${DEVICE_TEST_DIR}/AcceptanceProgress.cpp
  ${DEVICE_TEST_DIR}/BenchPlan.cpp
  ${DEVICE_TEST_DIR}/BenchJson.cpp
  ${DEVICE_TEST_DIR}/ChoiceList.cpp
  ${DEVICE_TEST_DIR}/ControlsCheck.cpp
  ${DEVICE_TEST_DIR}/Passage.cpp
  ${DEVICE_TEST_DIR}/ProfileChoice.cpp
  ${DEVICE_TEST_DIR}/SeriesRunner.cpp
  ${REPO_ROOT}/src/controls/ControlMap.cpp
)
set(DEVICE_TEST_LIBS
  ${DISPLAY_PROFILE_DIR}/DisplayProfiles.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayStats.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayTiming.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayBackend.cpp
  ${DISPLAY_PROFILE_DIR}/ProfileDisplaySink.cpp
  ${DISPLAY_PROFILE_DIR}/PanelRecords.cpp
  ${DISPLAY_PROFILE_DIR}/TrialUnlock.cpp
  ${DISPLAY_PROFILE_DIR}/TrialWaveforms.cpp
  ${DISPLAY_PROFILE_DIR}/TrialExposure.cpp
  ${RSVP_CORE_DIR}/RsvpText.cpp
  ${RSVP_CORE_DIR}/WordClassifier.cpp
  ${RSVP_CORE_DIR}/FocusLetter.cpp
  ${RSVP_CORE_DIR}/Pacing.cpp
  ${RSVP_CORE_DIR}/FrameBuilder.cpp
  ${RSVP_CORE_DIR}/ClearingPolicy.cpp
  ${RSVP_CORE_DIR}/DeliveredWpmMeter.cpp
  ${RSVP_CORE_DIR}/WordOrderAudit.cpp
  ${RSVP_CORE_DIR}/Scheduler.cpp
)

add_library(device_test_code STATIC ${DEVICE_TEST_SOURCES} ${DEVICE_TEST_LIBS})
target_include_directories(device_test_code PUBLIC
  ${REPO_ROOT}/src
  ${RSVP_CORE_DIR}
  ${DISPLAY_PROFILE_DIR}
)
target_compile_definitions(device_test_code PUBLIC CROSSPOINT_RSVP=1)
target_link_libraries(device_test_code PUBLIC crosspoint_test_common)
# The firmware code under test builds without exceptions or RTTI, warning-free.
set_source_files_properties(${DEVICE_TEST_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Werror"
)

add_executable(DeviceTestTest
  PassageTest.cpp
  ChoiceListTest.cpp
  BenchPlanTest.cpp
  SeriesRunnerTest.cpp
  ControlsCheckTest.cpp
  BenchJsonTest.cpp
  AcceptanceTest.cpp
  AcceptanceProgressTest.cpp
  ProfileChoiceTest.cpp
)
target_link_libraries(DeviceTestTest PRIVATE device_test_code GTest::gtest_main)
gtest_discover_tests(DeviceTestTest)

# Writes sample results files (a benchmark with a profile skipped part-way and the run ended by the
# owner, a complete benchmark on SE0400NQW47, a Controls check, a guided acceptance part-way and one
# complete, and a profile choice) for the schema test.
add_executable(DeviceTestSamples DeviceTestSamples.cpp)
target_link_libraries(DeviceTestSamples PRIVATE device_test_code)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DEVICE_TEST_SAMPLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/samples)
add_test(NAME DeviceTestSamplesWrite COMMAND DeviceTestSamples ${DEVICE_TEST_SAMPLE_DIR})
set_tests_properties(DeviceTestSamplesWrite PROPERTIES FIXTURES_SETUP device_test_samples)
add_test(NAME DeviceTestResultsSchema
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_results_schema.py ${DEVICE_TEST_SAMPLE_DIR})
set_tests_properties(DeviceTestResultsSchema PROPERTIES FIXTURES_REQUIRED device_test_samples)
