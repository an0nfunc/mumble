# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTFECLOOKAHEAD_SOURCES
	TestFecLookahead.cpp

	"${MUMBLE_SOURCE_DIR}/FecLookahead.cpp"
	"${MUMBLE_SOURCE_DIR}/FecLookahead.h"
)

add_executable(TestFecLookahead ${TESTFECLOOKAHEAD_SOURCES})

set_target_properties(TestFecLookahead PROPERTIES AUTOMOC ON)

target_include_directories(TestFecLookahead PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestFecLookahead PRIVATE shared mumble_client_audio_libs Qt6::Test)

add_test(NAME TestFecLookahead COMMAND $<TARGET_FILE:TestFecLookahead>)

if(WIN32 AND bundled-speex)
	# The bundled speexdsp DLL is placed next to the client executable, not next to the tests
	set_tests_properties(TestFecLookahead PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:speexdsp>")
endif()
