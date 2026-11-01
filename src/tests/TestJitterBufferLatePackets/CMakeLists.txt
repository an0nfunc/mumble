# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

add_executable(TestJitterBufferLatePackets TestJitterBufferLatePackets.cpp)

set_target_properties(TestJitterBufferLatePackets PROPERTIES AUTOMOC ON)

target_include_directories(TestJitterBufferLatePackets PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestJitterBufferLatePackets PRIVATE mumble_client_audio_libs Qt6::Test)

add_test(NAME TestJitterBufferLatePackets COMMAND $<TARGET_FILE:TestJitterBufferLatePackets>)

if(WIN32 AND bundled-speex)
	# The bundled speexdsp DLL is placed next to the client executable, not next to the tests
	set_tests_properties(TestJitterBufferLatePackets PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:speexdsp>")
endif()
