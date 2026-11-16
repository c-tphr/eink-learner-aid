add_executable(RsvpCoreTest
  FocusLetterTest.cpp
  WordClassifierTest.cpp
  PacingTest.cpp
  FrameBuilderTest.cpp
  SentenceStepTest.cpp
  FixtureBookTest.cpp
  ProseSampleTest.cpp
  ClearingPolicyTest.cpp
  DeliveredWpmMeterTest.cpp
  WordOrderAuditTest.cpp
  SchedulerTest.cpp
  PlaybackSweepTest.cpp
  ${REPO_ROOT}/lib/RsvpCore/RsvpText.cpp
  ${REPO_ROOT}/lib/RsvpCore/WordClassifier.cpp
  ${REPO_ROOT}/lib/RsvpCore/FocusLetter.cpp
  ${REPO_ROOT}/lib/RsvpCore/Pacing.cpp
  ${REPO_ROOT}/lib/RsvpCore/FrameBuilder.cpp
  ${REPO_ROOT}/lib/RsvpCore/SentenceStep.cpp
  ${REPO_ROOT}/lib/RsvpCore/ClearingPolicy.cpp
  ${REPO_ROOT}/lib/RsvpCore/DeliveredWpmMeter.cpp
  ${REPO_ROOT}/lib/RsvpCore/WordOrderAudit.cpp
  ${REPO_ROOT}/lib/RsvpCore/Scheduler.cpp
)

target_compile_definitions(RsvpCoreTest PRIVATE
  RSVP_REPO_ROOT="${REPO_ROOT}"
  RSVP_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# The library must build without exceptions or RTTI, as on the device.
set_source_files_properties(
  ${REPO_ROOT}/lib/RsvpCore/RsvpText.cpp
  ${REPO_ROOT}/lib/RsvpCore/WordClassifier.cpp
  ${REPO_ROOT}/lib/RsvpCore/FocusLetter.cpp
  ${REPO_ROOT}/lib/RsvpCore/Pacing.cpp
  ${REPO_ROOT}/lib/RsvpCore/FrameBuilder.cpp
  ${REPO_ROOT}/lib/RsvpCore/SentenceStep.cpp
  ${REPO_ROOT}/lib/RsvpCore/ClearingPolicy.cpp
  ${REPO_ROOT}/lib/RsvpCore/DeliveredWpmMeter.cpp
  ${REPO_ROOT}/lib/RsvpCore/WordOrderAudit.cpp
  ${REPO_ROOT}/lib/RsvpCore/Scheduler.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Werror"
)

target_link_libraries(RsvpCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RsvpCoreTest)
