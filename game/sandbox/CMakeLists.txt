set(RACER_COOKED_DIR ${CMAKE_BINARY_DIR}/cooked)

# Content cook: GLB -> Wicked native scene, rebuilt when the source asset changes.
set(COOKED_VEHICLES)
foreach(CAR D01 D02 D03 D04 D05)
    set(CAR_GLB ${PROJECT_SOURCE_DIR}/assets/vehicles_toon/${CAR}/${CAR}.glb) # toon look (D-051)
    set(CAR_COOKED ${RACER_COOKED_DIR}/vehicles/${CAR}.wiscene)
    add_custom_command(
        OUTPUT ${CAR_COOKED}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RACER_COOKED_DIR}/vehicles
        COMMAND import_check --cook ${CAR_GLB} ${CAR_COOKED}
        DEPENDS import_check ${CAR_GLB}
        COMMENT "Cooking ${CAR} vehicle model"
        VERBATIM
    )
    list(APPEND COOKED_VEHICLES ${CAR_COOKED})
endforeach()
set(S01_GLB ${PROJECT_SOURCE_DIR}/assets/tracks/dock_loop/dock_loop_greybox.glb)
set(S01_COOKED ${RACER_COOKED_DIR}/tracks/S01.wiscene)
add_custom_command(
    OUTPUT ${S01_COOKED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RACER_COOKED_DIR}/tracks
    COMMAND import_check --cook ${S01_GLB} ${S01_COOKED}
    DEPENDS import_check ${S01_GLB}
    COMMENT "Cooking S01 Dock Loop visuals"
    VERBATIM
)
set(L01_GLB ${PROJECT_SOURCE_DIR}/assets/tracks/container_run/container_run_greybox.glb)
set(L01_COOKED ${RACER_COOKED_DIR}/tracks/L01.wiscene)
add_custom_command(
    OUTPUT ${L01_COOKED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RACER_COOKED_DIR}/tracks
    COMMAND import_check --cook ${L01_GLB} ${L01_COOKED}
    DEPENDS import_check ${L01_GLB}
    COMMENT "Cooking L01 Container Run visuals"
    VERBATIM
)
# Toon sky kit (D-057): clouds, sun, moon, stars.
set(SKY_GLB ${PROJECT_SOURCE_DIR}/assets/environment/sky/sky_kit.glb)
set(SKY_COOKED ${RACER_COOKED_DIR}/sky/sky_kit.wiscene)
add_custom_command(
    OUTPUT ${SKY_COOKED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RACER_COOKED_DIR}/sky
    COMMAND import_check --cook ${SKY_GLB} ${SKY_COOKED}
    DEPENDS import_check ${SKY_GLB}
    COMMENT "Cooking the toon sky kit"
    VERBATIM
)
# The driver character, shared by every car (D-076).
set(DRIVER_GLB ${PROJECT_SOURCE_DIR}/assets/characters/driver/driver.glb)
set(DRIVER_COOKED ${RACER_COOKED_DIR}/characters/driver.wiscene)
add_custom_command(
    OUTPUT ${DRIVER_COOKED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RACER_COOKED_DIR}/characters
    COMMAND import_check --cook ${DRIVER_GLB} ${DRIVER_COOKED}
    DEPENDS import_check ${DRIVER_GLB}
    COMMENT "Cooking the driver character"
    VERBATIM
)
add_custom_target(cook_content DEPENDS ${COOKED_VEHICLES} ${S01_COOKED} ${L01_COOKED} ${SKY_COOKED} ${DRIVER_COOKED})

add_executable(racer_sandbox WIN32 main.cpp SandboxPath.cpp SandboxPath.h RacePath.cpp RacePath.h MenuPath.cpp MenuPath.h CarVisual.cpp CarVisual.h RaceCarFeatures.cpp)
target_link_libraries(racer_sandbox PRIVATE racer_runtime racer_audio nlohmann_json)
target_compile_definitions(racer_sandbox PRIVATE
    RACER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content"
    RACER_COOKED_DIR="${RACER_COOKED_DIR}"
    RACER_AUDIO_DIR="${PROJECT_SOURCE_DIR}/assets/audio/game")
add_dependencies(racer_sandbox cook_content)

# Runtime shader compilation needs dxcompiler next to the executable.
add_custom_command(TARGET racer_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PROJECT_SOURCE_DIR}/engine/WickedEngine/WickedEngine/dxcompiler.dll
        $<TARGET_FILE_DIR:racer_sandbox>)

# Performance gate (D-092, owner: "keep an eye on optimisation"): worst-case scenes on every ctest
# run; the race fails (exit 1) when the mean frame, the 99th percentile frame or the RAM goes past
# the budget. Measured when set: mean 6.1-6.7 ms, p99 8-9.7 ms, RAM ~1.5 GB.
set(PERF_BUDGET "--perf-budget=8.5,14,2400")
add_test(NAME perf_S01_storm_cockpit
    COMMAND racer_sandbox --autotest=20 --car=D02 --track=S01 --time=night --weather=storm --features=cockpit --field-offset=0 ${PERF_BUDGET}
            --report=${CMAKE_BINARY_DIR}/perf_S01_storm.json)
add_test(NAME perf_L01_20cars_rain
    COMMAND racer_sandbox --autotest=20 --car=D05 --track=L01 --time=night --weather=rain --opponents=19 --field-offset=0 ${PERF_BUDGET}
            --report=${CMAKE_BINARY_DIR}/perf_L01_rain20.json)
add_test(NAME perf_S01_day_chase
    COMMAND racer_sandbox --autotest=20 --car=D01 --track=S01 --time=day --field-offset=0 ${PERF_BUDGET}
            --report=${CMAKE_BINARY_DIR}/perf_S01_day.json)
