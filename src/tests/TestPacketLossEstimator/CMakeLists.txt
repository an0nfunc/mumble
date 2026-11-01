# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTPACKETLOSSESTIMATOR_SOURCES
	TestPacketLossEstimator.cpp

	"${MUMBLE_SOURCE_DIR}/PacketLossEstimator.cpp"
	"${MUMBLE_SOURCE_DIR}/PacketLossEstimator.h"
)

add_executable(TestPacketLossEstimator ${TESTPACKETLOSSESTIMATOR_SOURCES})

set_target_properties(TestPacketLossEstimator PROPERTIES AUTOMOC ON)

target_include_directories(TestPacketLossEstimator PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestPacketLossEstimator PRIVATE Qt6::Test)

add_test(NAME TestPacketLossEstimator COMMAND $<TARGET_FILE:TestPacketLossEstimator>)
