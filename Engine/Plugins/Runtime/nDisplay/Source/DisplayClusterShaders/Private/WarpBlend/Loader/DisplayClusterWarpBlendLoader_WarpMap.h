// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WarpBlend/DisplayClusterWarpEnums.h"


namespace mpcdi
{
	struct GeometryWarpFile;
	struct PFM;
}
class FDisplayClusterWarpBlendLoader_WarpMap;

class FLoadedWarpMapData
{
public:
	~FLoadedWarpMapData();

public:
	int32 GetWidth() const
	{
		return Width;
	}

	int32 GetHeight() const
	{
		return Height;
	}

	const FVector4* GetWarpData() const
	{
		return WarpData;
	}

protected:
	friend FDisplayClusterWarpBlendLoader_WarpMap;

	bool Initialize(int InWidth, int InHeight);

	void LoadGeometry(EDisplayClusterWarpProfileType ProfileType, const TArray<FVector>& InPoints, float WorldScale, bool bIsUnrealGameSpace);
	bool Is3DPointValid(int X, int Y) const;
	void ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);
	int RemoveDetachedPoints(const FIntPoint& SearchLen, const FIntPoint& RemoveRule);

protected:
	int32 Width = 0;
	int32 Height = 0;
	FVector4* WarpData = nullptr;
};

class FDisplayClusterWarpBlendLoader_WarpMap
{
public:
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, mpcdi::GeometryWarpFile* SourceWarpMap);
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, const TArray<FVector>& InPoints, int WarpX, int WarpY, float WorldScale, bool bIsUnrealGameSpace);
	static bool Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, mpcdi::PFM* SourcePFM, float PFMScale, bool bIsUnrealGameSpace);
};

