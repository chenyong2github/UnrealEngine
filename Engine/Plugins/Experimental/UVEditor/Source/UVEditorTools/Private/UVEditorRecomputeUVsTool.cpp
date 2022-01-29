// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorRecomputeUVsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Polygroups/PolygroupUtil.h"
#include "ParameterizationOps/RecomputeUVsOp.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "UVToolContextObjects.h"
#include "ContextObjectStore.h"
#include "MeshOpPreviewHelpers.h"
#include "UVEditorToolAnalyticsUtils.h"
#include "EngineAnalytics.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVEditorRecomputeUVsTool"


/*
 * ToolBuilder
 */

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
	ToolStartTimeAnalytics = FDateTime::UtcNow();
	
	UInteractiveTool::Setup();

	// initialize our properties

	Settings = NewObject<URecomputeUVsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);
	Factories.SetNum(Targets.Num());

	if (Targets.Num() == 1)
	{
		PolygroupLayerProperties = NewObject<UPolygroupLayersProperties>(this);
		PolygroupLayerProperties->RestoreProperties(this, TEXT("UVEditorRecomputeUVsTool"));
		PolygroupLayerProperties->InitializeGroupLayers(Targets[0]->AppliedCanonical.Get());
		PolygroupLayerProperties->WatchProperty(PolygroupLayerProperties->ActiveGroupLayer, [&](FName) { OnSelectedGroupLayerChanged(); });
		AddToolPropertySource(PolygroupLayerProperties);
		UpdateActiveGroupLayer();
	}
	else
	{
		Settings->bEnablePolygroupSupport = false;
		Settings->IslandGeneration = ERecomputeUVsPropertiesIslandMode::ExistingUVs;
	}

	
	for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
	{
		TObjectPtr<UUVEditorToolMeshInput> Target = Targets[TargetIndex];
		Factories[TargetIndex] = NewObject<URecomputeUVsOpFactory>();
		Factories[TargetIndex]->TargetTransform = (FTransform3d)Target->AppliedPreview->PreviewMesh->GetTransform();
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
		LOCTEXT("OnStartTool_Regions", "Generate UVs for PolyGroups or existing UV islands of the mesh using various strategies."),
		EToolMessageLevel::UserNotification);
	
	// Analytics
	InputTargetAnalytics = UVEditorAnalytics::CollectTargetAnalytics(Targets);
}


void UUVEditorRecomputeUVsTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == Settings )
	{
		// One of the UV generation properties must have changed.  Dirty the result to force a recompute
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			Target->AppliedPreview->InvalidateResult();
		}
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
		Target->AppliedPreview->ClearOpFactory();
	}
	for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
	{
		Factories[TargetIndex] = nullptr;
	}
	Settings = nullptr;
	Targets.Empty();
}

void UUVEditorRecomputeUVsTool::OnTick(float DeltaTime)
{
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->AppliedPreview->Tick(DeltaTime);
	}
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
			ensureMsgf(FoundAttrib, TEXT("Selected attribute not found! Falling back to Default group layer."));
			ActiveGroupSet = MakeShared<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe>(Targets[0]->AppliedCanonical.Get(), FoundAttrib);
		}
	}
	for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
	{
		if (Factories[TargetIndex]) {
			Factories[TargetIndex]->InputGroups = ActiveGroupSet;
		}
	}
}


void UUVEditorRecomputeUVsTool::RecordAnalytics()
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

	// Tool outputs	
	const FTargetAnalytics OutputTargetAnalytics = CollectTargetAnalytics(Targets);
	OutputTargetAnalytics.AppendToAttributes(Attributes, "Output");
	
	// Tool stats
	if (CanAccept())
	{
		TArray<double> PerAssetValidResultComputeTimes;
		for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
		{
			// Note: This would log -1 if the result was invalid, but checking CanAccept above ensures results are valid
			PerAssetValidResultComputeTimes.Add(Target->AppliedPreview->GetValidResultComputeTime());
		}
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Stats.PerAsset.ComputeTimeSeconds"), PerAssetValidResultComputeTimes));
	}
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Stats.ToolActiveDuration"), (FDateTime::UtcNow() - ToolStartTimeAnalytics).ToString()));
	
	// Tool settings chosen by the user (Volatile! Sync with EditCondition meta-tags in URecomputeUVsToolProperties)
	
	Attributes.Add(AnalyticsEventAttributeEnum(TEXT("Settings.IslandGeneration"), Settings->IslandGeneration));
	Attributes.Add(AnalyticsEventAttributeEnum(TEXT("Settings.AutoRotation"), Settings->AutoRotation));
	
	Attributes.Add(AnalyticsEventAttributeEnum(TEXT("Settings.UnwrapType"), Settings->UnwrapType));
	if (Settings->UnwrapType == ERecomputeUVsPropertiesUnwrapType::IslandMerging || Settings->UnwrapType == ERecomputeUVsPropertiesUnwrapType::ExpMap)
	{
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.SmoothingSteps"), Settings->SmoothingSteps));
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.SmoothingAlpha"), Settings->SmoothingAlpha));
	}
	if (Settings->UnwrapType == ERecomputeUVsPropertiesUnwrapType::IslandMerging)
	{
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.MergingDistortionThreshold"), Settings->MergingDistortionThreshold));
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.MergingAngleThreshold"), Settings->MergingAngleThreshold));
	}
	
	Attributes.Add(AnalyticsEventAttributeEnum(TEXT("Settings.LayoutType"), Settings->LayoutType));
	if (Settings->LayoutType == ERecomputeUVsPropertiesLayoutType::Repack)
	{
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.TextureResolution"), Settings->TextureResolution));
	}
	if (Settings->LayoutType == ERecomputeUVsPropertiesLayoutType::NormalizeToBounds || Settings->LayoutType == ERecomputeUVsPropertiesLayoutType::NormalizeToWorld)
	{
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.NormalizeScale"), Settings->NormalizeScale));
	}

	FEngineAnalytics::GetProvider().RecordEvent(UVEditorAnalyticsEventName(TEXT("UnwrapTool")), Attributes);

	if constexpr (false)
	{
		for (const FAnalyticsEventAttribute& Attr : Attributes)
		{
			UE_LOG(LogGeometry, Log, TEXT("Debug %s.UnwrapTool.%s = %s"), *UVEditorAnalyticsPrefix, *Attr.GetName(), *Attr.GetValue());
		}
	}
}

#undef LOCTEXT_NAMESPACE
