// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorTransformTool.h"

#include "ContextObjectStore.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "Operators/UVEditorUVTransformOp.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "ContextObjects/UVToolContextObjects.h"
#include "EngineAnalytics.h"
#include "UVEditorToolAnalyticsUtils.h"
#include "UVEditorUXSettings.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVEditorTransformTool"

// Tool builder
// TODO: Could consider sharing some of the tool builder boilerplate for UV editor tools in a common base class.

bool UUVEditorBaseTransformToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Targets && Targets->Num() > 0;
}

UInteractiveTool* UUVEditorBaseTransformToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVEditorTransformTool* NewTool = NewObject<UUVEditorTransformTool>(SceneState.ToolManager);
	ConfigureTool(NewTool);	
	return NewTool;
}

void UUVEditorBaseTransformToolBuilder::ConfigureTool(UUVEditorTransformTool* NewTool) const
{
	NewTool->SetTargets(*Targets);
}

void UUVEditorTransformToolBuilder::ConfigureTool(UUVEditorTransformTool* NewTool) const
{
	UUVEditorBaseTransformToolBuilder::ConfigureTool(NewTool);
	NewTool->SetToolMode(EUVEditorUVTransformType::Transform);
}

void UUVEditorAlignToolBuilder::ConfigureTool(UUVEditorTransformTool* NewTool) const
{
	UUVEditorBaseTransformToolBuilder::ConfigureTool(NewTool);
	NewTool->SetToolMode(EUVEditorUVTransformType::Align);
}

void UUVEditorDistributeToolBuilder::ConfigureTool(UUVEditorTransformTool* NewTool) const
{
	UUVEditorBaseTransformToolBuilder::ConfigureTool(NewTool);
	NewTool->SetToolMode(EUVEditorUVTransformType::Distribute);
}

void UUVEditorTransformTool::SetToolMode(const EUVEditorUVTransformType& Mode)
{
	ToolMode = Mode;
}

void UUVEditorTransformTool::Setup()
{
	check(Targets.Num() > 0);

	ToolStartTimeAnalytics = FDateTime::UtcNow();

	UInteractiveTool::Setup();

	switch(ToolMode.Get(EUVEditorUVTransformType::Transform))
	{
		case EUVEditorUVTransformType::Transform:
			Settings = NewObject<UUVEditorUVTransformProperties>(this);
			break;
		case EUVEditorUVTransformType::Align:
			Settings = NewObject<UUVEditorUVAlignProperties>(this);
			break;
		case EUVEditorUVTransformType::Distribute:
			Settings = NewObject<UUVEditorUVDistributeProperties>(this);
			break;
		default:
			ensure(false);
	}
	Settings->RestoreProperties(this);
	//Settings->bUDIMCVAREnabled = (FUVEditorUXSettings::CVarEnablePrototypeUDIMSupport.GetValueOnGameThread() > 0);
	AddToolPropertySource(Settings);

	UContextObjectStore* ContextStore = GetToolManager()->GetContextObjectStore();
	UVToolSelectionAPI = ContextStore->FindContext<UUVToolSelectionAPI>();

	UUVToolSelectionAPI::FHighlightOptions HighlightOptions;
	HighlightOptions.bBaseHighlightOnPreviews = true;
	HighlightOptions.bAutoUpdateUnwrap = true;
	UVToolSelectionAPI->SetHighlightOptions(HighlightOptions);
	UVToolSelectionAPI->SetHighlightVisible(true, false, true);
	
	auto SetupOpFactory = [this](UUVEditorToolMeshInput& Target, const FUVToolSelection* Selection)
	{
		TObjectPtr<UUVEditorUVTransformOperatorFactory> Factory = NewObject<UUVEditorUVTransformOperatorFactory>();
		Factory->TargetTransform = Target.AppliedPreview->PreviewMesh->GetTransform();
		Factory->Settings = Settings;
		Factory->TransformType = ToolMode.Get(EUVEditorUVTransformType::Transform);
		Factory->OriginalMesh = Target.UnwrapCanonical;
		Factory->GetSelectedUVChannel = [&Target]() { return Target.UVLayerIndex; };
		if (Selection)
		{
			// If we have a selection, it's in the unwrapped mesh. We need both triangles, which are 1:1 between them,
			// and vertices, which are not, in the applied mesh to pass to the factory.
			FUVToolSelection UnwrapVertexSelection;
			if (Selection->Type == FUVToolSelection::EType::Vertex)
			{
				UnwrapVertexSelection = *Selection;
			}
			else
			{
				UnwrapVertexSelection = Selection->GetConvertedSelection(*Target.UnwrapCanonical, FUVToolSelection::EType::Vertex);
			}				
			Factory->VertexSelection.Emplace(UnwrapVertexSelection.SelectedIDs);
			Factory->TriangleSelection.Emplace(UnwrapVertexSelection.GetConvertedSelection(*Target.UnwrapCanonical, FUVToolSelection::EType::Triangle).SelectedIDs);
		}

		Target.UnwrapPreview->ChangeOpFactory(Factory);
		Target.UnwrapPreview->OnMeshUpdated.AddWeakLambda(this, [this, &Target](UMeshOpPreviewWithBackgroundCompute* Preview) {
			Target.UpdateUnwrapPreviewOverlayFromPositions();
			Target.UpdateAppliedPreviewFromUnwrapPreview();
			

			this->UVToolSelectionAPI->RebuildUnwrapHighlight(Preview->PreviewMesh->GetTransform());
			});

		Target.UnwrapPreview->InvalidateResult();
		return Factory;
	};

	if (UVToolSelectionAPI->HaveSelections())
	{
		Factories.Reserve(UVToolSelectionAPI->GetSelections().Num());
		for (FUVToolSelection Selection : UVToolSelectionAPI->GetSelections())
		{
			Factories.Add(SetupOpFactory(*Selection.Target, &Selection));
		}
	}
	else
	{
		Factories.Reserve(Targets.Num());
		for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
		{
			Factories.Add(SetupOpFactory(*Targets[TargetIndex], nullptr));
		}
	}

	SetToolDisplayName(LOCTEXT("ToolName", "UV Transform"));
	GetToolManager()->DisplayMessage(LOCTEXT("OnStartUVTransformTool", "Translate, rotate or scale existing UV Charts using various strategies"),
		EToolMessageLevel::UserNotification);

	// Analytics
	InputTargetAnalytics = UVEditorAnalytics::CollectTargetAnalytics(Targets);
}

void UUVEditorTransformTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->UnwrapPreview->OnMeshUpdated.RemoveAll(this);
	}

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UUVToolEmitChangeAPI* ChangeAPI = GetToolManager()->GetContextObjectStore()->FindContext<UUVToolEmitChangeAPI>();
		const FText TransactionName(LOCTEXT("TransformTransactionName", "Transform Tool"));
		ChangeAPI->BeginUndoTransaction(TransactionName);

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

			ChangeAPI->EmitToolIndependentUnwrapCanonicalChange(Target, ChangeTracker.EndChange(), LOCTEXT("ApplyTransformTool", "Transform Tool"));
		}

		ChangeAPI->EndUndoTransaction();

		// Analytics
		RecordAnalytics();
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
		Target->UnwrapPreview->ClearOpFactory();
	}

	for (int32 FactoryIndex = 0; FactoryIndex < Factories.Num(); ++FactoryIndex)
	{
		Factories[FactoryIndex] = nullptr;
	}

	Settings = nullptr;
	Targets.Empty();
}

void UUVEditorTransformTool::OnTick(float DeltaTime)
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->UnwrapPreview->Tick(DeltaTime);
	}
}

void UUVEditorTransformTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->UnwrapPreview->InvalidateResult();
	}
}

bool UUVEditorTransformTool::CanAccept() const
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		if (!Target->UnwrapPreview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}

void UUVEditorTransformTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	// TODO: Add support here for highlighting first selected item for alignment visualization
}


void UUVEditorTransformTool::RecordAnalytics()
{
	using namespace UVEditorAnalytics;

	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}

	TArray<FAnalyticsEventAttribute> Attributes;
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), FDateTime::UtcNow().ToString()));

	// Tool inputs
	InputTargetAnalytics.AppendToAttributes(Attributes, "Input");

	// Tool stats
	if (CanAccept())
	{
		TArray<double> PerAssetValidResultComputeTimes;
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			// Note: This would log -1 if the result was invalid, but checking CanAccept above ensures results are valid
			PerAssetValidResultComputeTimes.Add(Target->UnwrapPreview->GetValidResultComputeTime());
		}
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Stats.PerAsset.ComputeTimeSeconds"), PerAssetValidResultComputeTimes));
	}
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Stats.ToolActiveDuration"), (FDateTime::UtcNow() - ToolStartTimeAnalytics).ToString()));

	// Tool settings chosen by the user
	//Attributes.Add(AnalyticsEventAttributeEnum(TEXT("Settings.LayoutType"), Settings->LayoutType));
	//Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.TextureResolution"), Settings->TextureResolution));
	//Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Scale"), Settings->Scale));
	//const TArray<FVector2D::FReal> TranslationArray({ Settings->Translation.X, Settings->Translation.Y });
	//Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Translation"), TranslationArray));
	//Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.AllowFlips"), Settings->bAllowFlips));

	FEngineAnalytics::GetProvider().RecordEvent(UVEditorAnalyticsEventName(TEXT("TransformTool")), Attributes);

	if constexpr (false)
	{
		for (const FAnalyticsEventAttribute& Attr : Attributes)
		{
			UE_LOG(LogGeometry, Log, TEXT("Debug %s.TransformTool.%s = %s"), *UVEditorAnalyticsPrefix, *Attr.GetName(), *Attr.GetValue());
		}
	}
}

#undef LOCTEXT_NAMESPACE
