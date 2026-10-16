set(TEST_SOURCES
  test_main.c
  test_stubs.c
  test_ini_doc.c
  ${PROJECT_SOURCE_DIR}/src/ini_doc.c
  ${PROJECT_SOURCE_DIR}/src/util.c
  ${PROJECT_SOURCE_DIR}/src/debug.c
)
set(TEST_SUITES ini_doc)

add_executable(reflex-tests ${TEST_SOURCES})
target_include_directories(reflex-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tmp)
target_compile_definitions(reflex-tests PRIVATE
  TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
  SAMPLE_CONFIG="${PROJECT_BINARY_DIR}/config.ini"
)
target_link_libraries(reflex-tests platform)
if (UNIX)
  target_link_libraries(reflex-tests PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_TTF PkgConfig::INIH m)
else ()
  target_link_libraries(reflex-tests
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    ${INIH} ${GETOPT} PowrProf
  )
  target_include_directories(reflex-tests PRIVATE ${INIH_INCLUDE_DIR} ${GETOPT_INCLUDE_DIR})
endif ()

foreach (suite ${TEST_SUITES})
  add_test(NAME ${suite} COMMAND reflex-tests ${suite})
endforeach ()
