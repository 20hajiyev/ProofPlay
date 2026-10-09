add_library(miniaudio STATIC miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json/single_include)
