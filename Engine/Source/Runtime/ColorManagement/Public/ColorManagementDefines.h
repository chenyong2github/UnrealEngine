// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE { namespace Color {

/** List of available encodings/transfer functions.
* 
* IMPORTANT NOTE: This list is replicated as a UENUM in TextureDefines.h, and both should always match.
* Furthermore, make sure to increment TEXTURE_ENCODING_TYPES_VER in TextureDerivedData.cpp upon changes to this list.
*/
enum class EEncoding : uint8 {
	None = 0,
	Linear = 1,
	sRGB,
	ST2084,
	Gamma22,
	BT1886,
	Cineon,
	REDLog,
	REDLog3G10,
	SLog1,
	SLog2,
	SLog3,
	AlexaV3LogC,
	CanonLog,
	ProTune,
	VLog,
	Max,
};

} } // end namespace UE::Color
