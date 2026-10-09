add_executable(racer_core_tests
    TestMain.cpp
    SimClockTests.cpp
    VehicleTests.cpp
    StabilityAssistTests.cpp
    RecoveryTests.cpp
    RaceTests.cpp
    AITests.cpp
    CombatTests.cpp
    AICombatTests.cpp
    TrackTests.cpp
    CareerTests.cpp
    EventTests.cpp
    AudioLogicTests.cpp
    BodyMotionTests.cpp
    CustomizationTests.cpp
    PerformanceTests.cpp
    WeatherTests.cpp
    DamageTests.cpp
    ContactTests.cpp
    CameraFeelTests.cpp
)
target_link_libraries(racer_core_tests PRIVATE racer_core nlohmann_json)
target_compile_definitions(racer_core_tests PRIVATE RACER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
add_test(NAME racer_core_tests COMMAND racer_core_tests)

if(RACER_WITH_ENGINE)
    add_executable(racer_handling_tests
        TestMain.cpp
        HandlingTests.cpp
        FontTests.cpp
    )
    target_link_libraries(racer_handling_tests PRIVATE racer_runtime nlohmann_json)
    # FontTests reads Wicked's embedded font header (Utility/liberation_sans.h, stb_truetype.h).
    target_include_directories(racer_handling_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/WickedEngine/WickedEngine)
    target_compile_definitions(racer_handling_tests PRIVATE RACER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
    add_test(NAME handling COMMAND racer_handling_tests)
endif()

add_executable(racer_audio_fixture
    TestMain.cpp
    AudioFixtureTests.cpp
)
target_link_libraries(racer_audio_fixture PRIVATE miniaudio)
add_test(NAME audio_fixture COMMAND racer_audio_fixture)

add_executable(racer_audio_tests
    TestMain.cpp
    AudioRuntimeTests.cpp
)
target_link_libraries(racer_audio_tests PRIVATE racer_audio)
target_compile_definitions(racer_audio_tests PRIVATE RACER_AUDIO_DIR="${PROJECT_SOURCE_DIR}/assets/audio/game")
add_test(NAME audio_runtime COMMAND racer_audio_tests)
