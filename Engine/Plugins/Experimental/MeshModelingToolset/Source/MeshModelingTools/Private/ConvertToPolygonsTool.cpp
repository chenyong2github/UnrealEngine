// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConvertToPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MeshRegionBoundaryLoops.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"


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
	AddToolPropertySource(Settings);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	ConvertModeWatcher.Initialize(
		[this]() { return Settings->ConversionMode; },
		[this](EConvertToPolygonsMode NewMode) { bPolygonsValid = false; }, Settings->ConversionMode);

	UpdatePolygons();
}


void UConvertToPolygonsTool::Shutdown(EToolShutdownType ShutdownType)
{
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ComponentTarget->SetOwnerVisibility(true);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ConvertToPolygonsToolTransactionName", "Convert to Polygons"));
		ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			ConvertToPolygons(CommitParams.MeshDescription);
		});
		GetToolManager()->EndUndoTransaction();
	}
}


void UConvertToPolygonsTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	bPolygonsValid = false;
	GetToolManager()->PostInvalidation();
}


void UConvertToPolygonsTool::Tick(float DeltaTime)
{
	ConvertModeWatcher.CheckAndUpdate();

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
		FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

		TArray<int>& Edges = Polygons.PolygonEdges;
		for (int eid : Edges)
		{
			FVector3d A, B;
			Polygons.Mesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
				LineColor, 0, 2.0, 1.0f, true);
		}
	}
}


bool UConvertToPolygonsTool::HasAccept() const
{
	return true;
}

bool UConvertToPolygonsTool::CanAccept() const
{
	return true;
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
		FText::Format(LOCTEXT("UpdatePolygonsMessage", "ConvertToPolygons - found {0} polys in {1} triangles"),
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

		PreviewMesh->UpdatePreview(&SearchMesh);
		PreviewMesh->SetVisible(true);
		ComponentTarget->SetOwnerVisibility(false);
	}
	else
	{
		PreviewMesh->SetVisible(false);
		ComponentTarget->SetOwnerVisibility(true);
	}

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




#undef LOCTEXT_NAMESPACE
