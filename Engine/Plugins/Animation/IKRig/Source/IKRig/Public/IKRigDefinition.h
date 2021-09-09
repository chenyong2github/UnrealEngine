// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "IKRigSkeleton.h"
#include "IKRigDefinition.generated.h"

class UIKRigSolver;

UENUM(BlueprintType)
enum class EIKRigGoalPreviewMode : uint8
{
	Additive		UMETA(DisplayName = "Additive"),
	Absolute		UMETA(DisplayName = "Absolute"),
};

UCLASS()
class IKRIG_API UIKRigEffectorGoal : public UObject
{
	GENERATED_BODY()

public:
	
	UIKRigEffectorGoal() : GoalName("DefaultGoal"), BoneName(NAME_None){}
	
	UIKRigEffectorGoal(const FName& InGoalName, const FName& InBoneName) :  GoalName(InGoalName), BoneName(InBoneName){}

	/** The name used to refer to this goal from outside systems.
	 *This is the name to use when referring to this Goal from Blueprint, Anim Graph, Control Rig or IK Retargeter.*/
	UPROPERTY(VisibleAnywhere, Category = "Goal Settings")
	FName GoalName;

	/**The name of the bone that this Goal is located at.*/
	UPROPERTY(VisibleAnywhere, Category = "Goal Settings")
	FName BoneName;

	/**Range 0-1, default is 1. Blend between the Goal position between the input bone pose (0.0) and the current goal transform (1.0).*/
	UPROPERTY(EditAnywhere, Category = "Goal Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PositionAlpha = 1.0f;

	/**Range 0-1, default is 1. Blend between the Goal rotation between the input bone pose (0.0) and the current goal transform (1.0).*/
	UPROPERTY(EditAnywhere, Category = "Goal Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float RotationAlpha = 1.0f;

	/**Effects how this Goal transform is previewed in the IK Rig editor.
	 *"Additive" interprets the Goal transform as being relative to the input pose. Useful for previewing animations.
	 *"Absolute" pins the Goal transform to the Gizmo in the viewport.
	 */
	UPROPERTY(EditAnywhere, Category = "Goal Settings")
	EIKRigGoalPreviewMode PreviewMode;

	/**The current transform of this Goal, in the Global Space of the character.*/
	UPROPERTY(EditAnywhere, Transient, Category = "Goal Settings")
	FTransform CurrentTransform;

	/**The initial transform of this Goal, as defined by the initial transform of the Goal's bone in the reference pose.*/
	UPROPERTY(VisibleAnywhere, Transient, Category = "Goal Settings")
	FTransform InitialTransform;

	/**The size of the Goal gizmo drawing in the editor viewport.*/
	UPROPERTY(EditAnywhere, Category = "Goal Gizmo", meta = (ClampMin = "0.1", ClampMax = "1000.0", UIMin = "0.1", UIMax = "100.0"))
	float GizmoSize = 7.0f;

	/**The thickness of the Goal gizmo drawing in the editor viewport.*/
	UPROPERTY(EditAnywhere, Category = "Goal Gizmo",  meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "5.0"))
	float GizmoThickness = 0.7f;

	bool operator==(const UIKRigEffectorGoal& Other) const { return GoalName == Other.GoalName; }

	virtual void PostLoad() override
	{
		Super::PostLoad();
		SetFlags(RF_Transactional);
	}
};

USTRUCT(Blueprintable)
struct IKRIG_API FBoneChain
{
	GENERATED_BODY()

	FBoneChain() = default;
	
	FBoneChain(FName InName, FName InStartBone, FName InEndBone)
	: ChainName(InName),
	StartBone(InStartBone),
	EndBone(InEndBone),
	IKGoalName(NAME_None){}

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName ChainName = NAME_None;

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName StartBone = NAME_None;

	UPROPERTY(EditAnywhere, Category = BoneChain)
	FName EndBone = NAME_None;
	
	UPROPERTY(EditAnywhere, Category = IK)
	FName IKGoalName = NAME_None;

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

	/** goals, used as effectors by solvers that support them */
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

	USkeleton* GetSkeletonAsset() const;

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

	void SortRetargetChains();
	
	friend class UIKRigController;
};
