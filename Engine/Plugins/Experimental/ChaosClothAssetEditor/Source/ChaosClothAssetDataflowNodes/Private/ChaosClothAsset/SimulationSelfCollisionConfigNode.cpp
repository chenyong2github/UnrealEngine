// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationSelfCollisionConfigNode.h"
#include "ChaosClothAsset/SimulationBaseConfigNodePrivate.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationSelfCollisionConfigNode)

FChaosClothAssetSimulationSelfCollisionConfigNode::FChaosClothAssetSimulationSelfCollisionConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
}

void FChaosClothAssetSimulationSelfCollisionConfigNode::AddProperties(::Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTY(SelfCollisionThickness);
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTY(SelfCollisionStiffness);
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTY(SelfCollisionFriction);
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTYBOOL(UseSelfIntersections);
}
