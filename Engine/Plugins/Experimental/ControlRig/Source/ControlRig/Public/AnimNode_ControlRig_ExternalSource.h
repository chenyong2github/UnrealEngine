// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNode_ControlRigBase.h"
#include "AnimNode_ControlRig_ExternalSource.generated.h"

/**
 * Animation node that allows animation ControlRig output to be used in an animation graph
 */
USTRUCT()
struct CONTROLRIG_API FAnimNode_ControlRig_ExternalSource : public FAnimNode_ControlRigBase
{
	GENERATED_BODY()

	FAnimNode_ControlRig_ExternalSource();

	void SetControlRig(UControlRig* InControlRig);
	virtual UControlRig* GetControlRig() const;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Source;

private:
	UPROPERTY(transient)
	TWeakObjectPtr<UControlRig> ControlRig;
};

