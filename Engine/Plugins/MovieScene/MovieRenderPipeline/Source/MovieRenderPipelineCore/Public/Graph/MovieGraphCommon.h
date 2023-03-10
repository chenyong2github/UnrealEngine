// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieGraphCommon.generated.h"

/** Types of members that can be used in the graph. */
UENUM()
enum class EMovieGraphMemberType : uint8
{
	Branch,
	Bool,
	Float,
	Int,
	IntPoint,
	String
};