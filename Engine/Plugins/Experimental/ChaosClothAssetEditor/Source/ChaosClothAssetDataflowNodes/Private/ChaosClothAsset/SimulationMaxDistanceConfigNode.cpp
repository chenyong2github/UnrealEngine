// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationMaxDistanceConfigNode.h"
#include "ChaosClothAsset/SimulationBaseConfigNodePrivate.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationMaxDistanceConfigNode)

FChaosClothAssetSimulationMaxDistanceConfigNode::FChaosClothAssetSimulationMaxDistanceConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
}

void FChaosClothAssetSimulationMaxDistanceConfigNode::AddProperties(::Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTYWEIGHTED(MaxDistance);
}
