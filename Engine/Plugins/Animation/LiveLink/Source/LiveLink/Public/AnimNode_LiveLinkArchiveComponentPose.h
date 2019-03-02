// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "LiveLinkRetargetAsset.h"
#include "LiveLinkArchiveComponent.h"

#include "AnimNode_LiveLinkArchiveComponentPose.generated.h"


USTRUCT(BlueprintInternalUseOnly)
struct LIVELINK_API FAnimNode_LiveLinkArchiveComponentPose : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** 
	* The binding of the component source we want to bind to.
	* We will search the owning actor's components to try and find a
	* LiveLinkArchiveComponent that matches this binding name.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceData, meta = (PinShownByDefault))
	FName ArchiveNameBinding;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, NoClear, Category = Retarget, meta = (NeverAsPin))
	TSubclassOf<ULiveLinkRetargetAsset> RetargetAsset;

	UPROPERTY(transient)
	ULiveLinkRetargetAsset* CurrentRetargetAsset;

	UPROPERTY(transient)
	ULiveLinkArchiveComponent* CurrentLiveLinkArchiveComponent;

	FAnimNode_LiveLinkArchiveComponentPose();

	// FAnimNode_Base interface
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext & Context) override {}
	virtual void Update_AnyThread(const FAnimationUpdateContext & Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	// End of FAnimNode_Base interface

private:
	// Delta time from update so that it can be passed to retargeter
	float CachedDeltaTime;
};