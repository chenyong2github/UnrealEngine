// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimNode_IKRetargeter.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct IKRIG_API FAnimNode_IKRetargeter : public FAnimNode_Base
{
	GENERATED_BODY()

	/** The Skeletal Mesh Component to retarget animation from. Assumed to be animated and tick BEFORE this anim instance.*/
	UPROPERTY(BlueprintReadWrite, transient, Category=Settings, meta=(PinShownByDefault))
	TWeakObjectPtr<USkeletalMeshComponent> SourceMeshComponent;
	
	/** Retarget asset to use. Must define a Source and Target IK Rig compatible with the SourceMeshComponent and current anim instance.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	UIKRetargeter* IKRetargeterAsset;

	/** Retarget asset to use. Must define a Source and Target IK Rig compatible with the SourceMeshComponent and current anim instance.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEnableIK;

	FAnimNode_IKRetargeter();

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	//virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:
	void EnsureInitialized(const UAnimInstance* InAnimInstance);
	void InitializeRetargetData(const UAnimInstance* InAnimInstance);
	void CopyBoneTransformsFromSource(USkeletalMeshComponent* TargetMeshComponent);

	// indicates that all prerequisites are met and node is ready to operate
	bool bIsInitialized;
	
	// source mesh references, cached during init so that we can compare and see if it has changed
	TWeakObjectPtr<USkeletalMeshComponent>	CurrentlyUsedSourceMeshComponent;
	TWeakObjectPtr<USkeletalMesh>			CurrentlyUsedSourceMesh;
	TWeakObjectPtr<USkeletalMesh>			CurrentlyUsedTargetMesh;
	TWeakObjectPtr<UIKRetargeter>			CurrentlyUsedRetargeter;
	TWeakObjectPtr<UIKRigDefinition>		CurrentlyUsedSourceIKRig;
	TWeakObjectPtr<UIKRigDefinition>		CurrentlyUsedTargetIKRig;

	// cached transforms, copied on the game thread
	TArray<FTransform> SourceMeshComponentSpaceBoneTransforms;
	TArray<FTransform> TargetMeshComponentSpaceBoneTransforms;
};
