// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/BindToRootBoneNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/ClothGeometryTools.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BindToRootBoneNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetBindToRootBoneNode"

FChaosClothAssetBindToRootBoneNode::FChaosClothAssetBindToRootBoneNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetBindToRootBoneNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		using namespace UE::Chaos::ClothAsset;

		// Evaluate in collection
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		// TODO: Needs to make cloth collection a facade to avoid the copy of the cloth collection to a managed array
		//       and remove the above const reference to operate on the InCollection instead.
		TSharedPtr<FClothCollection> ClothCollection = MakeShared<FClothCollection>();
		InCollection.CopyTo(ClothCollection.Get());

        FClothGeometryTools::BindMeshToRootBone(ClothCollection, bBindSimMesh, bBindRenderMesh);

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
