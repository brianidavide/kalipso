cmake_minimum_required(VERSION 3.22)
# Must precede project(): the oldest macOS the app and its helper run on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
project(Kalipso VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
set(KALIPSO_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

# --- Script tracking: Qt Core only ---------------------------------------------
add_library(prompter_core STATIC src/tracker.cpp)
target_include_directories(prompter_core PUBLIC src)
target_link_libraries(prompter_core PUBLIC Qt6::Core)
target_compile_options(prompter_core PRIVATE ${KALIPSO_WARNINGS})

# --- Speech recognition ---------------------------------------------------------
add_library(kalipso_speech STATIC src/speech.cpp src/moonshine.cpp src/system_speech/client.cpp)
target_link_libraries(kalipso_speech PUBLIC prompter_core Qt6::Core)
target_compile_options(kalipso_speech PRIVATE ${KALIPSO_WARNINGS})
if(APPLE OR WIN32)
  # Linux captures through PipeWire's pw-record; macOS and Windows use Qt Multimedia.
  if(APPLE)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Multimedia)
  else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia)
  endif()
  target_link_libraries(kalipso_speech PUBLIC Qt6::Multimedia)
  target_compile_definitions(kalipso_speech PUBLIC KALIPSO_QT_AUDIO=1)
endif()

# --- Linux companion for the Plasma widget and GNOME extension ------------------
if(UNIX AND NOT APPLE)
  find_package(Qt6 REQUIRED COMPONENTS DBus Gui)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
  add_library(kalipso_colors STATIC src/screen_colors/portal.cpp)
  target_include_directories(kalipso_colors PUBLIC src)
  target_link_libraries(kalipso_colors PUBLIC Qt6::Gui Qt6::DBus PRIVATE PkgConfig::PIPEWIRE)
  target_compile_definitions(kalipso_colors PUBLIC KALIPSO_PORTAL_COLORS=1)

  add_executable(kalipso-engine src/engine/main.cpp)
  target_link_libraries(kalipso-engine PRIVATE kalipso_speech kalipso_colors Qt6::DBus)
  target_compile_options(kalipso-engine PRIVATE ${KALIPSO_WARNINGS})
  install(TARGETS kalipso-engine RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Engine)
  configure_file(packaging/io.github.brianidavide.Kalipso.Engine.service.in
    io.github.brianidavide.Kalipso.Engine.service @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.brianidavide.Kalipso.Engine.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services COMPONENT Engine)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kalipso-engine COMPONENT Engine)
endif()

# --- Standalone application -----------------------------------------------------
add_library(kalipso_vendor STATIC third_party/puff/puff.c third_party/qrcodegen/qrcodegen.cpp)
add_library(kalipso_app STATIC
  src/window.cpp src/reader.cpp src/backdrop.cpp src/library.cpp src/import.cpp src/remote.cpp
  src/script_highlighter.cpp src/platform/capture_exclusion.cpp)
target_link_libraries(kalipso_app PUBLIC kalipso_speech Qt6::Widgets Qt6::Network PRIVATE kalipso_vendor)
target_compile_definitions(kalipso_app PUBLIC KALIPSO_VERSION="${PROJECT_VERSION}")
target_compile_options(kalipso_app PRIVATE ${KALIPSO_WARNINGS})
if(TARGET kalipso_colors)
  target_link_libraries(kalipso_app PUBLIC kalipso_colors)
endif()
if(APPLE)
  enable_language(OBJCXX)
  target_sources(kalipso_app PRIVATE src/platform/capture_exclusion_mac.mm)
  target_link_libraries(kalipso_app PUBLIC "-framework AppKit")
elseif(WIN32)
  target_link_libraries(kalipso_app PUBLIC gdi32 user32)
endif()

add_executable(kalipso src/main.cpp)
target_link_libraries(kalipso PRIVATE kalipso_app)
target_compile_options(kalipso PRIVATE ${KALIPSO_WARNINGS})
file(GLOB KALIPSO_TRANSLATIONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translations/kalipso_*.ts)
if(Qt6LinguistTools_FOUND AND KALIPSO_TRANSLATIONS)
  qt_add_translations(kalipso TS_FILES ${KALIPSO_TRANSLATIONS} RESOURCE_PREFIX /i18n)
endif()

