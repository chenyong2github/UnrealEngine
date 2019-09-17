// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshInspectorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"

#include "SimpleDynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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
 * Tool
 */

UMeshInspectorTool::UMeshInspectorTool()
{
}

void UMeshInspectorTool::Setup()
{
	UInteractiveTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// copy material if there is one
	DefaultMaterial = ComponentTarget->GetMaterial(0);
	if (DefaultMaterial != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, DefaultMaterial);
	}
	ActiveMaterialMode = EInspectorMaterialMode::Default;

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::ExternallyCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());

	Precompute();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	Settings = NewObject<UMeshInspectorProperties>(this);
	AddToolPropertySource(Settings);

	UMaterial* CheckerMaterialBase = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/CheckerMaterial"));
	if (CheckerMaterialBase != nullptr)
	{
		CheckerMaterial = UMaterialInstanceDynamic::Create(CheckerMaterialBase, NULL);
		if (CheckerMaterial != nullptr)
		{
			CheckerMaterial->SetScalarParameterValue("Density", Settings->CheckerDensity);
		}
	}
	ActiveCheckerDensity = Settings->CheckerDensity;

	DynamicMeshComponent->bExplicitShowWireframe = Settings->bWireframe;

	UMeshStatisticsProperties* Statistics = NewObject<UMeshStatisticsProperties>(this);
	Statistics->Update(*DynamicMeshComponent->GetMesh());
	AddToolPropertySource(Statistics);
}


void UMeshInspectorTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		ComponentTarget->SetOwnerVisibility(true);

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}


void UMeshInspectorTool::Precompute()
{
	BoundaryEdges.Reset();
	UVSeamEdges.Reset();
	NormalSeamEdges.Reset();

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshUVOverlay* UVOverlay =
		TargetMesh->HasAttributes() ? TargetMesh->Attributes()->PrimaryUV() : nullptr;
	FDynamicMeshNormalOverlay* NormalOverlay =
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
	FTransform Transform = ComponentTarget->GetWorldTransform();

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

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	FVector3d A, B;

	if (Settings->bBoundaryEdges)
	{
		for (int eid : BoundaryEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
				BoundaryEdgeColor, 0, BoundaryEdgeThickness, 2.0f, true);
		}
	}

	if (Settings->bUVSeams)
	{
		for (int eid : UVSeamEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
				UVSeamColor, 0, UVSeamThickness, 3.0f, true);
		}
	}

	if (Settings->bNormalSeams)
	{
		for (int eid : NormalSeamEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
				NormalSeamColor, 0, NormalSeamThickness, 3.0f, true);
		}
	}

	if (Settings->bPolygonBorders)
	{
		for (int eid : GroupBoundaryEdges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
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
				PDI->DrawLine(TriV[j], TriV[j] + Settings->NormalLength * Transform.TransformVectorNoScale(TriN[j]),
					NormalColor, SDPG_World, NormalThickness, 0.0f, true);
			}
		}
	}

	if (Settings->bTangentVectors)
	{
		const FMeshTangentsf* Tangents = DynamicMeshComponent->GetTangents();
		for (int TID : TargetMesh->TriangleIndicesItr())
		{
			FVector3d TriV[3];
			TargetMesh->GetTriVertices(TID, TriV[0], TriV[1], TriV[2]);
			for (int SubIdx = 0; SubIdx < 3; SubIdx++)
			{
				FVector Vert = Transform.TransformPosition(TriV[SubIdx]);
				FVector3f Tangent, Bitangent;
				Tangents->GetPerTriangleTangent(TID, SubIdx, Tangent, Bitangent);
				PDI->DrawLine(Vert, Vert + Settings->TangentLength * Transform.TransformVectorNoScale(Tangent),
					TangentColor, SDPG_World, TangentThickness, 3.5f, true);
				PDI->DrawLine(Vert, Vert + Settings->TangentLength * Transform.TransformVectorNoScale(Bitangent),
					BinormalColor, SDPG_World, TangentThickness, 3.5f, true);
			}
		}
	}


}


void UMeshInspectorTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	GetToolManager()->PostInvalidation();
	DynamicMeshComponent->bExplicitShowWireframe = Settings->bWireframe;

	if (CheckerMaterial != nullptr)
	{
		CheckerMaterial->SetScalarParameterValue("Density", Settings->CheckerDensity);
	}

	if (Settings->MaterialMode != ActiveMaterialMode)
	{
		if (Settings->MaterialMode == EInspectorMaterialMode::Checkerboard && CheckerMaterial != nullptr)
		{
			DynamicMeshComponent->SetMaterial(0, CheckerMaterial);
			ActiveMaterialMode = EInspectorMaterialMode::Checkerboard;
			return;
		}
		if (Settings->MaterialMode == EInspectorMaterialMode::Override && Settings->OverrideMaterial != nullptr)
		{
			DynamicMeshComponent->SetMaterial(0, Settings->OverrideMaterial);
			ActiveMaterialMode = EInspectorMaterialMode::Override;
			return;
		}
		// default or fallback
		if (DefaultMaterial != nullptr)
		{
			DynamicMeshComponent->SetMaterial(0, DefaultMaterial);
		}
		ActiveMaterialMode = EInspectorMaterialMode::Default;
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
