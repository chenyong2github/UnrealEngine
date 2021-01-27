// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Drawing/ControlRigDrawInterface.h"

#include "Core/PBIKSolver.h"
#include "Core/PBIKDebug.h"

#include "RigUnit_PBIK.generated.h"

using PBIK::FDebugLine;

USTRUCT()
struct FPBIKDebug
{
	GENERATED_BODY()

	UPROPERTY()
	float DrawScale = 1.0f;

	UPROPERTY()
	bool bDrawDebug = false;

	void Draw(FControlRigDrawInterface* DrawInterface, FPBIKSolver* Solver) const
	{
		if (!(DrawInterface && Solver && bDrawDebug))
		{
			return;
		}

		const FLinearColor Bright = FLinearColor(0.f, 1.f, 1.f, 1.f);
		DrawInterface->DrawBox(FTransform::Identity, FTransform(FQuat::Identity, FVector(0, 0, 0), FVector(1.f, 1.f, 1.f) * DrawScale * 0.1f), Bright);

		TArray<FDebugLine> BodyLines;
		Solver->GetDebugDraw()->GetDebugLinesForBodies(BodyLines);
		const FLinearColor BodyColor = FLinearColor(0.1f, 0.1f, 1.f, 1.f);
		for (FDebugLine Line : BodyLines)
		{
			DrawInterface->DrawLine(FTransform::Identity, Line.A, Line.B, BodyColor);
		}
	}
};

USTRUCT(BlueprintType)
struct FPBIKEffector
{
	GENERATED_BODY()

	FPBIKEffector()	: Bone(NAME_None) {}

	UPROPERTY(meta = (Constant, CustomWidget = "BoneName"))
	FName Bone;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	float Alpha = 1.0f;
};

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

	FPBIKBoneSetting() : Bone(NAME_None) {}

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

USTRUCT(meta=(DisplayName="Position Based IK", Category="Hierarchy", Keywords="Position Based, PBIK, IK, Full Body"))
struct FRigUnit_PBIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_PBIK()
		: Root(NAME_None)
	{
	}

	UPROPERTY(meta = (Input, Constant, CustomWidget = "BoneName"))
	FName Root;

	UPROPERTY(meta = (Input))
	TArray<FPBIKEffector> Effectors;

	UPROPERTY(meta = (Input))
	TArray<FPBIKBoneSetting> BoneSettings;

	UPROPERTY(meta = (Input))
	FPBIKSolverSettings Settings;

	UPROPERTY(meta = (Input))
	FPBIKDebug Debug;

	UPROPERTY(transient)
	FPBIKSolver Solver;
};
