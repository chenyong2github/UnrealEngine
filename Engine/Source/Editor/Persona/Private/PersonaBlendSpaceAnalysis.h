// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "BlendSpaceAnalysis.h"

class FBlendSpaceAnalysis
{
public:
	static FVector CalculateSampleValue(const UBlendSpace&   BlendSpace, 
										const UAnimSequence& Animation, 
										const float          RateScale, 
										const FVector&       OriginalPosition, 
										bool                 bAnalyzed[3]);


	// This will return an instance derived from UAnalysisProperties that is suitable for the Function. The caller will
	// pass in a suitable owning object, outer, that the implementation should assign as owner of the newly created object.
	static UAnalysisProperties* MakeAnalysisProperties(UObject* Outer, const FString& FunctionName);

	// This will return the names of the functions handled
	static TArray<FString> GetAnalysisFunctions();
};
