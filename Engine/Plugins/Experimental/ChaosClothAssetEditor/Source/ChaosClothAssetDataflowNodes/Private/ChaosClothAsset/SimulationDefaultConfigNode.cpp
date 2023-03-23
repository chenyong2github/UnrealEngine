// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationDefaultConfigNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationDefaultConfigNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSimulationDefaultConfigNode"

FChaosClothAssetSimulationDefaultConfigNode::FChaosClothAssetSimulationDefaultConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
	, SimulationConfig(NewObject<UChaosClothConfig>())
	, SharedSimulationConfig(NewObject<UChaosClothSharedSimConfig>())
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetSimulationDefaultConfigNode::Serialize(FArchive& Ar)
{
	if (!SimulationConfig)
	{
		SimulationConfig = NewObject<UChaosClothConfig>();
	}
	SimulationConfig->Serialize(Ar);

	if (!SharedSimulationConfig)
	{
		SharedSimulationConfig = NewObject<UChaosClothSharedSimConfig>();
	}
	SharedSimulationConfig->Serialize(Ar);
}

void FChaosClothAssetSimulationDefaultConfigNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace Chaos;
	using namespace Chaos::Softs;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FClothingSimulationConfig ClothingSimulationConfig;
		ClothingSimulationConfig.Initialize(SimulationConfig.Get(), SharedSimulationConfig.Get());

		ClothingSimulationConfig.GetPropertyCollection()->CopyTo(&InCollection);

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}

void FChaosClothAssetSimulationDefaultConfigNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SimulationConfig);
	Collector.AddReferencedObject(SharedSimulationConfig);
}

FString FChaosClothAssetSimulationDefaultConfigNode::GetReferencerName() const
{
	return TEXT("FChaosClothAssetSimulationDefaultConfigNode");
}

#undef LOCTEXT_NAMESPACE
