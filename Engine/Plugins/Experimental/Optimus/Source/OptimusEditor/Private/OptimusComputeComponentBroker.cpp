// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusComputeComponentBroker.h"

#include "ComputeFramework/ComputeGraphComponent.h"
#include "ComputeFramework/ComputeGraph.h"

UClass* FOptimusComputeComponentBroker::GetSupportedAssetClass()
{
	return UComputeGraph::StaticClass();
}


bool FOptimusComputeComponentBroker::AssignAssetToComponent(UActorComponent* InComponent, UObject* InAsset)
{
	if (UComputeGraphComponent* Component = Cast<UComputeGraphComponent>(InComponent))
	{
		if (UComputeGraph *ComputeGraph = Cast<UComputeGraph>(InAsset))
		{
			Component->ComputeGraph = ComputeGraph;
			Component->CreateDataProviders(true);
			return true;
		}
	}
	return false;	
}


UObject* FOptimusComputeComponentBroker::GetAssetFromComponent(UActorComponent* InComponent)
{
	if (UComputeGraphComponent* Component = Cast<UComputeGraphComponent>(InComponent))
	{
		return Component->ComputeGraph;
	}
	
	return nullptr;
}
