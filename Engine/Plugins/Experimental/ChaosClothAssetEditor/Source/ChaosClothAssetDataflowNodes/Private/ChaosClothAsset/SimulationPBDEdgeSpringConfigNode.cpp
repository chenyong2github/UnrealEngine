// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationPBDEdgeSpringConfigNode.h"
#include "ChaosClothAsset/SimulationBaseConfigNodePrivate.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationPBDEdgeSpringConfigNode)

FChaosClothAssetSimulationPBDEdgeSpringConfigNode::FChaosClothAssetSimulationPBDEdgeSpringConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
}

void FChaosClothAssetSimulationPBDEdgeSpringConfigNode::AddProperties(::Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTYWEIGHTEDCHECKED2(
		EdgeSpringStiffness,
		XPBDEdgeSpringStiffness,         // Existing properties to warn against
		XPBDAnisoStretchStiffnessWarp);  //
}
