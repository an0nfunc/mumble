# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTUDPPATHMONITOR_SOURCES
	TestUdpPathMonitor.cpp

	"${MUMBLE_SOURCE_DIR}/UdpPathMonitor.cpp"
	"${MUMBLE_SOURCE_DIR}/UdpPathMonitor.h"
)

add_executable(TestUdpPathMonitor ${TESTUDPPATHMONITOR_SOURCES})

set_target_properties(TestUdpPathMonitor PROPERTIES AUTOMOC ON)

target_include_directories(TestUdpPathMonitor PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestUdpPathMonitor PRIVATE Qt6::Test)

add_test(NAME TestUdpPathMonitor COMMAND $<TARGET_FILE:TestUdpPathMonitor>)
