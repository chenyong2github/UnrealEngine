// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimNodeBase.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimNode_IKRetargeter.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct IKRIG_API FAnimNode_IKRetargeter : public FAnimNode_Base
{
	GENERATED_BODY()

	/** The Skeletal Mesh Component to retarget animation from. Assumed to be animated and tick BEFORE this anim instance.*/
	UPROPERTY(BlueprintReadWrite, transient, Category=Settings, meta=(PinShownByDefault))
	TWeakObjectPtr<USkeletalMeshComponent> SourceMeshComponent = nullptr;

	/* If SourceMeshComponent is not valid, and if this is true, it will look for attached parent as a source */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta = (NeverAsPin))
	bool bUseAttachedParent = true;

	/** Map of chain names to per-chain retarget settings (can be modified at runtime).*/
	UPROPERTY(BlueprintReadWrite, transient, Category=Settings, meta=(PinShownByDefault))
	TMap<FName, FIKRetargetChainSettings> ChainSettings;
	
	/** Retarget asset to use. Must define a Source and Target IK Rig compatible with the SourceMeshComponent and current anim instance.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	UIKRetargeter* IKRetargeterAsset = nullptr;

	/** When false, IK is not applied as part of retargeter. Useful for debugging limb issues suspected to be caused by IK.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEnableIK = true;
	
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
	USkeletalMeshComponent* GetSourceMesh() const;

	// indicates that all prerequisites are met and node is ready to operate
	UPROPERTY(Transient)
	bool bIsInitialized = false;
	
	// source mesh references, cached during init so that we can compare and see if it has changed
	TWeakObjectPtr<USkeletalMeshComponent>	CurrentlyUsedSourceMeshComponent;
	TWeakObjectPtr<USkeletalMesh>			CurrentlyUsedSourceMesh;
	TWeakObjectPtr<USkeletalMesh>			CurrentlyUsedTargetMesh;
	TWeakObjectPtr<UIKRetargeter>			CurrentlyUsedRetargeter;
	TWeakObjectPtr<UIKRigDefinition>		CurrentlyUsedSourceIKRig;
	TWeakObjectPtr<UIKRigDefinition>		CurrentlyUsedTargetIKRig;

	// cached transforms, copied on the game thread
	TArray<FTransform> SourceMeshComponentSpaceBoneTransforms;
};
