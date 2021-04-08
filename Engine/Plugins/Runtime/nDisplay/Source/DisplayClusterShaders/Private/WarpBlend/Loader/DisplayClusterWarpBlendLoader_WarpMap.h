// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WarpBlend/DisplayClusterWarpEnums.h"


namespace mpcdi
{
	struct GeometryWarpFile;
	struct PFM;
}

class FLoadedWarpMapData
{
public:
	~FLoadedWarpMapData()
	{
		if (WarpData != nullptr)
		{
			delete WarpData;
			WarpData = nullptr;
		}
	}

	void Initialize(int InWidth, int InHeight)
	{
		Width = InWidth;
		Height = InHeight;
		WarpData = new FVector4[Width * Height];
	}

public:
	void LoadGeometry(EDisplayClusterWarpProfileType ProfileType, const TArray<FVector>& InPoints, float WorldScale, bool bIsUnrealGameSpace);
	bool Is3DPointValid(int X, int Y) const;
	void ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);
	int RemoveDetachedPoints(const FIntPoint& SearchLen, const FIntPoint& RemoveRule);

public:
	int Width = 0;
	int Height = 0;
	FVector4* WarpData = nullptr;
};

class FDisplayClusterWarpBlendLoader_WarpMap
{
public:
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, mpcdi::GeometryWarpFile* SourceWarpMap);
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, const TArray<FVector>& InPoints, int WarpX, int WarpY, float WorldScale, bool bIsUnrealGameSpace);
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, mpcdi::PFM* SourcePFM, float PFMScale, bool bIsUnrealGameSpace);
};
