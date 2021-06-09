// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlendLoader_WarpMap.h"
#include "RHI.h"
#include "DisplayClusterShadersLog.h"

THIRD_PARTY_INCLUDES_START
#include "mpcdiProfile.h"
#include "mpcdiReader.h"
#include "mpcdiDisplay.h"
#include "mpcdiBuffer.h"
#include "mpcdiRegion.h"
#include "mpcdiAlphaMap.h"
#include "mpcdiBetaMap.h"
#include "mpcdiDistortionMap.h"
#include "mpcdiGeometryWarpFile.h"
THIRD_PARTY_INCLUDES_END


static constexpr float kEpsilon = 0.00001f;

namespace
{
	float GetUnitToCentimeter(mpcdi::GeometricUnit GeomUnit)
	{
		switch (GeomUnit)
		{
		case mpcdi::GeometricUnitmm:
			return 1.f / 10.f;
		case mpcdi::GeometricUnitcm:
			return 1.f;
		case mpcdi::GeometricUnitdm:
			return 10.f;
		case mpcdi::GeometricUnitm:
			return 100.f;
		case mpcdi::GeometricUnitin:
			return 2.54f;
		case mpcdi::GeometricUnitft:
			return 30.48f;
		case mpcdi::GeometricUnityd:
			return 91.44f;
		case mpcdi::GeometricUnitunkown:
			return 1.f;
		default:
			check(false);
			return 1.f;
		}
	}
}


bool FDisplayClusterWarpBlendLoader_WarpMap::Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType ProfileType, mpcdi::GeometryWarpFile* SourceWarpMap)
{
	check(SourceWarpMap);

	if (!OutWarpMapData.Initialize(SourceWarpMap->GetSizeX(), SourceWarpMap->GetSizeY()))
	{
		return false;
	}

	bool bIsProfile2D = true;
	float UnitToCentemeter = 1.f;

	FMatrix ConventionMatrix = FMatrix::Identity;

	switch (ProfileType)
	{
	case EDisplayClusterWarpProfileType::warp_A3D:
		// Unreal is in cm, so we need to convert to cm.
		UnitToCentemeter = GetUnitToCentimeter(SourceWarpMap->GetGeometricUnit());

		// Convert from MPCDI convention to Unreal convention
		// MPCDI is Right Handed (Y is up, X is left, Z is in the screen)
		// Unreal is Left Handed (Z is up, X in the screen, Y is right)
		ConventionMatrix = FMatrix(
			FPlane(0.f, UnitToCentemeter, 0.f, 0.f),
			FPlane(0.f, 0.f, UnitToCentemeter, 0.f),
			FPlane(-UnitToCentemeter, 0.f, 0.f, 0.f),
			FPlane(0.f, 0.f, 0.f, 1.f));

		bIsProfile2D = false;

		break;

	default:
		break;
	};


	for (int y = 0; y < OutWarpMapData.Height; ++y)
	{
		for (int x = 0; x < OutWarpMapData.Width; ++x)
		{
			mpcdi::NODE& node = (*SourceWarpMap)(x, y);
			FVector t(node.r, node.g, bIsProfile2D ? 0.f : node.b);

			FVector4& Pts = OutWarpMapData.WarpData[x + y * OutWarpMapData.Width];

			if ((!(fabsf(t.X) < kEpsilon && fabsf(t.Y) < kEpsilon && fabsf(t.Z) < kEpsilon))
				&& (!FMath::IsNaN(t.X) && !FMath::IsNaN(t.Y) && !FMath::IsNaN(t.Z)))
			{
				Pts = ConventionMatrix.TransformPosition(t);
				Pts.W = 1;
			}
			else
			{
				Pts = FVector4(0.f, 0.f, 0.f, -1.f);
			}
		}
	}

	if (ProfileType == EDisplayClusterWarpProfileType::warp_A3D)
	{
		// Remove noise from warp mesh (small areas less than 3*3 quads)
		OutWarpMapData.ClearNoise(FIntPoint(3, 3), FIntPoint(2, 3));
	}

	return true;
}

bool FDisplayClusterWarpBlendLoader_WarpMap::Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, const TArray<FVector>& InPoints, int WarpX, int WarpY, float WorldScale, bool bIsUnrealGameSpace)
{
	if (!OutWarpMapData.Initialize(WarpX, WarpY))
	{
		return false;
	}

	OutWarpMapData.LoadGeometry(InProfileType, InPoints, WorldScale, bIsUnrealGameSpace);
	return true;
}

bool FDisplayClusterWarpBlendLoader_WarpMap::Load(FLoadedWarpMapData& OutWarpMapData, EDisplayClusterWarpProfileType InProfileType, mpcdi::PFM* SourcePFM, float PFMScale, bool bIsUnrealGameSpace)
{
	check(SourcePFM);

	if (!OutWarpMapData.Initialize(SourcePFM->GetSizeX(), SourcePFM->GetSizeY()))
	{
		return false;
	}

	TArray<FVector> WarpMeshPoints;
	WarpMeshPoints.Reserve(OutWarpMapData.Width * OutWarpMapData.Height);

	for (int y = 0; y < OutWarpMapData.Height; ++y)
	{
		for (int x = 0; x < OutWarpMapData.Width; ++x)
		{
			mpcdi::NODE node = SourcePFM->operator()(x, y);

			FVector pts = (FVector&)(node);
			WarpMeshPoints.Add(pts);
		}
	}

	OutWarpMapData.LoadGeometry(InProfileType, WarpMeshPoints, PFMScale, bIsUnrealGameSpace);
	return true;
}

