// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_CopyPoseFromMesh.generated.h"

class USkeletalMeshComponent;
struct FAnimInstanceProxy;

/**
 *	Simple controller to copy a bone's transform to another one.
 */

USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_CopyPoseFromMesh : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/*  This is used by default if it's valid */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Copy, meta=(PinShownByDefault))
	TWeakObjectPtr<USkeletalMeshComponent> SourceMeshComponent;

	/* If SourceMeshComponent is not valid, and if this is true, it will look for attahced parent as a source */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Copy, meta = (NeverAsPin))
	bool bUseAttachedParent;

	/* Copy curves also from SouceMeshComponent. This will copy the curves if this instance also contains */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Copy, meta = (NeverAsPin))
	bool bCopyCurves;

	FAnimNode_CopyPoseFromMesh();

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:
	TWeakObjectPtr<USkeletalMeshComponent>	CurrentlyUsedSourceMeshComponent;
	TWeakObjectPtr<USkeletalMesh>			CurrentlyUsedSourceMesh;
	// cache of target space bases to source space bases
	TMap<int32, int32> BoneMapToSource;
	TMap<FName, SmartName::UID_Type> CurveNameToUIDMap;

	// Cached transforms, copied on the game thread
	TArray<FTransform> SourceMeshTransformArray;

	// Cached curves, copied on the game thread
	TMap<FName, float> SourceCurveList;

	// Reference skeleton used for parent bone lookups
	FReferenceSkeleton* RefSkeleton;

	// reinitialize mesh component 
	void ReinitializeMeshComponent(USkeletalMeshComponent* NewSkeletalMeshComponent, USkeletalMeshComponent* TargetMeshComponent);
	void RefreshMeshComponent(USkeletalMeshComponent* TargetMeshComponent);
};
