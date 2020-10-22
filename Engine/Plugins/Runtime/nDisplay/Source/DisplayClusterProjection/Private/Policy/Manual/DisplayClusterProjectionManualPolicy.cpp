// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Manual/DisplayClusterProjectionManualPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Misc/DisplayClusterHelpers.h"

#include "IDisplayCluster.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Components/DisplayClusterScreenComponent.h"


FDisplayClusterProjectionManualPolicy::FDisplayClusterProjectionManualPolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FDisplayClusterProjectionPolicyBase(ViewportId, Parameters)
{
}

FDisplayClusterProjectionManualPolicy::~FDisplayClusterProjectionManualPolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionManualPolicy::StartScene(UWorld* World)
{
	check(IsInGameThread());
}

void FDisplayClusterProjectionManualPolicy::EndScene()
{
	check(IsInGameThread());
}

bool FDisplayClusterProjectionManualPolicy::HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount)
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("Initializing internals for the viewport '%s'"), *GetViewportId());

	// Get view rotation
	if (!DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::Rotation), ViewRotation))
	{
		UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("No rotation specified for projection policy of viewport '%s'"), *GetViewportId());
	}

	// Get matrix data
	bool bDataTypeDetermined = false;
	if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::Matrix),     ProjectionMatrix[0]) ||
		DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::MatrixLeft), ProjectionMatrix[0]))
	{
		if (ViewsAmount == 2)
		{
			if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::MatrixRight), ProjectionMatrix[1]))
			{
				bDataTypeDetermined = true;
				DataType = EManualDataType::Matrix;
			}
		}
		else
		{
			bDataTypeDetermined = true;
			DataType = EManualDataType::Matrix;
		}
	}

	if (!bDataTypeDetermined)
	{
		FString AnglesLeft;

		if (DisplayClusterHelpers::map::template ExtractValue(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::Frustum),     AnglesLeft) ||
			DisplayClusterHelpers::map::template ExtractValue(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::FrustumLeft), AnglesLeft))
		{
			if (!ExtractAngles(AnglesLeft, FrustumAngles[0]))
			{
				UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("Couldn't extract frustum angles from value '%s'"), *AnglesLeft);
				return false;
			}

			if (ViewsAmount == 2)
			{
				FString AnglesRight;

				if (DisplayClusterHelpers::map::template ExtractValue(GetParameters(), FString(DisplayClusterProjectionStrings::cfg::manual::FrustumRight), AnglesRight))
				{
					if (!ExtractAngles(AnglesRight, FrustumAngles[1]))
					{
						UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("Couldn't extract frustum angles from value '%s'"), *AnglesRight);
						return false;
					}

					bDataTypeDetermined = true;
					DataType = EManualDataType::FrustumAngles;
				}
			}
			else
			{
				bDataTypeDetermined = true;
				DataType = EManualDataType::FrustumAngles;
			}
		}

	}

	if (!bDataTypeDetermined)
	{
		UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("No mandatory data specified for projection policy of viewport '%s'"), *GetViewportId());
		return false;
	}

	return true;
}

void FDisplayClusterProjectionManualPolicy::HandleRemoveViewport()
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
}

bool FDisplayClusterProjectionManualPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& InViewOffset, const float InWorldToMeters, const float InNCP, const float InFCP)
{
	check(IsInGameThread());
	check(ViewIdx < 2);

	// Add local rotation specified in config
	InOutViewRotation += ViewRotation;

	// Store culling data
	NCP = InNCP;
	FCP = InFCP;

	return true;
}

bool FDisplayClusterProjectionManualPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());
	check(ViewIdx < 2);

	bool bResult = false;

	switch (DataType)
	{
	case EManualDataType::Matrix:
		OutPrjMatrix = ProjectionMatrix[ViewIdx];
		bResult = true;
		break;

	case EManualDataType::FrustumAngles:
		OutPrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromAngles(FrustumAngles[ViewIdx].Left, FrustumAngles[ViewIdx].Right, FrustumAngles[ViewIdx].Top, FrustumAngles[ViewIdx].Bottom, NCP, FCP);
		bResult = true;
		break;

	default:
		break;
	}

	return bResult;
}

bool FDisplayClusterProjectionManualPolicy::ExtractAngles(const FString& InAngles, FFrustumAngles& OutAngles)
{
	float Left;
	if (!DisplayClusterHelpers::str::ExtractValue(InAngles, FString(DisplayClusterProjectionStrings::cfg::manual::AngleL), Left))
	{
		return false;
	}

	float Right;
	if (!DisplayClusterHelpers::str::ExtractValue(InAngles, FString(DisplayClusterProjectionStrings::cfg::manual::AngleR), Right))
	{
		return false;
	}

	float Top;
	if (!DisplayClusterHelpers::str::ExtractValue(InAngles, FString(DisplayClusterProjectionStrings::cfg::manual::AngleT), Top))
	{
		return false;
	}

	float Bottom;
	if (!DisplayClusterHelpers::str::ExtractValue(InAngles, FString(DisplayClusterProjectionStrings::cfg::manual::AngleB), Bottom))
	{
		return false;
	}

	OutAngles.Left   = Left;
	OutAngles.Right  = Right;
	OutAngles.Top    = Top;
	OutAngles.Bottom = Bottom;

	return true;
}
