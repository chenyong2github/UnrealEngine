// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimClassInterface.h"
#include "Algo/Transform.h"
#include "AnimClassData.generated.h"

class USkeleton;

/** Serialized anim BP function data */
USTRUCT()
struct FAnimBlueprintFunctionData
{
	GENERATED_BODY()

	UPROPERTY()
	TFieldPath<FStructProperty> OutputPoseNodeProperty;

	/** The properties of the input nodes, patched up during link */
	UPROPERTY()
	TArray<TFieldPath<FStructProperty>> InputPoseNodeProperties;

	/** The input properties themselves */
	UPROPERTY()
	TArray<TFieldPath<FProperty>> InputProperties;
};

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

	// Serialized function data, used to patch up transient data in AnimBlueprintFunctions
	UPROPERTY()
	TArray<FAnimBlueprintFunctionData> AnimBlueprintFunctionData;

	// The array of anim nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > AnimNodeProperties;
	TArray< FStructProperty* > ResolvedAnimNodeProperties;

	// The array of linked anim graph nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > LinkedAnimGraphNodeProperties;
	TArray< FStructProperty* > ResolvedLinkedAnimGraphNodeProperties;

	// The array of linked anim layer nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > LinkedAnimLayerNodeProperties;
	TArray< FStructProperty* > ResolvedLinkedAnimLayerNodeProperties;

	// Array of nodes that need a PreUpdate() call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > PreUpdateNodeProperties;
	TArray< FStructProperty* > ResolvedPreUpdateNodeProperties;

	// Array of nodes that need a DynamicReset() call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > DynamicResetNodeProperties;
	TArray< FStructProperty* > ResolvedDynamicResetNodeProperties;

	// Array of state machine nodes
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > StateMachineNodeProperties;
	TArray< FStructProperty* > ResolvedStateMachineNodeProperties;

	// Array of nodes that need an OnInitializeAnimInstance call
	UPROPERTY()
	TArray< TFieldPath<FStructProperty> > InitializationNodeProperties;
	TArray< FStructProperty* > ResolvedInitializationNodeProperties;

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
	virtual const TArray<FStructProperty*>& GetAnimNodeProperties() const override { return ResolvedAnimNodeProperties; }
	virtual const TArray<FStructProperty*>& GetLinkedAnimGraphNodeProperties() const override { return ResolvedLinkedAnimGraphNodeProperties; }
	virtual const TArray<FStructProperty*>& GetLinkedAnimLayerNodeProperties() const override { return ResolvedLinkedAnimLayerNodeProperties; }
	virtual const TArray<FStructProperty*>& GetPreUpdateNodeProperties() const override { return ResolvedPreUpdateNodeProperties; }
	virtual const TArray<FStructProperty*>& GetDynamicResetNodeProperties() const override { return ResolvedDynamicResetNodeProperties; }
	virtual const TArray<FStructProperty*>& GetStateMachineNodeProperties() const override { return ResolvedStateMachineNodeProperties; }
	virtual const TArray<FStructProperty*>& GetInitializationNodeProperties() const override { return ResolvedInitializationNodeProperties; }
	virtual const TArray<FName>& GetSyncGroupNames() const override { return SyncGroupNames; }
	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const override { return SyncGroupNames.IndexOfByKey(SyncGroupName); }
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const { return EvaluateGraphExposedInputs; }
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const { return GraphNameAssetPlayers; }
	virtual const TMap<FName, FAnimGraphBlendOptions>& GetGraphBlendOptions() const { return GraphBlendOptions; }

	// Resolve TFieldPaths to FStructPropertys
	void ResolvePropertyPaths();

#if WITH_EDITOR
	void CopyFrom(IAnimClassInterface* AnimClass)
	{
		check(AnimClass);
		BakedStateMachines = AnimClass->GetBakedStateMachines();
		TargetSkeleton = AnimClass->GetTargetSkeleton();
		AnimNotifies = AnimClass->GetAnimNotifies();
		AnimBlueprintFunctions = AnimClass->GetAnimBlueprintFunctions();
		AnimBlueprintFunctionData.Empty(AnimBlueprintFunctions.Num());

		for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimBlueprintFunctions)
		{
			FAnimBlueprintFunctionData& NewAnimBlueprintFunctionData = AnimBlueprintFunctionData.AddDefaulted_GetRef();
			NewAnimBlueprintFunctionData.OutputPoseNodeProperty = AnimBlueprintFunction.OutputPoseNodeProperty;
			Algo::Transform(AnimBlueprintFunction.InputProperties, NewAnimBlueprintFunctionData.InputProperties, [](FProperty* InProperty){ return TFieldPath<FProperty>(InProperty); });
			Algo::Transform(AnimBlueprintFunction.InputPoseNodeProperties, NewAnimBlueprintFunctionData.InputPoseNodeProperties, [](FStructProperty* InProperty){ return TFieldPath<FStructProperty>(InProperty); });
		}

		OrderedSavedPoseIndicesMap = AnimClass->GetOrderedSavedPoseNodeIndicesMap();

		auto MakePropertyPath = [](FStructProperty* InProperty)
		{ 
			return TFieldPath<FStructProperty>(InProperty); 
		};

		Algo::Transform(AnimClass->GetAnimNodeProperties(), AnimNodeProperties, MakePropertyPath);
		ResolvedAnimNodeProperties = AnimClass->GetAnimNodeProperties();
		Algo::Transform(AnimClass->GetLinkedAnimGraphNodeProperties(), LinkedAnimGraphNodeProperties, MakePropertyPath);
		ResolvedLinkedAnimGraphNodeProperties = AnimClass->GetLinkedAnimGraphNodeProperties();
		Algo::Transform(AnimClass->GetLinkedAnimLayerNodeProperties(), LinkedAnimLayerNodeProperties, MakePropertyPath);
		ResolvedLinkedAnimLayerNodeProperties = AnimClass->GetLinkedAnimLayerNodeProperties();
		Algo::Transform(AnimClass->GetPreUpdateNodeProperties(), PreUpdateNodeProperties, MakePropertyPath);
		ResolvedPreUpdateNodeProperties = AnimClass->GetPreUpdateNodeProperties();
		Algo::Transform(AnimClass->GetDynamicResetNodeProperties(), DynamicResetNodeProperties, MakePropertyPath);
		ResolvedDynamicResetNodeProperties = AnimClass->GetDynamicResetNodeProperties();
		Algo::Transform(AnimClass->GetStateMachineNodeProperties(), StateMachineNodeProperties, MakePropertyPath);
		ResolvedStateMachineNodeProperties = AnimClass->GetStateMachineNodeProperties();
		Algo::Transform(AnimClass->GetInitializationNodeProperties(), InitializationNodeProperties, MakePropertyPath);
		ResolvedInitializationNodeProperties = AnimClass->GetInitializationNodeProperties();

		SyncGroupNames = AnimClass->GetSyncGroupNames();
		EvaluateGraphExposedInputs = AnimClass->GetExposedValueHandlers();
		GraphNameAssetPlayers = AnimClass->GetGraphAssetPlayerInformation();
		GraphBlendOptions = AnimClass->GetGraphBlendOptions();
	}
#endif // WITH_EDITOR
};
