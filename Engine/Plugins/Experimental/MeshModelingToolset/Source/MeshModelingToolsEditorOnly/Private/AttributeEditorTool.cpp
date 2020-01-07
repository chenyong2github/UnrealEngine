// Copyright Epic Games, Inc. All Rights Reserved.

#include "AttributeEditorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"
#include "MathUtil.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionOperations.h"

#include "Components/PrimitiveComponent.h"


#define LOCTEXT_NAMESPACE "UAttributeEditorTool"


/*
 * ToolBuilder
 */


bool UAttributeEditorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UAttributeEditorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UAttributeEditorTool* NewTool = NewObject<UAttributeEditorTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */

 // UObject interface
#if WITH_EDITOR
bool
UAttributeEditorToolProperties::CanEditChange( const FProperty* InProperty) const
{
	static TArray<FString> UVLayers
	{
		TEXT("bClearUVLayer0"),
		TEXT("bClearUVLayer1"),
		TEXT("bClearUVLayer2"),
		TEXT("bClearUVLayer3"),
		TEXT("bClearUVLayer4"),
		TEXT("bClearUVLayer5"),
		TEXT("bClearUVLayer6"),
		TEXT("bClearUVLayer7")
	};

	int32 Index;
	if (UVLayers.Find(InProperty->GetFName().ToString(), Index))
	{
		return Index < NumUVLayers;
	}
	else
	{
		return true;
	}
}
#endif // WITH_EDITOR	
// End of UObject interface


UAttributeEditorTool::UAttributeEditorTool()
{
}

void UAttributeEditorTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UAttributeEditorTool::Setup()
{
	UInteractiveTool::Setup();

	int NumUVLayers = 1;
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		FMeshDescription* MeshDescription = ComponentTarget->GetMesh();
		TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
			MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		NumUVLayers = FMath::Max(NumUVLayers, InstanceUVs.GetNumIndices());
	}

	RemovalProperties = NewObject<UAttributeEditorToolProperties>(this);
	RemovalProperties->NumUVLayers = NumUVLayers;
	AddToolPropertySource(RemovalProperties);
}




void UAttributeEditorTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAssets();
	}
}

void UAttributeEditorTool::GenerateAssets()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AttributeEditorToolTransactionName", "Attribute Editor Tool Transaction"));

	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->CommitMesh([this](FMeshDescription* MeshDescription)
		{
			if (RemovalProperties->bClearNormals)
			{
				TEdgeAttributesRef<bool> EdgeHardnesses = MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
				if (EdgeHardnesses.IsValid())
				{
					for (FEdgeID ElID : MeshDescription->Edges().GetElementIDs())
					{
						EdgeHardnesses[ElID] = false;
					}
				}
				FMeshDescriptionOperations::CreatePolygonNTB(*MeshDescription, FMathf::Epsilon);
				FMeshDescriptionOperations::RecomputeNormalsAndTangentsIfNeeded(*MeshDescription, FMeshDescriptionOperations::ETangentOptions::UseWeightedAreaAndAngle, true);
			}
			bool RemoveLayers[8] =
			{
				RemovalProperties->bClearUVLayer0,
				RemovalProperties->bClearUVLayer1,
				RemovalProperties->bClearUVLayer2,
				RemovalProperties->bClearUVLayer3,
				RemovalProperties->bClearUVLayer4,
				RemovalProperties->bClearUVLayer5,
				RemovalProperties->bClearUVLayer6,
				RemovalProperties->bClearUVLayer7
			};
			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			const FVertexInstanceArray& Instances = MeshDescription->VertexInstances();
			for (int LayerIndex = 7; LayerIndex >= 0; LayerIndex--)
			{
				if (RemoveLayers[LayerIndex] && LayerIndex < InstanceUVs.GetNumIndices())
				{
					if (!FMeshDescriptionOperations::RemoveUVChannel(*MeshDescription, LayerIndex))
					{
						for (const FVertexInstanceID& ElID : Instances.GetElementIDs())
						{
							InstanceUVs.Set(ElID, LayerIndex, FVector2D::ZeroVector);
						}
					}
				}
			}
		});
	}

	GetToolManager()->EndUndoTransaction();
}



void UAttributeEditorTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void UAttributeEditorTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UAttributeEditorTool::Tick(float DeltaTime)
{
}


void UAttributeEditorTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
}



bool UAttributeEditorTool::HasAccept() const
{
	return true;
}

bool UAttributeEditorTool::CanAccept() const
{
	return true;
}





#undef LOCTEXT_NAMESPACE
