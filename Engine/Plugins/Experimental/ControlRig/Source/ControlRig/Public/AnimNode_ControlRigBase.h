// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNode_CustomProperty.h"
#include "AnimNode_ControlRigBase.generated.h"

class UControlRig;
class UNodeMappingContainer;
/**
 * Animation node that allows animation ControlRig output to be used in an animation graph
 */
USTRUCT()
struct CONTROLRIG_API FAnimNode_ControlRigBase : public FAnimNode_CustomProperty
{
	GENERATED_BODY()

	FAnimNode_ControlRigBase();

	/* return Control Rig of current object */
	virtual UControlRig* GetControlRig() const PURE_VIRTUAL(FAnimNode_ControlRigBase::GetControlRig, return nullptr; );

	// FAnimNode_Base interface
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;

protected:
	/** Rig Hierarchy bone name to required array index mapping */
	UPROPERTY(transient)
	TMap<FName, uint16> ControlRigBoneMapping;

	/** Rig Curve name to Curve LUI mapping */
	UPROPERTY(transient)
	TMap<FName, uint16> ControlRigCurveMapping;

	UPROPERTY(transient)
	TMap<FName, uint16> InputToCurveMappingUIDs;

	/** Node Mapping Container */
	UPROPERTY(transient)
	TWeakObjectPtr<UNodeMappingContainer> NodeMappingContainer;

	UPROPERTY(transient)
	bool bUpdateInput;

	UPROPERTY(transient)
	bool bExecute;

	// update input/output to control rig
	virtual void UpdateInput(UControlRig* ControlRig, const FPoseContext& InOutput);
	virtual void UpdateOutput(UControlRig* ControlRig, FPoseContext& InOutput);
	virtual UClass* GetTargetClass() const override;

	friend struct FControlRigSequencerAnimInstanceProxy;
};

