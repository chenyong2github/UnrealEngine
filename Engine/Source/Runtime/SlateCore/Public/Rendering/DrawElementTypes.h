// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Slate higher level abstract element types
 */
enum class EElementType : uint8
{
	ET_Box,
	ET_DebugQuad,
	ET_Text,
	ET_ShapedText,
	ET_Spline,
	ET_Line,
	ET_Gradient,
	ET_Viewport,
	ET_Border,
	ET_Custom,
	ET_CustomVerts,
	ET_PostProcessPass,
	ET_RoundedBox,
	/**
	 * We map draw elements by type on add for better cache coherency if possible,
	 * this type is used when that grouping is not possible.
	 * Grouping is also planned to be used for bulk element type processing.
	 */
	ET_NonMapped,
	/** Total number of draw commands */
	ET_Count,
};