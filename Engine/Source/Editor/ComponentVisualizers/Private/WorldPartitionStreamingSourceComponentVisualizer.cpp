// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionStreamingSourceComponentVisualizer.h"
#include "SceneManagement.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"

void FWorldPartitionStreamingSourceComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (const UWorldPartitionStreamingSourceComponent* StreamingSourceComponent = Cast<const UWorldPartitionStreamingSourceComponent>(Component))
	{
		StreamingSourceComponent->DrawVisualization(View, PDI);
	}
}
