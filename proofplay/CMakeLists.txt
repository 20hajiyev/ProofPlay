# ProofPlay (hackathon layer): headless record/replay/analysis on top of the game runtime.
add_executable(proofplay
    src/main.cpp
    src/Session.cpp
    src/SessionIO.cpp
    src/Analyze.cpp
)
target_link_libraries(proofplay PRIVATE racer_runtime nlohmann_json)
target_compile_definitions(proofplay PRIVATE RACER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")

# The game records the player's races for ProofPlay (proofplay/src/Recorder.h).
target_sources(racer_sandbox PRIVATE src/Recorder.cpp src/SessionIO.cpp)
target_include_directories(racer_sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(racer_sandbox PRIVATE RACER_PROOFPLAY_SESSIONS="${CMAKE_CURRENT_SOURCE_DIR}/sessions")
