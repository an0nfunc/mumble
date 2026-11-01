# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTPLAYOUTCATCHUP_SOURCES
	TestPlayoutCatchUp.cpp

	"${MUMBLE_SOURCE_DIR}/PlayoutCatchUp.h"
)

add_executable(TestPlayoutCatchUp ${TESTPLAYOUTCATCHUP_SOURCES})

set_target_properties(TestPlayoutCatchUp PROPERTIES AUTOMOC ON)

target_include_directories(TestPlayoutCatchUp PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestPlayoutCatchUp PRIVATE Qt6::Test)

add_test(NAME TestPlayoutCatchUp COMMAND $<TARGET_FILE:TestPlayoutCatchUp>)
