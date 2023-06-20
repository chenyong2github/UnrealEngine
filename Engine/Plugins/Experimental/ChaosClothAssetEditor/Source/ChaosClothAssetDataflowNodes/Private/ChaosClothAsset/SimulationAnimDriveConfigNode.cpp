// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationAnimDriveConfigNode.h"
#include "ChaosClothAsset/SimulationBaseConfigNodePrivate.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationAnimDriveConfigNode)

FChaosClothAssetSimulationAnimDriveConfigNode::FChaosClothAssetSimulationAnimDriveConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FChaosClothAssetSimulationBaseConfigNode(InParam, InGuid)
{
	RegisterCollectionConnections();
}

void FChaosClothAssetSimulationAnimDriveConfigNode::AddProperties(::Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTYWEIGHTED(AnimDriveStiffness);
	UE_CHAOS_CLOTHASSET_SIMULATIONCONFIG_SETPROPERTYWEIGHTED(AnimDriveDamping);
}
