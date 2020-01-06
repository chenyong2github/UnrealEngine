// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshInspectorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"

#include "SimpleDynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Properties/MeshStatisticsProperties.h"
#include "Properties/MeshAnalysisProperties.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface

#define LOCTEXT_NAMESPACE "UMeshInspectorTool"


/*
 * ToolBuilder
 */


bool UMeshInspectorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UMeshInspectorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshInspectorTool* NewTool = NewObject<UMeshInspectorTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}



/*
 * Properties
 */

void UMeshInspectorProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UMeshInspectorProperties* PropertyCache = GetPropertyCache<UMeshInspectorProperties>();
	PropertyCache->bWireframe = this->bWireframe;
	PropertyCache->bBoundaryEdges = this->bBoundaryEdges;
	PropertyCache->bPolygonBorders = this->bPolygonBorders;
	PropertyCache->bUVSeams = this->bUVSeams;
	PropertyCache->bNormalSeams = this->bNormalSeams;
	PropertyCache->bNormalVectors = this->bNormalVectors;
	PropertyCache->bTangentVectors = this->bTangentVectors;
	PropertyCache->NormalLength = this->NormalLength;
	PropertyCache->TangentLength = this->TangentLength;
}

void UMeshInspectorProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UMeshInspectorProperties* PropertyCache = GetPropertyCache<UMeshInspectorProperties>();
	this->bWireframe = PropertyCache->bWireframe;
	this->bBoundaryEdges = PropertyCache->bBoundaryEdges;
	this->bPolygonBorders = PropertyCache->bPolygonBorders;
	this->bUVSeams = PropertyCache->bUVSeams;
	this->bNormalSeams = PropertyCache->bNormalSeams;
	this->bNormalVectors = PropertyCache->bNormalVectors;
	this->bTangentVectors = PropertyCache->bTangentVectors;
	this->NormalLength = PropertyCache->NormalLength;
	this->TangentLength = PropertyCache->TangentLength;
}




/*
 * Tool
 */

UMeshInspectorTool::UMeshInspectorTool()
{
}

void UMeshInspectorTool::Setup()
{
	UInteractiveTool::Setup();

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);
	DefaultMaterial = PreviewMesh->GetMaterial(0);

	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::ExternallyCalculated);
	PreviewMesh->InitializeMesh(ComponentTarget->GetMesh());

	Precompute();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	Settings = NewObject<UMeshInspectorProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->Setup();
	AddToolPropertySource(MaterialSettings);

	PreviewMesh->EnableWireframe(Settings->bWireframe);

	UMeshStatisticsProperties* Statistics = NewObject<UMeshStatisticsProperties>(this);
	Statistics->Update(*PreviewMesh->GetPreviewDynamicMesh());
	AddToolPropertySource(Statistics);

	UMeshAnalysisProperties* MeshAnalysis = NewObject<UMeshAnalysisProperties>(this);
	MeshAnalysis->Update(*PreviewMesh->GetPreviewDynamicMesh(), ComponentTarget->GetWorldTransform());
	AddToolPropertySource(MeshAnalysis);
}


