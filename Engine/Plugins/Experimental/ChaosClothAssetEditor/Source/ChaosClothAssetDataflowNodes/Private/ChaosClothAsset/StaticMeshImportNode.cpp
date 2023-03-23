// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/StaticMeshImportNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "Engine/StaticMesh.h"
#include "MeshDescriptionToDynamicMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StaticMeshImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetStaticMeshImportNode"

FChaosClothAssetStaticMeshImportNode::FChaosClothAssetStaticMeshImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetStaticMeshImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::DataflowNodes;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{

		if (StaticMesh)
		{
			// Evaluate in collection
			const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
			FCollectionClothFacade ClothFacade(ClothCollection);
			ClothFacade.DefineSchema();

			const int32 NumLods = StaticMesh->GetNumSourceModels();
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LodIndex);
				check(MeshDescription);

				FMeshDescriptionToDynamicMesh Converter;
				Converter.bPrintDebugMessages = true; // TODO: testing only
				Converter.bEnableOutputGroups = false;
				Converter.bVIDsFromNonManifoldMeshDescriptionAttr = true;
				UE::Geometry::FDynamicMesh3 DynamicMesh;

				Converter.Convert(MeshDescription, DynamicMesh);

				FCollectionClothLodFacade ClothLodFacade = ClothFacade.AddGetLod();
				ClothLodFacade.Initialize(DynamicMesh, UVChannel);
			}
			// Make sure that whatever happens there is always at least one empty LOD to avoid crashing the render data
			if (ClothFacade.GetNumLods() < 1)
			{
				ClothFacade.AddLod();
			}
			SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
		}


	}
}

#undef LOCTEXT_NAMESPACE
