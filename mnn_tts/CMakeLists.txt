cmake_minimum_required(VERSION 3.13)

project(mnn_tts VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_BERTVITS2 "Build BertVit2 TTS " ON)
option(BUILD_PIPER "Build PIPER TTS " OFF)
option(BUILD_SUPERTONIC "Build Supertonic TTS " ON)
option(BUILD_ANDROID "Build for Android" OFF)

if(ANDROID)
    set(BUILD_ANDROID ON CACHE BOOL "Build for Android" FORCE)
endif()

add_library(MNN
        SHARED
        IMPORTED)

#set(MNN_SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../)

# project root path
set(MNN_SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../mnn)

#set(MNN_INSTALL_ROOT "${MNN_SOURCE_ROOT}/project/android/build_64")
#set(LIB_PATH "${MNN_INSTALL_ROOT}/lib")

set(LIB_PATH "${MNN_SOURCE_ROOT}/lib/${ANDROID_ABI}")
if(NOT EXISTS "${LIB_PATH}/libMNN.so")
    set(LIB_PATH "${MNN_SOURCE_ROOT}/lib")
endif()

set(MNN_TTS_SOURCE_PATH ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp)

set(MNN_EXPRESS_PATH "${LIB_PATH}/libMNN_Express.so")
message(STATUS "MNN_SOURCE_ROOT: ${MNN_SOURCE_ROOT}")
message(STATUS "LIB_PATH: ${LIB_PATH}")

set_target_properties(MNN PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
        "${MNN_SOURCE_ROOT}/include;${MNN_SOURCE_ROOT}/tools/audio/include/;${MNN_SOURCE_ROOT}/transformers/llm/engine/include"
)
set_target_properties(MNN PROPERTIES IMPORTED_LOCATION "${LIB_PATH}/libMNN.so")

if(EXISTS "${MNN_EXPRESS_PATH}")
  add_library(MNNExpress SHARED IMPORTED)
  set_target_properties(MNNExpress PROPERTIES IMPORTED_LOCATION "${MNN_EXPRESS_PATH}")
  message(STATUS "Linking legacy split runtime: ${MNN_EXPRESS_PATH}")
endif()

include_directories(
        ${MNN_TTS_SOURCE_PATH}/include
        ${MNN_TTS_SOURCE_PATH}/3rd_party/include
)

if(BUILD_BERTVITS2)
    include_directories(
            ${MNN_TTS_SOURCE_PATH}/include/bertvits2
    )
endif()

if(BUILD_PIPER)
  include_directories(
    ${MNN_TTS_SOURCE_PATH}/include/piper
    ${CMAKE_CURRENT_LIST_DIR}/third_party/piper/espeak-ng/src/include/espeak-ng/
  )
endif()

if(BUILD_SUPERTONIC)
  include_directories(
    ${MNN_TTS_SOURCE_PATH}/include/supertonic
  )
endif()

set(SHARED_SOURCE_FILES
  ${MNN_TTS_SOURCE_PATH}/src/mnn_tts_config.cpp
  ${MNN_TTS_SOURCE_PATH}/src/mnn_tts_sdk.cpp
  ${MNN_TTS_SOURCE_PATH}/src/mnn_tts_logger.cpp
)

set(BERTVITS2_SOURCE_FILES
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/an_to_cn.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/utils.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/mnn_bertvits2_tts_impl.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/text_preprocessor.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/pinyin.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/chinese_g2p.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/chinese_bert.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/english_bert.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/english_g2p.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/tone_adjuster.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/tts_generator.cpp
  ${MNN_TTS_SOURCE_PATH}/src/bertvits2/word_spliter.cpp
)

if(BUILD_PIPER)
  set(PIPER_SOURCE_FILES
    ${MNN_TTS_SOURCE_PATH}/src/piper/audio_generator.cpp
    ${MNN_TTS_SOURCE_PATH}/src/piper/mnn_piper_tts_impl.cpp
    ${MNN_TTS_SOURCE_PATH}/src/piper/phoneme_ids.cpp
  )
endif()

if(BUILD_SUPERTONIC)
  set(SUPERTONIC_SOURCE_FILES
    ${MNN_TTS_SOURCE_PATH}/src/supertonic/mnn_supertonic_tts_impl.cpp
  )
endif()

if(BUILD_ANDROID)
  set(ANDROID_SOURCE_FILES
    ${MNN_TTS_SOURCE_PATH}/src/android/tts_service.cpp
    ${MNN_TTS_SOURCE_PATH}/src/android/tts_service_jni.cpp
  )
endif()

if(BUILD_PIPER)
  add_subdirectory(third_party/piper/espeak-ng)
endif()

add_library(${PROJECT_NAME} SHARED ${PIPER_SOURCE_FILES} ${BERTVITS2_SOURCE_FILES} ${SUPERTONIC_SOURCE_FILES} ${SHARED_SOURCE_FILES} ${ANDROID_SOURCE_FILES})

# Add 16KB page size support for Android
if(BUILD_ANDROID)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
endif()

if(BUILD_ANDROID)
    if(TARGET MNNExpress)
        target_link_libraries(${PROJECT_NAME} log MNN MNNExpress)
    else()
        target_link_libraries(${PROJECT_NAME} log MNN)
    endif()
else()
    if(TARGET MNNExpress)
        target_link_libraries(${PROJECT_NAME} MNN MNNExpress)
    else()
        target_link_libraries(${PROJECT_NAME} MNN)
    endif()
endif() 
