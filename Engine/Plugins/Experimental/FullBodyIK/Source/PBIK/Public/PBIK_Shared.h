// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Core/PBIKSolver.h"

#include "PBIK_Shared.generated.h"

UENUM(BlueprintType)
enum class EPBIKLimitType : uint8
{
	Free,
	Limited,
	Locked,
};

USTRUCT(BlueprintType)
struct FPBIKBoneSetting
{
	GENERATED_BODY()

	FPBIKBoneSetting()
	:	Bone(NAME_None),
		PreferredAngles(FRotator::ZeroRotator) {}

	UPROPERTY(meta = (Constant, CustomWidget = "BoneName"))
	FName Bone;

	UPROPERTY(meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotationStiffness = 0.0f;
	UPROPERTY(meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.0", UIMax = "1.0"))
	float PositionStiffness = 0.0f;

	UPROPERTY()
	EPBIKLimitType X;
	UPROPERTY(meta = (ClampMin = "-180", ClampMax = "0", UIMin = "-180.0", UIMax = "0.0"))
	float MinX = 0.0f;
	UPROPERTY(meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxX = 0.0f;

	UPROPERTY()
	EPBIKLimitType Y;
	UPROPERTY(meta = (ClampMin = "-180", ClampMax = "0", UIMin = "-180.0", UIMax = "0.0"))
	float MinY = 0.0f;
	UPROPERTY(meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxY = 0.0f;

	UPROPERTY()
	EPBIKLimitType Z;
	UPROPERTY(meta = (ClampMin = "-180", ClampMax = "0", UIMin = "-180.0", UIMax = "0.0"))
	float MinZ = 0.0f;
	UPROPERTY(meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxZ = 0.0f;

	UPROPERTY()
	bool bUsePreferredAngles = false;
	UPROPERTY()
	FRotator PreferredAngles;

	void CopyToCoreStruct(PBIK::FBoneSettings& Settings) const
	{
		Settings.RotationStiffness = RotationStiffness;
		Settings.PositionStiffness = PositionStiffness;
		Settings.X = static_cast<PBIK::ELimitType>(X);
		Settings.MinX = MinX;
		Settings.MaxX = MaxX;
		Settings.Y = static_cast<PBIK::ELimitType>(Y);
		Settings.MinY = MinY;
		Settings.MaxY = MaxY;
		Settings.Z = static_cast<PBIK::ELimitType>(Z);
		Settings.MinZ = MinZ;
		Settings.MaxZ = MaxZ;
		Settings.bUsePreferredAngles = bUsePreferredAngles;
		Settings.PreferredAngles = PreferredAngles;
	}
};