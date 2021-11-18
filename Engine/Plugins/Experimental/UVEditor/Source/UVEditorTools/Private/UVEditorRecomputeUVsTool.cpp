// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorRecomputeUVsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Polygroups/PolygroupUtil.h"
#include "FaceGroupUtil.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"
#include "ParameterizationOps/RecomputeUVsOp.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Properties/MeshMaterialProperties.h"
#include "UVToolContextObjects.h"
#include "ContextObjectStore.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVEditorRecomputeUVsTool"


/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UUVEditorRecomputeUVsToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(UUVEditorToolMeshInput::StaticClass());
	return TypeRequirements;
}

bool UUVEditorRecomputeUVsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Targets && Targets->Num() > 0;
}

UInteractiveTool* UUVEditorRecomputeUVsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVEditorRecomputeUVsTool* NewTool = NewObject<UUVEditorRecomputeUVsTool>(SceneState.ToolManager);
	NewTool->SetTargets(*Targets);

	return NewTool;
}


/*
 * Tool
 */


void UUVEditorRecomputeUVsTool::Setup()
{
	UInteractiveTool::Setup();

	// initialize our properties

	Settings = NewObject<URecomputeUVsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	if (Targets.Num() == 1)
	{
		PolygroupLayerProperties = NewObject<UPolygroupLayersProperties>(this);
		PolygroupLayerProperties->RestoreProperties(this, TEXT("UVEditorRecomputeUVsTool"));
		PolygroupLayerProperties->InitializeGroupLayers(Targets[0]->AppliedCanonical.Get());
		PolygroupLayerProperties->WatchProperty(PolygroupLayerProperties->ActiveGroupLayer, [&](FName) { OnSelectedGroupLayerChanged(); });
		AddToolPropertySource(PolygroupLayerProperties);
		UpdateActiveGroupLayer();
	}

	Factories.SetNum(Targets.Num());
	for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
	{
		TObjectPtr<UUVEditorToolMeshInput> Target = Targets[TargetIndex];
		Factories[TargetIndex] = NewObject<URecomputeUVsOpFactory>();
		Factories[TargetIndex]->TargetTransform = (UE::Geometry::FTransform3d)Target->AppliedPreview->PreviewMesh->GetTransform();
		Factories[TargetIndex]->Settings = Settings;
		Factories[TargetIndex]->OriginalMesh = Target->AppliedCanonical;
		Factories[TargetIndex]->InputGroups = ActiveGroupSet;
		Factories[TargetIndex]->GetSelectedUVChannel = [Target]() { return Target->UVLayerIndex; };

		Target->AppliedPreview->ChangeOpFactory(Factories[TargetIndex]);
		Target->AppliedPreview->OnMeshUpdated.AddWeakLambda(this, [Target](UMeshOpPreviewWithBackgroundCompute* Preview) {
			Target->UpdateUnwrapPreviewFromAppliedPreview();
			});

		Target->AppliedPreview->InvalidateResult();
	}

	SetToolDisplayName(LOCTEXT("ToolNameLocal", "UV Unwrap"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool_Regions", "Generate UVs for Polygroups or existing UV charts of the Mesh using various strategies."),
		EToolMessageLevel::UserNotification);
}


void UUVEditorRecomputeUVsTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	bool bForceMaterialUpdate = false;
	if (PropertySet == Settings )
	{
		// One of the UV generation properties must have changed.  Dirty the result to force a recompute
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			Target->AppliedPreview->InvalidateResult();
		}
		bForceMaterialUpdate = true;
	}

}


void UUVEditorRecomputeUVsTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	if (PolygroupLayerProperties) {
		PolygroupLayerProperties->RestoreProperties(this, TEXT("UVEditorRecomputeUVsTool"));
	}

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UUVToolEmitChangeAPI* ChangeAPI = GetToolManager()->GetContextObjectStore()->FindContext<UUVToolEmitChangeAPI>();

		const FText TransactionName(LOCTEXT("RecomputeUVsTransactionName", "Recompute UVs"));
		ChangeAPI->BeginUndoTransaction(TransactionName);
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			// Set things up for undo. 
			FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
			ChangeTracker.BeginChange();

			for (int32 Tid : Target->UnwrapCanonical->TriangleIndicesItr())
			{
				ChangeTracker.SaveTriangle(Tid, true);
			}

			Target->UpdateCanonicalFromPreviews();

			ChangeAPI->EmitToolIndependentUnwrapCanonicalChange(Target, ChangeTracker.EndChange(), LOCTEXT("ApplyRecomputeUVsTool", "Unwrap Tool"));
		}

		ChangeAPI->EndUndoTransaction();
	}
	else
	{
		// Reset the inputs
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			Target->UpdatePreviewsFromCanonical();
		}
	}

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->ClearOpFactory();
	}
	for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
	{
		Factories[TargetIndex] = nullptr;
	}
	Settings = nullptr;
	Targets.Empty();
}

void UUVEditorRecomputeUVsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}



bool UUVEditorRecomputeUVsTool::CanAccept() const
{
	bool bPreviewsHaveValidResults = true;
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		bPreviewsHaveValidResults = bPreviewsHaveValidResults && Target->AppliedPreview->HaveValidResult();
	}
	return bPreviewsHaveValidResults;
}


void UUVEditorRecomputeUVsTool::OnSelectedGroupLayerChanged()
{
	UpdateActiveGroupLayer();
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->InvalidateResult();
	}
}


void UUVEditorRecomputeUVsTool::UpdateActiveGroupLayer()
{
	if (Targets.Num() == 1)
	{
		if (PolygroupLayerProperties->HasSelectedPolygroup() == false)
		{
			ActiveGroupSet = MakeShared<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe>(Targets[0]->AppliedCanonical.Get());
		}
		else
		{
			FName SelectedName = PolygroupLayerProperties->ActiveGroupLayer;
			FDynamicMeshPolygroupAttribute* FoundAttrib = UE::Geometry::FindPolygroupLayerByName(*Targets[0]->AppliedCanonical, SelectedName);
			ensureMsgf(FoundAttrib, TEXT("Selected Attribute Not Found! Falling back to Default group layer."));
			ActiveGroupSet = MakeShared<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe>(Targets[0]->AppliedCanonical.Get(), FoundAttrib);
		}
	}
}


#undef LOCTEXT_NAMESPACE
