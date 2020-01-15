// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimClassInterface.h"
#include "AnimClassData.generated.h"

class USkeleton;

UCLASS()
class ENGINE_API UAnimClassData : public UObject, public IAnimClassInterface
{
	GENERATED_BODY()
public:
	// List of state machines present in this blueprint class
	UPROPERTY()
	TArray<FBakedAnimationStateMachine> BakedStateMachines;

	/** Target skeleton for this blueprint class */
	UPROPERTY()
	class USkeleton* TargetSkeleton;

	/** A list of anim notifies that state machines (or anything else) may reference */
	UPROPERTY()
	TArray<FAnimNotifyEvent> AnimNotifies;
	
	// Indices for each of the saved pose nodes that require updating, in the order they need to get updates.
	UPROPERTY()
	TMap<FName, FCachedPoseIndices> OrderedSavedPoseIndicesMap;

	// All of the functions that this anim class provides
	UPROPERTY()
	TArray<FAnimBlueprintFunction> AnimBlueprintFunctions;

	// The array of anim nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > AnimNodeProperties;

	// The array of linked anim graph nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > LinkedAnimGraphNodeProperties;

	// The array of linked anim layer nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > LinkedAnimLayerNodeProperties;

	// Array of nodes that need a PreUpdate() call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > PreUpdateNodeProperties;

	// Array of nodes that need a DynamicReset() call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > DynamicResetNodeProperties;

	// Array of state machine nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > StateMachineNodeProperties;

	// Array of nodes that need an OnInitializeAnimInstance call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > InitializationNodeProperties;

	// Indices for any Asset Player found within a specific (named) Anim Layer Graph, or implemented Anim Interface Graph
	UPROPERTY()
	TMap<FName, FGraphAssetPlayerInformation> GraphNameAssetPlayers;

	// Array of sync group names in the order that they are requested during compile
	UPROPERTY()
	TArray<FName> SyncGroupNames;

	// The default handler for graph-exposed inputs
	UPROPERTY()
	TArray<FExposedValueHandler> EvaluateGraphExposedInputs;

	// Per layer graph blending options
	UPROPERTY()
	TMap<FName, FAnimGraphBlendOptions> GraphBlendOptions;

public:
	// IAnimClassInterface interface
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const override { return BakedStateMachines; }
	virtual USkeleton* GetTargetSkeleton() const override { return TargetSkeleton; }
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies() const override { return AnimNotifies; }
	virtual const TArray<FAnimBlueprintFunction>& GetAnimBlueprintFunctions() const override { return AnimBlueprintFunctions; }
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap() const override { return OrderedSavedPoseIndicesMap; }
	virtual const TArray<FStructPropertyPath>& GetAnimNodeProperties() const override { return AnimNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetLinkedAnimGraphNodeProperties() const override { return LinkedAnimGraphNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetLinkedAnimLayerNodeProperties() const override { return LinkedAnimLayerNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetPreUpdateNodeProperties() const override { return PreUpdateNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetDynamicResetNodeProperties() const override { return DynamicResetNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetStateMachineNodeProperties() const override { return StateMachineNodeProperties; }
	virtual const TArray<FStructPropertyPath>& GetInitializationNodeProperties() const override { return InitializationNodeProperties; }
	virtual const TArray<FName>& GetSyncGroupNames() const override { return SyncGroupNames; }
	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const override { return SyncGroupNames.IndexOfByKey(SyncGroupName); }
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const { return EvaluateGraphExposedInputs; }
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const { return GraphNameAssetPlayers; }
	virtual const TMap<FName, FAnimGraphBlendOptions>& GetGraphBlendOptions() const { return GraphBlendOptions; }

#if WITH_EDITOR
	void CopyFrom(IAnimClassInterface* AnimClass)
	{
		check(AnimClass);
		BakedStateMachines = AnimClass->GetBakedStateMachines();
		TargetSkeleton = AnimClass->GetTargetSkeleton();
		AnimNotifies = AnimClass->GetAnimNotifies();
		AnimBlueprintFunctions = AnimClass->GetAnimBlueprintFunctions();
		OrderedSavedPoseIndicesMap = AnimClass->GetOrderedSavedPoseNodeIndicesMap();
		AnimNodeProperties = AnimClass->GetAnimNodeProperties();
		LinkedAnimGraphNodeProperties = AnimClass->GetLinkedAnimGraphNodeProperties();
		LinkedAnimLayerNodeProperties = AnimClass->GetLinkedAnimLayerNodeProperties();
		PreUpdateNodeProperties = AnimClass->GetPreUpdateNodeProperties();
		DynamicResetNodeProperties = AnimClass->GetDynamicResetNodeProperties();
		StateMachineNodeProperties = AnimClass->GetStateMachineNodeProperties();
		InitializationNodeProperties = AnimClass->GetInitializationNodeProperties();
		SyncGroupNames = AnimClass->GetSyncGroupNames();
		EvaluateGraphExposedInputs = AnimClass->GetExposedValueHandlers();
		GraphNameAssetPlayers = AnimClass->GetGraphAssetPlayerInformation();
		GraphBlendOptions = AnimClass->GetGraphBlendOptions();
	}
#endif // WITH_EDITOR
};