if(APPLE)
  set_target_properties(kalipso PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  find_program(SWIFTC swiftc REQUIRED)
  if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 KALIPSO_SWIFT_ARCH)
  else()
    set(KALIPSO_SWIFT_ARCH ${CMAKE_HOST_SYSTEM_PROCESSOR})
  endif()
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech"
    COMMAND ${SWIFTC} -parse-as-library -swift-version 5 -O
      -target ${KALIPSO_SWIFT_ARCH}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/system_speech/macos.swift" -o "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech"
    DEPENDS src/system_speech/macos.swift VERBATIM)
  add_custom_target(system_speech_helper ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech")
  add_dependencies(kalipso system_speech_helper)
  add_custom_command(TARGET kalipso POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:kalipso>/Contents/Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech" "$<TARGET_BUNDLE_DIR:kalipso>/Contents/Helpers/")
  install(TARGETS kalipso BUNDLE DESTINATION .)
elseif(WIN32)
  set_target_properties(kalipso PROPERTIES WIN32_EXECUTABLE TRUE)
  add_executable(kalipso-system-speech src/system_speech/windows.cpp)
  target_link_libraries(kalipso-system-speech PRIVATE Qt6::Core sapi ole32)
  target_compile_definitions(kalipso-system-speech PRIVATE NOMINMAX UNICODE _UNICODE)
  install(TARGETS kalipso kalipso-system-speech RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS kalipso RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/io.github.brianidavide.kalipso.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/io.github.brianidavide.kalipso.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES packaging/io.github.brianidavide.kalipso.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kalipso)

# --- Tests ------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  find_program(DBUS_RUN_SESSION dbus-run-session)
  find_program(GDBUS_EXECUTABLE gdbus)

  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE prompter_core Qt6::Test)
  add_test(NAME core COMMAND core_tests)

  add_executable(library_tests tests/library_tests.cpp)
  target_link_libraries(library_tests PRIVATE kalipso_app Qt6::Test)
  add_test(NAME library COMMAND library_tests)
  set_tests_properties(library PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_library(fake_moonshine SHARED tests/fake_moonshine.cpp)
  set_target_properties(fake_moonshine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  add_executable(speech_tests tests/speech_tests.cpp)
  target_link_libraries(speech_tests PRIVATE kalipso_speech Qt6::Test)
  target_compile_definitions(speech_tests PRIVATE FAKE_MOONSHINE_LIBRARY="$<TARGET_FILE:fake_moonshine>")
  add_dependencies(speech_tests fake_moonshine)
  add_test(NAME speech COMMAND speech_tests)

  add_executable(fake_system_speech tests/fake_system_speech.cpp)
  target_link_libraries(fake_system_speech PRIVATE Qt6::Core)
  add_executable(system_speech_tests tests/system_speech_tests.cpp)
  target_link_libraries(system_speech_tests PRIVATE kalipso_speech Qt6::Test)
  target_compile_definitions(system_speech_tests PRIVATE FAKE_SYSTEM_SPEECH="$<TARGET_FILE:fake_system_speech>")
  add_dependencies(system_speech_tests fake_system_speech)
  add_test(NAME system_speech COMMAND system_speech_tests)

  add_executable(ui_tests tests/ui_tests.cpp)
  target_link_libraries(ui_tests PRIVATE kalipso_app Qt6::Test)
  add_test(NAME ui COMMAND ui_tests -o ui-results.txt,txt -o -,txt)
  if(WIN32)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
  else()
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()

  add_executable(speech_probe benchmarks/speech_probe.cpp)
  target_link_libraries(speech_probe PRIVATE kalipso_speech)

  if(Python3_Interpreter_FOUND)
    add_test(NAME speech_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/speech_assets_test.py)
    if(APPLE)
      add_test(NAME system_helper_probe COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_helper_smoke.py "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech")
    elseif(WIN32)
      add_test(NAME system_helper_probe COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_helper_smoke.py $<TARGET_FILE:kalipso-system-speech>)
    endif()
  endif()

  if(TARGET kalipso-engine AND Python3_Interpreter_FOUND AND DBUS_RUN_SESSION AND GDBUS_EXECUTABLE)
    add_test(NAME engine_dependencies COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_dependencies.py $<TARGET_FILE:kalipso-engine>)
  endif()

  if(TARGET kalipso_colors AND DBUS_RUN_SESSION)
    add_executable(portal_colors_tests tests/portal_colors_tests.cpp)
    target_link_libraries(portal_colors_tests PRIVATE kalipso_colors Qt6::Test)
    target_compile_definitions(portal_colors_tests PRIVATE ENGINE_BINARY="$<TARGET_FILE:kalipso-engine>")
    add_dependencies(portal_colors_tests kalipso-engine)
    add_test(NAME portal_colors COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:portal_colors_tests>)
    # Real PipeWire frames through a simulated consent portal, when the tools exist.
    find_program(GST_LAUNCH gst-launch-1.0)
    find_program(GST_INSPECT gst-inspect-1.0)
    find_program(PIPEWIRE_SERVER pipewire)
    find_program(WIREPLUMBER wireplumber)
    if(GST_INSPECT)
      execute_process(COMMAND ${GST_INSPECT} pipewiresink RESULT_VARIABLE HAVE_PIPEWIRE_SINK OUTPUT_QUIET ERROR_QUIET)
      execute_process(COMMAND ${GST_INSPECT} videotestsrc RESULT_VARIABLE HAVE_VIDEO_TEST_SOURCE OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(GST_LAUNCH AND HAVE_PIPEWIRE_SINK EQUAL 0 AND HAVE_VIDEO_TEST_SOURCE EQUAL 0 AND PIPEWIRE_SERVER
       AND WIREPLUMBER AND Python3_Interpreter_FOUND)
      add_test(NAME portal_colors_stream COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_colors_stream.py $<TARGET_FILE:portal_colors_tests>)
      set_tests_properties(portal_colors_stream PROPERTIES TIMEOUT 60)
    endif()
  endif()
endif()

option(BUILD_RENDER_BENCHMARKS "Build equivalent Widgets/Quick render probes" OFF)
if(BUILD_RENDER_BENCHMARKS)
  find_package(Qt6 REQUIRED COMPONENTS Quick)
  add_executable(render_probe benchmarks/render_probe.cpp)
  target_link_libraries(render_probe PRIVATE Qt6::Widgets Qt6::Quick)
endif()
