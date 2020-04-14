// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimClassData.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/AnimNode_LinkedInputPose.h"

void UAnimClassData::ResolvePropertyPaths()
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
}