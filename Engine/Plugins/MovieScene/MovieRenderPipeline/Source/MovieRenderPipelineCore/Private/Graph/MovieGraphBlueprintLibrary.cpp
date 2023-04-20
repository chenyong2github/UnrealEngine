// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphBlueprintLibrary.h"
#include "Graph/Nodes/MovieGraphOutputSettingNode.h"

FFrameRate UMovieGraphBlueprintLibrary::GetEffectiveFrameRate(UMovieGraphOutputSettingNode* InNode, const FFrameRate& InDefaultRate)
{
	if (InNode && InNode->bOverride_OutputFrameRate)
	{
		return InNode->OutputFrameRate;
	}

	return InDefaultRate;
}