set(LIBRARIES_DIR	    ${CMAKE_SOURCE_DIR}/libs)
set(EmbeddedIOServicesHardwareAbstraction_Path ${LIBRARIES_DIR}/EmbeddedIOServices/HardwareAbstractions/Esp32Idf)

idf_component_register( SRCS "JustAFile.cpp"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES driver esp_http_server esp_driver_gpio esp_driver_gptimer esp_adc)

include(FetchContent)

FetchContent_Declare(
    EmbeddedIOServices
    GIT_REPOSITORY https://github.com/FL0WL0W/EmbeddedIOServices
    GIT_TAG        main
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
    SOURCE_DIR  ${LIBRARIES_DIR}/EmbeddedIOServices
)

FetchContent_MakeAvailable(EmbeddedIOServices)

target_link_libraries(${COMPONENT_LIB} PUBLIC EmbeddedIOServices)

file(GLOB_RECURSE EmbeddedIOServiceHardwareAbstraction_SRCS ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*.cpp)
file(GLOB_RECURSE EmbeddedIOServiceHardwareAbstraction_SRCS_Templates ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*_template.cpp)
IF(EmbeddedIOServiceHardwareAbstraction_SRCS_Templates)
        list(REMOVE_ITEM EmbeddedIOServiceHardwareAbstraction_SRCS ${EmbeddedIOServiceHardwareAbstraction_SRCS_Templates})
ENDIF()

target_sources(${COMPONENT_LIB} PRIVATE "${EmbeddedIOServiceHardwareAbstraction_SRCS}")
target_include_directories(${COMPONENT_LIB} PUBLIC "${EmbeddedIOServicesHardwareAbstraction_Path}/include")