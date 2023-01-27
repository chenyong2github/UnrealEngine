// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshActorFactoryNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeMeshActorFactoryNode)

#if WITH_ENGINE
	#include "GameFramework/Actor.h"
#endif

UInterchangeMeshActorFactoryNode::UInterchangeMeshActorFactoryNode()
{
	SlotMaterialDependencies.Initialize(Attributes.ToSharedRef(), TEXT("__SlotMaterialDependencies__"));
	MorphTargetCurveWeights.Initialize(Attributes.ToSharedRef(), TEXT("__MorphTargetCurves__Key"));
}

void UInterchangeMeshActorFactoryNode::GetSlotMaterialDependencies(TMap<FString, FString>& OutMaterialDependencies) const
{
	OutMaterialDependencies = SlotMaterialDependencies.ToMap();
}

bool UInterchangeMeshActorFactoryNode::GetSlotMaterialDependencyUid(const FString& SlotName, FString& OutMaterialDependency) const
{
	return SlotMaterialDependencies.GetValue(SlotName, OutMaterialDependency);
}

bool UInterchangeMeshActorFactoryNode::SetSlotMaterialDependencyUid(const FString& SlotName, const FString& MaterialDependencyUid)
{
	return SlotMaterialDependencies.SetKeyValue(SlotName, MaterialDependencyUid);
}

bool UInterchangeMeshActorFactoryNode::RemoveSlotMaterialDependencyUid(const FString& SlotName)
{
	return SlotMaterialDependencies.RemoveKey(SlotName);
}

bool UInterchangeMeshActorFactoryNode::SetMorphTargetCurveWeight(const FString& MorphTargetName, const float& Weight)
{
	return MorphTargetCurveWeights.SetKeyValue(MorphTargetName, Weight);
}

void UInterchangeMeshActorFactoryNode::SetMorphTargetCurveWeights(const TMap<FString, float>& InMorphTargetCurveWeights)
{
	for (const TPair<FString, float>& MorphTargetCurve : InMorphTargetCurveWeights)
	{
		SetMorphTargetCurveWeight(MorphTargetCurve.Key, MorphTargetCurve.Value);
	}
}

void UInterchangeMeshActorFactoryNode::GetMorphTargetCurveWeights(TMap<FString, float>& OutMorphTargetCurveWeights) const
{
	OutMorphTargetCurveWeights = MorphTargetCurveWeights.ToMap();
}