// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SetPhysicsAssetNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SetPhysicsAssetNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSetPhysicsAssetNode"

FChaosClothAssetSetPhysicsAssetNode::FChaosClothAssetSetPhysicsAssetNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&PhysicsAsset);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetSetPhysicsAssetNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		using namespace UE::Chaos::ClothAsset;

		// Evaluate in collection
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		FCollectionClothFacade CollectionClothFacade(ClothCollection);
		CollectionClothFacade.SetPhysicsAssetPathName(PhysicsAsset.GetPathName());

		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
