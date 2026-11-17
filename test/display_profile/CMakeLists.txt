# Host tests for lib/DisplayProfileCore (tasks E3, D1 and D2): the profile table and clearing limits, the
# simulator timing model (defaults < timings file < environment), statistics and their percentiles,
# the modelled backend ("sleep the remainder") through the fake, the device backend's timing and
# timeout logic (TimedBackend) through a fake refresh port, the per-panel record, and the real
# rsvp::Scheduler driven through ProfileDisplaySink; the trial profiles' allow rule, unlock value,
# waveform tables (checked against the pinned SDK header) and exposure limits (task D2).
# test_display_timings.py checks the timings-file validator and generator (scripts/display_timings.py)
# that the simulator build runs.

set(DISPLAY_PROFILE_DIR ${REPO_ROOT}/lib/DisplayProfileCore)
set(RSVP_CORE_DIR ${REPO_ROOT}/lib/RsvpCore)
set(DISPLAY_PROFILE_SOURCES
  ${DISPLAY_PROFILE_DIR}/DisplayProfiles.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayStats.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayTiming.cpp
  ${DISPLAY_PROFILE_DIR}/DisplayBackend.cpp
  ${DISPLAY_PROFILE_DIR}/ProfileDisplaySink.cpp
  ${DISPLAY_PROFILE_DIR}/PanelRecords.cpp
  ${DISPLAY_PROFILE_DIR}/TimedBackend.cpp
  ${DISPLAY_PROFILE_DIR}/TrialUnlock.cpp
  ${DISPLAY_PROFILE_DIR}/TrialWaveforms.cpp
  ${DISPLAY_PROFILE_DIR}/TrialExposure.cpp
)

add_executable(DisplayProfileTest
  ProfileTableTest.cpp
  TimingModelTest.cpp
  StatsTest.cpp
  ModelledBackendTest.cpp
  SchedulerThroughProfileTest.cpp
  TimedBackendTest.cpp
  PanelRecordsTest.cpp
  TrialRulesTest.cpp
  TrialWaveformsTest.cpp
  TrialExposureTest.cpp
  ${DISPLAY_PROFILE_SOURCES}
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

target_include_directories(DisplayProfileTest PRIVATE
  ${DISPLAY_PROFILE_DIR}
  ${RSVP_CORE_DIR}
  ${REPO_ROOT}/test/rsvp_core
)

target_compile_definitions(DisplayProfileTest PRIVATE
  RSVP_REPO_ROOT="${REPO_ROOT}"
  RSVP_TEST_DIR="${REPO_ROOT}/test/rsvp_core"
)

# The library must build without exceptions or RTTI, warning-free, as on the device.
set_source_files_properties(${DISPLAY_PROFILE_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wconversion;-Werror"
)

target_link_libraries(DisplayProfileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DisplayProfileTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME DisplayTimingsFile COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_display_timings.py)
