// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvertToPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MeshRegionBoundaryLoops.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface

#define LOCTEXT_NAMESPACE "UConvertToPolygonsTool"


/*
 * ToolBuilder
 */


bool UConvertToPolygonsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UConvertToPolygonsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UConvertToPolygonsTool* NewTool = NewObject<UConvertToPolygonsTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}

/*
 * Tool
 */
UConvertToPolygonsTool::UConvertToPolygonsTool()
{
	SetToolDisplayName(LOCTEXT("ConvertToPolygonsToolName", "Find PolyGroups Tool"));
}

void UConvertToPolygonsTool::Setup()
{
	UInteractiveTool::Setup();

	FMeshDescription* MeshDescription = nullptr;
	MeshDescription = ComponentTarget->GetMesh();

	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(MeshDescription, SearchMesh);

	if (SearchMesh.HasAttributes())
	{
		InitialNormals.Copy( *SearchMesh.Attributes()->PrimaryNormals() );
	}

	Settings = NewObject<UConvertToPolygonsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	Settings->WatchProperty(Settings->ConversionMode,
							[this](EConvertToPolygonsMode NewMode)
							{ bPolygonsValid = false; });
	Settings->WatchProperty(Settings->bShowGroupColors,
							[this](bool bNewValue) { UpdateVisualization(); });
	if (Settings->bShowGroupColors)
	{
		UpdateVisualization();
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Cluster triangles of the Mesh into PolyGroups using various strategies"),
		EToolMessageLevel::UserNotification);

	UpdatePolygons();
}


void UConvertToPolygonsTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ComponentTarget->SetOwnerVisibility(true);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ConvertToPolygonsToolTransactionName", "Find Polygroups"));
		ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			ConvertToPolygons(CommitParams.MeshDescription);
		});
		GetToolManager()->EndUndoTransaction();
	}
}


void UConvertToPolygonsTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	bPolygonsValid = false;
	GetToolManager()->PostInvalidation();
}


void UConvertToPolygonsTool::OnTick(float DeltaTime)
{
	if (bPolygonsValid == false)
	{
		UpdatePolygons();
	}
}

void UConvertToPolygonsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FColor LineColor(255, 0, 0);

	if (true)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		float PDIScale = RenderAPI->GetCameraState().GetPDIScalingFactor();
		FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

		TArray<int>& Edges = Polygons.PolygonEdges;
		for (int eid : Edges)
		{
			FVector3d A, B;
			Polygons.Mesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
				LineColor, 0, 2.0*PDIScale, 1.0f, true);
		}
	}
}


void UConvertToPolygonsTool::UpdatePolygons()
{
	Polygons = FFindPolygonsAlgorithm(&SearchMesh);
	if (Settings->ConversionMode == EConvertToPolygonsMode::FromUVISlands)
	{
		Polygons.FindPolygonsFromUVIslands();
	}
	else if (Settings->ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation)
	{
		double DotTolerance = 1.0 - FMathd::Cos(Settings->AngleTolerance * FMathd::DegToRad);
		Polygons.FindPolygonsFromFaceNormals(DotTolerance);
	}
	else
	{
		check(false);
	}
	
	Polygons.FindPolygonEdges();

	GetToolManager()->DisplayMessage(
		FText::Format(LOCTEXT("UpdatePolygonsMessage", "Found {0} Polygroups in {1} Triangles"),
			FText::AsNumber(Polygons.FoundPolygons.Num()), FText::AsNumber(SearchMesh.TriangleCount())), EToolMessageLevel::Internal);

	if (Settings->bCalculateNormals)
	{
		if (SearchMesh.HasAttributes() == false)
		{
			SearchMesh.EnableAttributes();
		}

		FDynamicMeshNormalOverlay* NormalOverlay = SearchMesh.Attributes()->PrimaryNormals();
		NormalOverlay->ClearElements();

		FDynamicMeshEditor Editor(&SearchMesh);
		for (const TArray<int>& Polygon : Polygons.FoundPolygons)
		{
			FVector3f Normal = (FVector3f)SearchMesh.GetTriNormal(Polygon[0]);
			Editor.SetTriangleNormals(Polygon, Normal);
		}

		FMeshNormals Normals(&SearchMesh);
		Normals.RecomputeOverlayNormals(SearchMesh.Attributes()->PrimaryNormals());
		Normals.CopyToOverlay(NormalOverlay, false);
	}

	PreviewMesh->UpdatePreview(&SearchMesh);
	PreviewMesh->SetVisible(true);
	ComponentTarget->SetOwnerVisibility(false);

	bPolygonsValid = true;
}



void UConvertToPolygonsTool::ConvertToPolygons(FMeshDescription* MeshIn)
{
	// restore input normals
	if (Settings->bCalculateNormals == false)
	{
		SearchMesh.Attributes()->PrimaryNormals()->Copy(InitialNormals);
	}

	FDynamicMeshToMeshDescription Converter;
	Converter.ConversionOptions.bSetPolyGroups = true;
	Converter.Convert(&SearchMesh, *MeshIn);
}



void UConvertToPolygonsTool::UpdateVisualization()
{
	if (Settings->bShowGroupColors)
	{
		PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
		PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
		{
			return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
		}, 
		UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	else
	{
		PreviewMesh->ClearOverrideRenderMaterial();
		PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
}



#undef LOCTEXT_NAMESPACE
