// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFUVAnalysis
{
	FGLTFUVAnalysis(float OverlapPercentage)
		: OverlapPercentage(OverlapPercentage)
	{
	}

	const float OverlapPercentage;
};
