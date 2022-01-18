// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimNodeBase.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"

#include "AnimNode_RetargetPoseFromMesh.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct IKRIG_API FAnimNode_RetargetPoseFromMesh : public FAnimNode_Base
{
	GENERATED_BODY()

	/** The Skeletal Mesh Component to retarget animation from. Assumed to be animated and tick BEFORE this anim instance.*/
	UPROPERTY(BlueprintReadWrite, transient, Category=Settings, meta=(PinShownByDefault))
	TWeakObjectPtr<USkeletalMeshComponent> SourceMeshComponent = nullptr;

	/* If SourceMeshComponent is not valid, and if this is true, it will look for attached parent as a source */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta = (NeverAsPin))
	bool bUseAttachedParent = true;
	
	/** Retarget asset to use. Must define a Source and Target IK Rig compatible with the SourceMeshComponent and current anim instance.*/
	UPROPERTY(EditAnywhere, Category = Settings)
	TObjectPtr<UIKRetargeter> IKRetargeterAsset = nullptr;

#if WITH_EDITOR
	/** when true, will copy all setting from target IK Rig asset each tick (for live preview) */
	bool bDriveTargetIKRigWithAsset = false;
#endif
	
	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

#if WITH_EDITOR
	/** Force reinitialization. */
	void SetProcessorNeedsInitialized();
#endif

	/** Read-only access to the runtime processor */
	const UIKRetargetProcessor* GetRetargetProcessor() const;

private:
	
	void EnsureInitialized(const UAnimInstance* InAnimInstance);
	void CopyBoneTransformsFromSource(USkeletalMeshComponent* TargetMeshComponent);
	
	// source mesh references, cached during init so that we can compare and see if it has changed
	TWeakObjectPtr<USkeletalMesh> CurrentlyUsedSourceMesh;
	TWeakObjectPtr<USkeletalMesh> CurrentlyUsedTargetMesh;

	/** the runtime processor used to run the retarget and generate new poses */
	UPROPERTY(Transient)
	TObjectPtr<UIKRetargetProcessor> Processor = nullptr;

	// cached transforms, copied on the game thread
	TArray<FTransform> SourceMeshComponentSpaceBoneTransforms;

	// mapping from required bones to actual bones within the target skeleton
	TArray< TPair<int32, int32> > RequiredToTargetBoneMapping;
};
