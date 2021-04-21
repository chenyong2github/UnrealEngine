// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimClassData.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/AnimNode_LinkedInputPose.h"

void UAnimClassData::DynamicClassInitialization(UDynamicClass* InDynamicClass)
{
	auto ResolveTransform = [](const TFieldPath<FStructProperty>& InPropertyPath)
	{ 
		return InPropertyPath.Get(); 
	};

	// Copy serialized property paths to resolved paths
	Algo::Transform(AnimNodeProperties, ResolvedAnimNodeProperties, ResolveTransform);
	Algo::Transform(LinkedAnimGraphNodeProperties, ResolvedLinkedAnimGraphNodeProperties, ResolveTransform);
	Algo::Transform(LinkedAnimLayerNodeProperties, ResolvedLinkedAnimLayerNodeProperties, ResolveTransform);
	Algo::Transform(PreUpdateNodeProperties, ResolvedPreUpdateNodeProperties, ResolveTransform);
	Algo::Transform(DynamicResetNodeProperties, ResolvedDynamicResetNodeProperties, ResolveTransform);
	Algo::Transform(StateMachineNodeProperties, ResolvedStateMachineNodeProperties, ResolveTransform);
	Algo::Transform(InitializationNodeProperties, ResolvedInitializationNodeProperties, ResolveTransform);

	check(AnimBlueprintFunctions.Num() == AnimBlueprintFunctionData.Num());

	for(int32 FunctionIndex = 0; FunctionIndex < AnimBlueprintFunctions.Num(); ++FunctionIndex)
	{
		AnimBlueprintFunctions[FunctionIndex].OutputPoseNodeProperty = AnimBlueprintFunctionData[FunctionIndex].OutputPoseNodeProperty.Get();
		Algo::Transform(AnimBlueprintFunctionData[FunctionIndex].InputProperties, AnimBlueprintFunctions[FunctionIndex].InputProperties, [](const TFieldPath<FProperty>& InPropertyPath){ return InPropertyPath.Get(); });
		Algo::Transform(AnimBlueprintFunctionData[FunctionIndex].InputPoseNodeProperties, AnimBlueprintFunctions[FunctionIndex].InputPoseNodeProperties, ResolveTransform);
	}

	// Init property access library
	PropertyAccess::PostLoadLibrary(PropertyAccessLibrary);

	// Init exposed value handlers
	FExposedValueHandler::DynamicClassInitialization(EvaluateGraphExposedInputs, InDynamicClass);
}

#if WITH_EDITOR
void UAnimClassData::CopyFrom(UAnimBlueprintGeneratedClass* AnimClass)
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
	PropertyAccessLibrary = AnimClass->GetPropertyAccessLibrary();
}
#endif // WITH_EDITOR