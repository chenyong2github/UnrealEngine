// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimNode_CustomProperty.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimLayerInterface.h"

#include "AnimNode_LinkedAnimGraph.generated.h"

struct FAnimInstanceProxy;
class UUserDefinedStruct;
struct FAnimBlueprintFunction;
class IAnimClassInterface;

USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_LinkedAnimGraph : public FAnimNode_CustomProperty
{
	GENERATED_BODY()

public:

	FAnimNode_LinkedAnimGraph();

	/** 
	 *  Input poses for the node, intentionally not accessible because if there's no input
	 *  nodes in the target class we don't want to show these as pins
	 */
	UPROPERTY()
	TArray<FPoseLink> InputPoses;

	/** List of input pose names, 1-1 with pose links about, built by the compiler */
	UPROPERTY()
	TArray<FName> InputPoseNames;

	/** The class spawned for this linked instance */
	UPROPERTY(EditAnywhere, Category = Settings)
	TSubclassOf<UAnimInstance> InstanceClass;

	/** Optional tag used to identify this linked instance */
	UPROPERTY(EditAnywhere, Category = Settings)
	FName Tag;

	// The root node of the dynamically-linked graph
	FAnimNode_Base* LinkedRoot;

	// Our node index
	int32 NodeIndex;

	// Cached node index for our linked function
	int32 CachedLinkedNodeIndex;

protected:
	// Inertial blending duration to request next update
	float PendingBlendDuration;

public:
	/** Whether named notifies will be received by this linked instance from other instances (outer or other linked instances) */
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bReceiveNotifiesFromLinkedInstances : 1;

	/** Whether named notifies will be propagated from this linked instance to other instances (outer or other linked instances) */
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bPropagateNotifiesToLinkedInstances : 1;

	/** Dynamically set the anim class of this linked instance */
	void SetAnimClass(TSubclassOf<UAnimInstance> InClass, const UAnimInstance* InOwningAnimInstance);

	/** Get the function name we should be linking with when we call DynamicLink/Unlink */
	virtual FName GetDynamicLinkFunctionName() const;

	/** Get the dynamic link target */
	virtual UAnimInstance* GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	// Initializes only the sub-graph that this node is linked to
	void InitializeSubGraph_AnyThread(const FAnimationInitializeContext& Context);

	// Caches bones only for the sub graph that this node is linked to
	void CacheBonesSubGraph_AnyThread(const FAnimationCacheBonesContext& Context);

protected:

	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual bool NeedsOnInitializeAnimInstance() const override { return true; }
	// End of FAnimNode_Base interface

	// Re-create the linked instances for this node
	void ReinitializeLinkedAnimInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewAnimInstance = nullptr);

	// Shutdown the currently running instance
	void TeardownInstance();

	// Get Target Class
	virtual UClass* GetTargetClass() const override 
	{
		return *InstanceClass;
	}

	/** Link up pose links dynamically with linked instance */
	void DynamicLink(UAnimInstance* InOwningAnimInstance);

	/** Break any pose links dynamically with linked instance */
	void DynamicUnlink(UAnimInstance* InOwningAnimInstance);

	/** Helper function for finding function inputs when linking/unlinking */
	int32 FindFunctionInputIndex(const FAnimBlueprintFunction& AnimBlueprintFunction, const FName& InInputName);

	/** Request a blend when the active instance changes */
	void RequestBlend(const IAnimClassInterface* PriorAnimBPClass, const IAnimClassInterface* NewAnimBPClass);

	friend class UAnimInstance;
};

UE_DEPRECATED(4.24, "FAnimNode_SubInstance has been renamed to FAnimNode_LinkedAnimGraph")
typedef FAnimNode_LinkedAnimGraph FAnimNode_SubInstance;