// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Drawing/ControlRigDrawInterface.h"

#include "Core/PBIKSolver.h"
#include "Core/PBIKDebug.h"

#include "PBIK_Shared.h"

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
	float OffsetAlpha = 1.0f;

	UPROPERTY()
	float StrengthAlpha = 1.0f;
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
	UPROPERTY(transient)
	TArray<int32> EffectorSolverIndices;

	UPROPERTY(meta = (Input))
	TArray<FPBIKBoneSetting> BoneSettings;

	UPROPERTY(meta = (Input))
	FPBIKSolverSettings Settings;

	UPROPERTY(meta = (Input))
	FPBIKDebug Debug;

	UPROPERTY(transient)
	TArray<int32> BoneSettingToSolverBoneIndex;

	UPROPERTY(transient)
	TArray<int32> SolverBoneToElementIndex;

	UPROPERTY(transient)
	FPBIKSolver Solver;
};