void UMeshInspectorTool::Shutdown(EToolShutdownType ShutdownType)
{
	ComponentTarget->SetOwnerVisibility(true);

	if (PreviewMesh != nullptr)
	{
		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

	Settings->SaveProperties(this);
}


void UMeshInspectorTool::Precompute()
{
	BoundaryEdges.Reset();
	UVSeamEdges.Reset();
	NormalSeamEdges.Reset();

	const FDynamicMesh3* TargetMesh = PreviewMesh->GetPreviewDynamicMesh();
	const FDynamicMeshUVOverlay* UVOverlay =
		TargetMesh->HasAttributes() ? TargetMesh->Attributes()->PrimaryUV() : nullptr;
	const FDynamicMeshNormalOverlay* NormalOverlay =
		TargetMesh->HasAttributes() ? TargetMesh->Attributes()->PrimaryNormals() : nullptr;

	for (int eid : TargetMesh->EdgeIndicesItr())
	{
		if (TargetMesh->IsBoundaryEdge(eid))
		{
			BoundaryEdges.Add(eid);
		}
		if (UVOverlay != nullptr && UVOverlay->IsSeamEdge(eid))
		{
			UVSeamEdges.Add(eid);
		}
		if (NormalOverlay != nullptr && NormalOverlay->IsSeamEdge(eid))
		{
			NormalSeamEdges.Add(eid);
		}
		if (TargetMesh->IsGroupBoundaryEdge(eid))
		{
			GroupBoundaryEdges.Add(eid);
		}
	}
}


void UMeshInspectorTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform3d Transform(ComponentTarget->GetWorldTransform());

	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = LineWidthMultiplier * 4.0;
	FColor UVSeamColor(15, 240, 15);
	float UVSeamThickness = LineWidthMultiplier * 2.0;
	FColor NormalSeamColor(15, 240, 240);
	float NormalSeamThickness = LineWidthMultiplier * 2.0;
	FColor PolygonBorderColor(240, 15, 240);
	float PolygonBorderThickness = LineWidthMultiplier * 2.0;
	FColor NormalColor(15, 15, 240);
	float NormalThickness = LineWidthMultiplier * 2.0f;
	FColor TangentColor(240, 15, 15);
	FColor BinormalColor(15, 240, 15);
	float TangentThickness = LineWidthMultiplier * 2.0f;

	const FDynamicMesh3* TargetMesh = PreviewMesh->GetPreviewDynamicMesh();
	FVector3d A, B;

	if (Settings->bBoundaryEdges)
	{
		for (int eid : BoundaryEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine((FVector)Transform.TransformPosition(A), (FVector)Transform.TransformPosition(B),
				BoundaryEdgeColor, 0, BoundaryEdgeThickness, 2.0f, true);
		}
	}

	if (Settings->bUVSeams)
	{
		for (int eid : UVSeamEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine((FVector)Transform.TransformPosition(A), (FVector)Transform.TransformPosition(B),
				UVSeamColor, 0, UVSeamThickness, 3.0f, true);
		}
	}

	if (Settings->bNormalSeams)
	{
		for (int eid : NormalSeamEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine((FVector)Transform.TransformPosition(A), (FVector)Transform.TransformPosition(B),
				NormalSeamColor, 0, NormalSeamThickness, 3.0f, true);
		}
	}

	if (Settings->bPolygonBorders)
	{
		for (int eid : GroupBoundaryEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine((FVector)Transform.TransformPosition(A), (FVector)Transform.TransformPosition(B),
				PolygonBorderColor, 0, PolygonBorderThickness, 2.0f, true);
		}
	}

	if (Settings->bNormalVectors && TargetMesh->HasAttributes() && TargetMesh->Attributes()->PrimaryNormals() != nullptr)
	{
		const FDynamicMeshNormalOverlay* NormalOverlay = TargetMesh->Attributes()->PrimaryNormals();
		FVector3d TriV[3];
		FVector3f TriN[3];
		for (int tid : TargetMesh->TriangleIndicesItr())
		{
			TargetMesh->GetTriVertices(tid, TriV[0], TriV[1], TriV[2]);
			NormalOverlay->GetTriElements(tid, TriN[0], TriN[1], TriN[2]);
			for (int j = 0; j < 3; ++j)
			{
				TriV[j] = Transform.TransformPosition(TriV[j]);
				PDI->DrawLine((FVector)TriV[j], (FVector)((FVector3f)TriV[j] + Settings->NormalLength * Transform.TransformVectorNoScale(TriN[j])),
					NormalColor, SDPG_World, NormalThickness, 0.0f, true);
			}
		}
	}

	if (Settings->bTangentVectors && PreviewMesh->GetTangents() != nullptr)
	{
		const FMeshTangentsf* Tangents = PreviewMesh->GetTangents();
		for (int TID : TargetMesh->TriangleIndicesItr())
		{
			FVector3d TriV[3];
			TargetMesh->GetTriVertices(TID, TriV[0], TriV[1], TriV[2]);
			for (int SubIdx = 0; SubIdx < 3; SubIdx++)
			{
				FVector3f Vert(Transform.TransformPosition(TriV[SubIdx]));
				FVector3f Tangent, Bitangent;
				Tangents->GetPerTriangleTangent(TID, SubIdx, Tangent, Bitangent);
				PDI->DrawLine((FVector)Vert, (FVector)(Vert + Settings->TangentLength * Transform.TransformVectorNoScale(Tangent)),
					TangentColor, SDPG_World, TangentThickness, 3.5f, true);
				PDI->DrawLine((FVector)Vert, (FVector)(Vert + Settings->TangentLength * Transform.TransformVectorNoScale(Bitangent)),
					BinormalColor, SDPG_World, TangentThickness, 3.5f, true);
			}
		}
	}


}


void UMeshInspectorTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	GetToolManager()->PostInvalidation();
	PreviewMesh->EnableWireframe(Settings->bWireframe);
	
	MaterialSettings->UpdateMaterials();
	UMaterialInterface* OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	if (OverrideMaterial == nullptr)
	{
		PreviewMesh->ClearOverrideRenderMaterial();
	}
	else
	{
		PreviewMesh->SetOverrideRenderMaterial(OverrideMaterial);
	}
}


bool UMeshInspectorTool::HasAccept() const
{
	return false;
}

bool UMeshInspectorTool::CanAccept() const
{
	return false;
}



void UMeshInspectorTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("IncreaseLineWidth"), 
		LOCTEXT("IncreaseLineWidth", "Increase Line Width"),
		LOCTEXT("IncreaseLineWidthTooltip", "Increase line width of rendering"),
		EModifierKey::Shift, EKeys::Equals,
		[this]() { IncreaseLineWidthAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("DecreaseLineWidth"), 
		LOCTEXT("DecreaseLineWidth", "Decrease Line Width"),
		LOCTEXT("DecreaseLineWidthTooltip", "Decrease line width of rendering"),
		EModifierKey::None, EKeys::Equals,
		[this]() { DecreaseLineWidthAction(); });
}



void UMeshInspectorTool::IncreaseLineWidthAction()
{
	LineWidthMultiplier = LineWidthMultiplier * 1.25f;
	GetToolManager()->PostInvalidation();
}

void UMeshInspectorTool::DecreaseLineWidthAction()
{
	LineWidthMultiplier = LineWidthMultiplier * (1.0f / 1.25f);
	GetToolManager()->PostInvalidation();
}



#undef LOCTEXT_NAMESPACE
