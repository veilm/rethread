cmake_minimum_required(VERSION 3.21)

project(rethread LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets WebChannel)

set(RETHREAD_BROWSER_SOURCES
    src/main.cpp
    src/app/app.cc
    src/app/tab_cli.cc
    src/app/user_dirs.cc
    src/common/debug_log.cc
    src/common/theme.cc
    src/browser/command_dispatcher.cc
    src/browser/context_menu_binding_manager.cc
    src/browser/js_eval_bridge.cc
    src/browser/rules_manager.cc
    src/browser/rules_request_interceptor.cc
    src/browser/script_manager.cc
    src/browser/key_binding_manager.cc
    src/browser/main_window.cc
    src/browser/tab_ipc_server.cc
    src/browser/tab_manager.cc
    src/browser/tab_strip_controller.cc
    src/browser/tab_strip_overlay.cc
    src/browser/web_page.cc
    src/browser/web_view.cc)

add_executable(browser ${RETHREAD_BROWSER_SOURCES})
target_include_directories(browser PRIVATE src)
target_link_libraries(browser PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
set_target_properties(browser PROPERTIES OUTPUT_NAME rethread-browser)

add_executable(cli
    src/app/rethread_cli.cc
    src/app/tab_cli.cc
    src/app/user_dirs.cc)
target_include_directories(cli PRIVATE src)
target_link_libraries(cli PRIVATE Qt6::Core)
set_target_properties(cli PROPERTIES OUTPUT_NAME rethread)
