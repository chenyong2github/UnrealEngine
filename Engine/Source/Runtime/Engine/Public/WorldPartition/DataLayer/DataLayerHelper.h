// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct ENGINE_API FDataLayersHelper
{
	static const uint32 NoDataLayerID = 0;
	static uint32 ComputeDataLayerID(const TArray<FName>& DataLayers);
};