# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(TESTCONNECTION_SOURCES
	TestConnection.cpp

	"${CMAKE_SOURCE_DIR}/src/Connection.cpp"
	"${CMAKE_SOURCE_DIR}/src/Connection.h"
)

add_executable(TestConnection ${TESTCONNECTION_SOURCES})

set_target_properties(TestConnection PROPERTIES AUTOMOC ON)

target_link_libraries(TestConnection PRIVATE shared Qt6::Test)

add_test(NAME TestConnection COMMAND $<TARGET_FILE:TestConnection>)
