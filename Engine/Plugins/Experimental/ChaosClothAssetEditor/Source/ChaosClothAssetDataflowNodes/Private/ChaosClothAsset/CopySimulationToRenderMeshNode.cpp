// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CopySimulationToRenderMeshNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
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
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		// Empty mesh and materials
		FClothGeometryTools::DeleteRenderMesh(ClothCollection);

		// Find the material asset path
		const FString MaterialPathName = Material ? 
			Material->GetPathName() :
			FString(TEXT("/Engine/EditorMaterials/Cloth/CameraLitDoubleSided.CameraLitDoubleSided"));

		// Copy the mesh data
		FClothGeometryTools::CopySimMeshToRenderMesh(ClothCollection, MaterialPathName);

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
