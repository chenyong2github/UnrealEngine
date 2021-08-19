// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorLayoutTool.h"

#include "ContextObjectStore.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "ParameterizationOps/UVLayoutOp.h"
#include "Properties/UVLayoutProperties.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "UVToolContextObjects.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVSelectTool"

// Tool builder
// TODO: Could consider sharing some of the tool builder boilerplate for UV editor tools in a common base class.

const FToolTargetTypeRequirements& UUVEditorLayoutToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(UUVEditorToolMeshInput::StaticClass());
	return TypeRequirements;
}

bool UUVEditorLayoutToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Targets && Targets->Num() > 0;
}

UInteractiveTool* UUVEditorLayoutToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVEditorLayoutTool* NewTool = NewObject<UUVEditorLayoutTool>(SceneState.ToolManager);
	NewTool->SetTargets(*Targets);

	return NewTool;
}


void UUVEditorLayoutTool::Setup()
{
	check(Targets.Num() > 0);

	UInteractiveTool::Setup();

	Settings = NewObject<UUVLayoutProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		UUVLayoutOperatorFactory* OpFactory = NewObject<UUVLayoutOperatorFactory>();
		OpFactory->TargetTransform = Target->AppliedPreview->PreviewMesh->GetTransform();
		OpFactory->Settings = Settings;
		OpFactory->OriginalMesh = Target->AppliedCanonical;
		OpFactory->GetSelectedUVChannel = [Target]() { return Target->UVLayerIndex; };

		Target->AppliedPreview->ChangeOpFactory(OpFactory);
		Target->AppliedPreview->OnMeshUpdated.AddWeakLambda(this, [Target](UMeshOpPreviewWithBackgroundCompute* Preview) {
			Target->UpdateUnwrapPreviewFromAppliedPreview();
		});

		Target->AppliedPreview->InvalidateResult();
	}

	SetToolDisplayName(LOCTEXT("ToolName", "UV Layout"));
	GetToolManager()->DisplayMessage(LOCTEXT("OnStartUVLayoutTool", "Transform/Rotate/Scale existing UV Charts using various strategies"),
		EToolMessageLevel::UserNotification);
}

void UUVEditorLayoutTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->OnMeshUpdated.RemoveAll(this);
		Target->AppliedPreview->ClearOpFactory();
	}

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UUVToolEmitChangeAPI* ChangeAPI = GetToolManager()->GetContextObjectStore()->FindContext<UUVToolEmitChangeAPI>();

		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			// Set things up for undo. 
			// TODO: It's not entirely clear whether it would be safe to use a FMeshVertexChange instead... It seems like
			// when bAllowFlips is true, we would end up with changes to the tris of the unwrap. Also, if we stick to saving
			// all the tris and verts, should we consider using the new dynamic mesh serialization?
			FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
			ChangeTracker.BeginChange();
			
			for (int32 Tid : Target->UnwrapCanonical->TriangleIndicesItr())
			{
				ChangeTracker.SaveTriangle(Tid, true);
			}

			// TODO: Again, it's not clear whether we need to update the entire triangle topology...
			Target->UpdateCanonicalFromPreviews();

			ChangeAPI->EmitToolIndependentUnwrapCanonicalChange(Target, ChangeTracker.EndChange(), LOCTEXT("ApplyLayoutTool", "Layout Tool"));
		}
	}
	else
	{
		// Reset the inputs
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			Target->UpdatePreviewsFromCanonical();
		}
	}

	Settings = nullptr;
	Targets.Empty();
}

void UUVEditorLayoutTool::OnTick(float DeltaTime)
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->Tick(DeltaTime);
	}
}



void UUVEditorLayoutTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->InvalidateResult();
	}
}

bool UUVEditorLayoutTool::CanAccept() const
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		if (!Target->AppliedPreview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
