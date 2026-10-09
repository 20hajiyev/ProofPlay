set(WICKED_EDITOR_DIR ${PROJECT_SOURCE_DIR}/engine/WickedEngine/Editor)

# The importer includes "stdafx.h" by quotes, which would resolve to the Editor's
# precompiled header (pulls in the whole editor). Copy it next to our shim instead.
configure_file(${WICKED_EDITOR_DIR}/ModelImporter_GLTF.cpp ${CMAKE_CURRENT_BINARY_DIR}/importer/ModelImporter_GLTF.cpp COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stdafx_shim.h ${CMAKE_CURRENT_BINARY_DIR}/importer/stdafx.h COPYONLY)

add_executable(import_check
    main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/importer/ModelImporter_GLTF.cpp
)
target_include_directories(import_check PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/importer
    ${WICKED_EDITOR_DIR}
    ${PROJECT_SOURCE_DIR}/engine/WickedEngine/WickedEngine
)
target_link_libraries(import_check PRIVATE WickedEngine)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME fixture_axes
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_fixture_axes.py $<TARGET_FILE:import_check>)
add_test(NAME import_leak_D01
    COMMAND import_check --leak-test 60 ${PROJECT_SOURCE_DIR}/assets/vehicles_toon/D01/D01.glb)