bool FLoadedWarpMapData::Initialize(int InWidth, int InHeight)
{
	if (FMath::Min(InWidth, InHeight) <= 1 || FMath::Max(InWidth, InHeight) >= GMaxTextureDimensions)
	{
		UE_LOG(LogDisplayClusterWarpBlend, Error, TEXT("Invalid PFM warpmap data size '%d x %d'"), InWidth, InHeight);

		return false;
	}

	Width = InWidth;
	Height = InHeight;
	WarpData = new FVector4[Width * Height];

	return true;
}

FLoadedWarpMapData::~FLoadedWarpMapData()
{
	if (WarpData != nullptr)
	{
		delete WarpData;
		WarpData = nullptr;
	}
}

void FLoadedWarpMapData::LoadGeometry(EDisplayClusterWarpProfileType ProfileType, const TArray<FVector>& InPoints, float WorldScale, bool bIsUnrealGameSpace)
{
	FMatrix ConventionMatrix = FMatrix::Identity;

	switch (ProfileType)
	{
	case EDisplayClusterWarpProfileType::warp_A3D:
		if (bIsUnrealGameSpace)
		{
			ConventionMatrix = FMatrix(
				FPlane(WorldScale, 0.f, 0.f, 0.f),
				FPlane(0.f, WorldScale, 0.f, 0.f),
				FPlane(0.f, 0.f, WorldScale, 0.f),
				FPlane(0.f, 0.f, 0.f, 1.f));
		}
		else
		{
			// Convert from MPCDI convention to Unreal convention
			// MPCDI is Right Handed (Y is up, X is left, Z is in the screen)
			// Unreal is Left Handed (Z is up, X in the screen, Y is right)
			ConventionMatrix = FMatrix(
				FPlane(0.f, WorldScale, 0.f, 0.f),
				FPlane(0.f, 0.f, WorldScale, 0.f),
				FPlane(-WorldScale, 0.f, 0.f, 0.f),
				FPlane(0.f, 0.f, 0.f, 1.f));
		}
		break;

	default:
		break;
	};

	FVector4* DstPoint = WarpData;;
	for (const FVector& PointIt : InPoints)
	{
		const FVector& t = PointIt;

		FVector4& Pts = *DstPoint;
		DstPoint++;

		if ((!(fabsf(t.X) < kEpsilon && fabsf(t.Y) < kEpsilon && fabsf(t.Z) < kEpsilon))
			&& (!FMath::IsNaN(t.X) && !FMath::IsNaN(t.Y) && !FMath::IsNaN(t.Z)))
		{
			Pts = ConventionMatrix.TransformPosition(t);
			Pts.W = 1;
		}
		else
		{
			Pts = FVector4(0.f, 0.f, 0.f, -1.f);
		}
	}
}



bool FLoadedWarpMapData::Is3DPointValid(int X, int Y) const
{
	if (X >= 0 && X < Width && Y >= 0 && Y < Height)
	{
		FVector4* pts = (FVector4*)WarpData;
		return pts[(X + Y * Width)].W > 0;
	}

	return false;
}

void FLoadedWarpMapData::ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules)
{
	if (Width > 10 && Height > 10)
	{
		//Remove noise for large warp mesh
		int MaxLoops = 50;
		while (MaxLoops-- > 0)
		{
			if (!RemoveDetachedPoints(SearchXYDepth, AllowedXYDepthRules))
			{
				break;
			}
		}
	}
}

int FLoadedWarpMapData::RemoveDetachedPoints(const FIntPoint& SearchLen, const FIntPoint& RemoveRule)
{
	FVector4* pts = (FVector4*)WarpData;

	int SearchX = SearchLen.X * Width / 100;
	int SearchY = SearchLen.Y * Height / 100;
	int Rule1X = RemoveRule.X * Width / 100;
	int Rule1Y = RemoveRule.Y * Height / 100;

	int TotalChangesCount = 0;
	static int DirIndexValue[] = { -1, 1 };

	for (int Y = 0; Y < Height; ++Y)
	{
		for (int X = 0; X < Width; ++X)
		{
			if (Is3DPointValid(X, Y))
			{
				int XLen = 0;
				int YLen = 0;

				for (int DirIndex = 0; DirIndex < 2; DirIndex++)
				{
					int dx = 0;
					int dy = 0;

					for (int Offset = 1; Offset <= SearchX; Offset++)
					{
						if (Is3DPointValid(X + DirIndexValue[DirIndex] * Offset, Y))
						{
							dx++;
						}
						else
						{
							break;
						}
					}
					for (int Offset = 1; Offset <= SearchY; Offset++)
					{
						if (Is3DPointValid(X, Y + DirIndexValue[DirIndex] * Offset))
						{
							dy++;
						}
						else
						{
							break;
						}
					}

					XLen = FMath::Max(XLen, dx);
					YLen = FMath::Max(YLen, dy);
				}

				bool Test1 = XLen >= Rule1X && YLen >= Rule1Y;
				bool Test2 = YLen >= Rule1X && XLen >= Rule1Y;

				if (!Test1 && !Test2)
				{
					// Both test failed, remove it
					pts[(X + Y * Width)] = FVector4(0.f, 0.f, 0.f, -1.f);
					TotalChangesCount++;
				}
			}
		}
	}

	return TotalChangesCount;
}

