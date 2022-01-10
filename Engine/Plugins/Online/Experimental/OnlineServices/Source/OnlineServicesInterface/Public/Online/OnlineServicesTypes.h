// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE::Online {

enum class EOnlineServices : uint8
{
	// Null, Providing minimal functionality when no backend services are required
	Null,
	// Epic Online Services
	Epic,
	// Xbox services
	Xbox,
	// PlayStation Network
	PSN,
	// Nintendo
	Nintendo,
	// Stadia,
	Stadia,
	// Steam
	Steam,
	// Google
	Google,
	// GooglePlay
	GooglePlay,
	// Apple
	Apple,
	// GameKit
	AppleGameKit,
	// Samsung
	Samsung,
	// Oculus
	Oculus,
	// Tencent
	Tencent,
	// Reserved for future use/platform extensions
	Reserved_14,
	Reserved_15,
	Reserved_16,
	Reserved_17,
	Reserved_18,
	Reserved_19,
	Reserved_20,
	Reserved_21,
	Reserved_22,
	Reserved_23,
	Reserved_24,
	Reserved_25,
	Reserved_26,
	Reserved_27,
	// For game specific Online Services
	GameDefined_0 = 28,
	GameDefined_1,
	GameDefined_2,
	GameDefined_3,
	// Platform native, may not exist for all platforms
	Platform = 254,
	// Default, configured via ini, TODO: List specific ini section/key
	Default = 255
};

/* UE::Online */ }
