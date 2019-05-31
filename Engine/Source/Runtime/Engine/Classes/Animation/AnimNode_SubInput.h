// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimCurveTypes.h"
#include "BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_SubInput.generated.h"

USTRUCT()
struct ENGINE_API FAnimNode_SubInput : public FAnimNode_Base
{
	GENERATED_BODY()

	/** The default name of this input pose */
	static const FName DefaultInputPoseName;

	FAnimNode_SubInput()
		: Name(DefaultInputPoseName)
		, Graph(NAME_None)
		, InputProxy(nullptr)
	{
	}

	/** The name of this sub input node's pose, used to identify the input of this graph. */
	UPROPERTY(EditAnywhere, Category = "Inputs", meta = (NeverAsPin))
	FName Name;

	/** The graph that this sub-input node is in, filled in by the compiler */
	UPROPERTY()
	FName Graph;

	/** Input pose, optionally linked dynamically to another graph */
	UPROPERTY()
	FPoseLink InputPose;

	/** 
	 * If this sub-input is not dynamically linked, this cached data will be populated by the calling 
	 * sub-instance node before this graph is processed.
	 */
	FCompactHeapPose CachedInputPose;
	FBlendedHeapCurve CachedInputCurve;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	/** Called by sub-instance nodes to dynamically link this to an outer graph */
	void DynamicLink(FAnimInstanceProxy* InInputProxy, FPoseLinkBase* InPoseLink);

	/** Called by sub-instance nodes to dynamically unlink this to an outer graph */
	void DynamicUnlink();

private:
	/** The proxy to use when getting inputs, set when dynamically linked */
	FAnimInstanceProxy* InputProxy;
};
