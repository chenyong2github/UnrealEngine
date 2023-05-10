// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "SimulationBaseConfigNode.generated.h"

namespace Chaos::Softs
{
	class FCollectionPropertyMutableFacade;
}

/**
 * Base abstract class for all cloth asset config nodes.
 * Inherited class must call RegisterCollectionConnections() in constructor to use this base class Collection.
 */
USTRUCT(meta = (Abstract))
struct FChaosClothAssetSimulationBaseConfigNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	FChaosClothAssetSimulationBaseConfigNode() = default;

	FChaosClothAssetSimulationBaseConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

protected:
	virtual void AddProperties(::Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
	PURE_VIRTUAL(FChaosClothAssetSimulationBaseConfigNode::AddProperties, );

	void RegisterCollectionConnections();

	int32 AddPropertyHelper(
		::Chaos::Softs::FCollectionPropertyMutableFacade& Properties,
		const FName& PropertyName,
		bool bIsAnimatable = true,
		const TArray<FName>& SimilarPropertyNames = TArray<FName>()) const;
};
