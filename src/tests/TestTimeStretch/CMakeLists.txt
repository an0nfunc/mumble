# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTTIMESTRETCH_SOURCES
	TestTimeStretch.cpp

	"${MUMBLE_SOURCE_DIR}/TimeStretch.cpp"
	"${MUMBLE_SOURCE_DIR}/TimeStretch.h"
)

add_executable(TestTimeStretch ${TESTTIMESTRETCH_SOURCES})

set_target_properties(TestTimeStretch PROPERTIES AUTOMOC ON)

target_include_directories(TestTimeStretch PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestTimeStretch PRIVATE Qt6::Test)

add_test(NAME TestTimeStretch COMMAND $<TARGET_FILE:TestTimeStretch>)
