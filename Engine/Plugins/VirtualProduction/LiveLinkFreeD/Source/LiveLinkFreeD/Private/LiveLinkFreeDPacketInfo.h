// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

//
// Free-D protocol packet data definitions as per the official spec:
// https://www.manualsdir.com/manuals/641433/vinten-radamec-free-d.html
//
struct FreeDPacketDefinition
{
	static constexpr uint8 PacketTypeD1 = 0xD1;
	static constexpr uint8 PacketSizeD1 = 0x1D;

	static constexpr uint8 PacketType = 0x00;
	static constexpr uint8 CameraID = 0x01;
	static constexpr uint8 Yaw = 0x02;
	static constexpr uint8 Pitch = 0x05;
	static constexpr uint8 Roll = 0x08;
	static constexpr uint8 X = 0x0B;
	static constexpr uint8 Y = 0x0E;
	static constexpr uint8 Z = 0x11;
	static constexpr uint8 FocalLength = 0x14;
	static constexpr uint8 FocusDistance = 0x17;
	static constexpr uint8 UserDefined = 0x1A;
	static constexpr uint8 Checksum = 0x1C;
};
