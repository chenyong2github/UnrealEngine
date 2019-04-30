// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimNode_CustomProperty.h"
#include "Animation/AnimInstance.h"
#include "AnimNode_SubInstance.generated.h"

struct FAnimInstanceProxy;

USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SubInstance : public FAnimNode_CustomProperty
{
	GENERATED_BODY()

public:

	FAnimNode_SubInstance();

	/** 
	 *  Input pose for the node, intentionally not accessible because if there's no input
	 *  Node in the target class we don't want to show this as a pin
	 */
	UPROPERTY()
	FPoseLink InPose;

	/** The class spawned for this sub-instance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TSubclassOf<UAnimInstance> InstanceClass;

	/** Optional tag used to identify this sub-instance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName Tag;

	// Temporary storage for the output of the subinstance, will be copied into output pose.
	FBlendedHeapCurve BlendedCurve;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

	// Shutdown the currently running instance
	void TeardownInstance();

	// Get Target Class
	virtual UClass* GetTargetClass() const override 
	{
		return *InstanceClass;
	}
};
