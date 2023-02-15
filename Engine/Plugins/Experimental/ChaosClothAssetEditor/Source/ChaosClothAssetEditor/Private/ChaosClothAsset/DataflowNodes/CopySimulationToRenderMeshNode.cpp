// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/CopySimulationToRenderMeshNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "Materials/Material.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CopySimulationToRenderMeshNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetCopySimulationToRenderMeshNode"

FChaosClothAssetCopySimulationToRenderMeshNode::FChaosClothAssetCopySimulationToRenderMeshNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&Patterns);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetCopySimulationToRenderMeshNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		using namespace UE::Chaos::ClothAsset;

		// Evaluate in collection
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		// TODO: Needs to make cloth collection a facade to avoid the copy of the cloth collection to a managed array
		//       and remove the above const reference to operated on the InCollection instead.
		TSharedPtr<FClothCollection> ClothCollection = MakeShared<FClothCollection>();
		InCollection.CopyTo(ClothCollection.Get());

		// Empty mesh and materials
		FClothGeometryTools::DeleteRenderMesh(ClothCollection);

		// Find the material asset path
		const FString MaterialPathName = Material ? 
			Material->GetPathName() :
			FString(TEXT("/Engine/EditorMaterials/Cloth/CameraLitDoubleSided.CameraLitDoubleSided"));

		// Find or add the material attribute
		// TODO: Add to ClothAdapter functionality
		TManagedArray<FString>* const FindAttributeResult = ClothCollection->FindAttribute<FString>("MaterialPathName", FClothCollection::MaterialsGroup);
		TManagedArray<FString>& MaterialPathNameArray = FindAttributeResult ? *FindAttributeResult :
			ClothCollection->AddAttribute<FString>("MaterialPathName", FClothCollection::MaterialsGroup);

		// Work out the material index and set the path name
		int32 MaterialIndex = MaterialPathNameArray.Find(MaterialPathName);
	
		if (MaterialIndex == INDEX_NONE)
		{
			MaterialIndex = ClothCollection->AddElements(1, FClothCollection::MaterialsGroup);
			MaterialPathNameArray[MaterialIndex] = MaterialPathName;
		}

		FClothGeometryTools::CopySimMeshToRenderMesh(ClothCollection, MaterialIndex);

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
