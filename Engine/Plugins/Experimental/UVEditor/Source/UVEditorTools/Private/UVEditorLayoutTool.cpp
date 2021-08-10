// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorLayoutTool.h"

#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "ParameterizationOps/UVLayoutOp.h"
#include "Properties/UVLayoutProperties.h"
#include "ToolTargets/UVEditorToolMeshInput.h"


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
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			Target->UpdateCanonicalFromPreviews();
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
