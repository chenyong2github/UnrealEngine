// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "IKRigSkeleton.h"
#include "IKRigDefinition.generated.h"

class UIKRigSolver;

UCLASS()
class IKRIG_API UIKRigEffectorGoal : public UObject
{
	GENERATED_BODY()

public:
	
	UIKRigEffectorGoal() : GoalName("DefaultGoal"), BoneName(NAME_None) {}
	
	UIKRigEffectorGoal(const FName& InGoalName, const FName& InBoneName) :  GoalName(InGoalName), BoneName(InBoneName){}

	UPROPERTY(VisibleAnywhere, Category = "Goal Settings")
	FName GoalName;
	
	UPROPERTY(VisibleAnywhere, Category = "Goal Settings")
	FName BoneName;

	UPROPERTY(EditAnywhere, Category = "Goal Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PositionAlpha = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Goal Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RotationAlpha = 1.0f;

	UPROPERTY(EditAnywhere, Transient, Category = "Goal Settings")
	FTransform CurrentTransform;
	
	UPROPERTY(VisibleAnywhere, Transient, Category = "Goal Settings")
	FTransform InitialTransform;

	UPROPERTY(EditAnywhere, Category = "Goal Gizmo", meta = (ClampMin = "0.1", ClampMax = "1000.0", UIMin = "0.1", UIMax = "100.0"))
	float GizmoSize = 7.0f;

	UPROPERTY(EditAnywhere, Category = "Goal Gizmo",  meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "5.0"))
	float GizmoThickness = 0.7f;

	bool operator==(const UIKRigEffectorGoal& Other) const { return GoalName == Other.GoalName; }
};

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
class IKRIG_API UIKRigDefinition : public UObject, public IInterface_PreviewMeshProvider
{
	GENERATED_BODY()

public:

	/** the skeletal mesh that was used as the source of the skeleton data. also used for preview.
	 * NOTE: the IK rig may be played back on ANY skeleton that is compatible with it's hierarchy. */
	UPROPERTY(AssetRegistrySearchable, VisibleAnywhere, Category = "Skeleton")
	TObjectPtr<class USkeletalMesh> PreviewSkeletalMesh;

	/** hierarchy and bone-pose transforms */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	FIKRigSkeleton Skeleton;

	/** stack of solvers, of varying types, executed in serial fashion where
	output of prior solve is input to the next */
	UPROPERTY(VisibleAnywhere, instanced, Category = "IK")
	TArray<UIKRigSolver*> Solvers;

	/** goals, used by solvers that support them */
	UPROPERTY(VisibleAnywhere, Category = "IK")
	TArray<UIKRigEffectorGoal*> Goals;

	/** bone chains for animation retargeting */
	UPROPERTY(EditAnywhere, Category = "Retargeting")
	FRetargetDefinition RetargetDefinition;

	/** editors systems can use this to check if they have most up-to-date settings */
	int32 GetAssetVersion() const {return AssetVersion;};
	
	/** IInterface_PreviewMeshProvider interface */
	virtual void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true) override;
	virtual USkeletalMesh* GetPreviewMesh() const override;
	/** END IInterface_PreviewMeshProvider interface */

private:
	
	/** UObject interface */
#if WITH_EDITOR
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
#endif
	virtual void PostLoad() override;
	/** END UObject interface */

	/** this is incremented whenever the asset is modified in such a way that would require
	* any IKRigProcessors that are using it to reinitialize with the latest version */
	int32 AssetVersion = 0;

	void ResetGoalTransforms() const;
	FTransform GetGoalInitialTransform(UIKRigEffectorGoal* Goal) const;
	
	friend class UIKRigController;
};
