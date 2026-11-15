# Run the repository's test-header portability guard before the long build.
get_filename_component(SCORE_ROOT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SCORE_TEST_GUARDS_FATAL ON)
include("${SCORE_ROOT_SOURCE_DIR}/cmake/ScoreRhiPortabilityGuard.cmake")
score_check_rhi_portability()
