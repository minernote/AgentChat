cmake_minimum_required(VERSION 3.16)
project(AgentChat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -- Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -- Dependencies

find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)

# SQLite3
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 REQUIRED sqlite3)
        add_library(SQLite::SQLite3 INTERFACE IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES      "${SQLITE3_LIBRARIES}")
    else()
        find_library(SQLITE3_LIB NAMES sqlite3 REQUIRED)
        find_path(SQLITE3_INC NAMES sqlite3.h REQUIRED)
        add_library(SQLite::SQLite3 INTERFACE IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INC}"
            INTERFACE_LINK_LIBRARIES      "${SQLITE3_LIB}")
    endif()
endif()

# -- Core library

add_library(agentchat_core
    src/core/crypto/crypto.cpp
    src/core/crypto/ratchet.cpp
    src/core/crypto/x3dh.cpp
    src/core/crypto/group_ratchet.cpp
    src/core/protocol/protocol.cpp
    src/core/storage/storage.cpp
    src/core/network/mdns.cpp
)

target_include_directories(agentchat_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agentchat_core
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        SQLite::SQLite3
)

# -- Agent SDK

add_library(agentchat_agent STATIC
    src/agent/agent_client.cpp
)

target_include_directories(agentchat_agent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/agent>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agentchat_agent
    PUBLIC agentchat_core
)

# -- C ABI shared library (for Python / Node.js / any FFI)

add_library(agentchat_c_api SHARED
    bindings/python/agentchat_c.cpp
)

target_include_directories(agentchat_c_api
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/agent
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python
)

target_link_libraries(agentchat_c_api
    PRIVATE agentchat_agent agentchat_core
)

# Make symbols visible on all platforms
set_target_properties(agentchat_c_api PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)

# -- Server executable

add_executable(agentchat_server
    src/server/main.cpp
)

target_include_directories(agentchat_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    /opt/homebrew/include
)

target_link_libraries(agentchat_server
    PRIVATE agentchat_core agentchat_agent
)

# -- CLI client executable

add_executable(agentchat_client
    src/client/main.cpp
)

target_include_directories(agentchat_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agentchat_client
    PRIVATE agentchat_core agentchat_agent
)

# -- Tests

enable_testing()

add_executable(test_crypto   tests/test_crypto.cpp)
add_executable(test_protocol tests/test_protocol.cpp)

foreach(t test_crypto test_protocol)
    target_include_directories(${t} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(${t} PRIVATE agentchat_core)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# -- Storage tests

add_executable(test_storage
    tests/test_storage.cpp
    src/core/storage/storage.cpp
)
target_include_directories(test_storage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_storage PRIVATE agentchat_core)
add_test(NAME test_storage COMMAND test_storage)

# -- ACL tests

add_executable(test_acl
    tests/test_acl.cpp
    src/server/acl.cpp
)
target_include_directories(test_acl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_link_libraries(test_acl PRIVATE agentchat_core)
add_test(NAME test_acl COMMAND test_acl)

# -- Integration tests

add_executable(test_integration
    tests/test_integration.cpp
)
target_include_directories(test_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/agent
)
target_link_libraries(test_integration PRIVATE agentchat_agent agentchat_core)
add_test(NAME test_integration
    COMMAND test_integration $<TARGET_FILE:agentchat_server>)

# -- E2EE tests
add_executable(test_e2ee tests/test_e2ee.cpp)
target_include_directories(test_e2ee PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_e2ee PRIVATE agentchat_core)
add_test(NAME test_e2ee COMMAND test_e2ee)

# -- mDNS tests

add_executable(test_mdns
    tests/test_mdns.cpp
    src/core/network/mdns.cpp
)
target_include_directories(test_mdns PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_mdns PRIVATE agentchat_core)
add_test(NAME test_mdns COMMAND test_mdns)

# -- Install rules

include(GNUInstallDirs)

install(TARGETS agentchat_c_api
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES bindings/python/agentchat.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS agentchat_server agentchat_client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
