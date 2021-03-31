// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IKRigBoneSetting.h"
#include "IKRigSkeleton.h"

#include "IKRigDefinition.generated.h"

struct FIKRigEffectorGoal;
class UIKRigSolver;

USTRUCT(Blueprintable)
struct IKRIG_API FBoneChain
{
	FBoneChain()
		: bUseIK(false)
	{
	}

	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName ChainName;

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName StartBone;

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName EndBone;

	UPROPERTY(EditAnywhere, Category = IK)
	bool bUseIK;
	
	UPROPERTY(EditAnywhere, Category = IK)
	FName IKGoalName;

	/*
	UPROPERTY(EditAnywhere, Category = IK)
	FName PoleVectorGoalName;

	UPROPERTY(EditAnywhere, Category = IK)
	FName PoleVectorBoneName;

	UPROPERTY(EditAnywhere, Category = IK)
	FVector PoleVectorDirection;
	*/
};

USTRUCT(Blueprintable)
struct IKRIG_API FRetargetDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = BodyParts)
	FName RootBone;

	UPROPERTY(EditAnywhere, Category = BodyParts)
	TArray<FBoneChain> BoneChains;

	FBoneChain* GetBoneChainByName(FName ChainName);
};

UCLASS(Blueprintable)
class IKRIG_API UIKRigDefinition : public UObject
{
	GENERATED_BODY()

public:

	/** load a source skeleton to import a hierarchy from */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	TSoftObjectPtr<UObject> SourceAsset;

	/** hierarchy and bone-pose transforms */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	FIKRigSkeleton Skeleton;

	/** stack of solvers, of varying types, executed in serial fashion where
	output of prior solve is input to the next */
	UPROPERTY(EditAnywhere, instanced, Category = "IK")
	TArray<UIKRigSolver*> Solvers;

	/** per-bone settings, used by solvers that support them */
	UPROPERTY(EditAnywhere, instanced, Category = "IK")
	TArray<UIKRigBoneSetting*> BoneSettings;

	/** bone chains for animation retargeting */
	UPROPERTY(EditAnywhere, Category = "Retargeting")
	FRetargetDefinition RetargetDefinition;

	/** get a list of all goal names used in the solvers */
	void GetGoalNamesFromSolvers(TArray<FIKRigEffectorGoal>& OutGoalNames) const;

	/** get bone associated with a given goal */
	FName GetBoneNameForGoal(FName GoalName) const;

private:
	
	friend class UIKRigController;
};

