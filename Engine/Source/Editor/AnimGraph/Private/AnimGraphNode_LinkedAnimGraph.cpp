// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LinkedAnimGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"

void UAnimGraphNode_LinkedAnimGraph::PostPasteNode()
{
	// Clear incompatible target class
	if(UClass* InstanceClass = GetTargetClass())
	{
		if(UAnimBlueprint* LinkedBlueprint = Cast<UAnimBlueprint>(UBlueprint::GetBlueprintFromClass(InstanceClass)))
		{
			if(UAnimBlueprint* ThisBlueprint = GetAnimBlueprint())
			{
				if(LinkedBlueprint->TargetSkeleton != ThisBlueprint->TargetSkeleton)
				{
					Node.InstanceClass = nullptr;
				}
			}
		}
	}
}