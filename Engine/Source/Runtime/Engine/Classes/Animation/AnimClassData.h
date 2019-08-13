// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	TArray<UStructProperty*> AnimNodeProperties;

	// The array of sub instance nodes
	UPROPERTY()
	TArray<UStructProperty*> SubInstanceNodeProperties;

	// The array of layer nodes
	UPROPERTY()
	TArray<UStructProperty*> LayerNodeProperties;

	// Indices for any Asset Player found within a specific (named) Anim Layer Graph, or implemented Anim Interface Graph
	UPROPERTY()
	TMap<FName, FGraphAssetPlayerInformation> GraphNameAssetPlayers;

	// Array of sync group names in the order that they are requested during compile
	UPROPERTY()
	TArray<FName> SyncGroupNames;

	// The default handler for graph-exposed inputs
	UPROPERTY()
	TArray<FExposedValueHandler> EvaluateGraphExposedInputs;

public:
	// IAnimClassInterface interface
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const override { return BakedStateMachines; }
	virtual USkeleton* GetTargetSkeleton() const override { return TargetSkeleton; }
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies() const override { return AnimNotifies; }
	virtual const TArray<FAnimBlueprintFunction>& GetAnimBlueprintFunctions() const override { return AnimBlueprintFunctions; }
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap() const override { return OrderedSavedPoseIndicesMap; }
	virtual const TArray<UStructProperty*>& GetAnimNodeProperties() const override { return AnimNodeProperties; }
	virtual const TArray<UStructProperty*>& GetSubInstanceNodeProperties() const override { return SubInstanceNodeProperties; }
	virtual const TArray<UStructProperty*>& GetLayerNodeProperties() const override { return LayerNodeProperties; }
	virtual const TArray<FName>& GetSyncGroupNames() const override { return SyncGroupNames; }
	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const override { return SyncGroupNames.IndexOfByKey(SyncGroupName); }
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const { return EvaluateGraphExposedInputs; }
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const { return GraphNameAssetPlayers; }

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
		SubInstanceNodeProperties = AnimClass->GetSubInstanceNodeProperties();
		LayerNodeProperties = AnimClass->GetLayerNodeProperties();
		SyncGroupNames = AnimClass->GetSyncGroupNames();
		EvaluateGraphExposedInputs = AnimClass->GetExposedValueHandlers();
		GraphNameAssetPlayers = AnimClass->GetGraphAssetPlayerInformation();
	}
#endif // WITH_EDITOR
};
