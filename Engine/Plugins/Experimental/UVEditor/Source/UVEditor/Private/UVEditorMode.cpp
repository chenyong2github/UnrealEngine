// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorMode.h"

#include "Algo/AnyOf.h"
#include "ContextObjectStore.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Editor.h"
#include "EdModeInteractiveToolsContext.h" //ToolsContext
#include "EngineAnalytics.h"
#include "Framework/Commands/UICommandList.h"
#include "InteractiveTool.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "ModelingToolTargetUtil.h"
#include "PreviewMesh.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "ToolSetupUtil.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "ToolTargetManager.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "UVEditorCommands.h"
#include "UVEditorLayoutTool.h"
#include "UVEditorParameterizeMeshTool.h"
#include "UVEditorLayerEditTool.h"
#include "UVSelectTool.h"
#include "UVEditorModeToolkit.h"
#include "UVEditorSubsystem.h"
#include "UVEditorToolUtil.h"
#include "UVToolContextObjects.h"
#include "UVEditorBackgroundPreview.h"
#include "UVEditorModeChannelProperties.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "UUVEditorMode"

using namespace UE::Geometry;

const FEditorModeID UUVEditorMode::EM_UVEditorModeId = TEXT("EM_UVEditorMode");

FDateTime UUVEditorMode::AnalyticsLastStartTimestamp;

namespace UVEditorModeLocals
{
	// The layer we open when we first open the UV editor
	const int32 DefaultUVLayerIndex = 0;

	// Determines what the default tool is
	const FString DefaultToolIdentifier = TEXT("UVSelectTool");

	const FText UVLayerChangeTransactionName = LOCTEXT("UVLayerChangeTransactionName", "Change UV Layer");

	/** 
	 * Change for undoing/redoing displayed layer changes.
	 */
	class FInputObjectUVLayerChange : public FToolCommandChange
	{
	public:
		FInputObjectUVLayerChange(int32 AssetIDIn, int32 OldUVLayerIndexIn, int32 NewUVLayerIndexIn)
			: AssetID(AssetIDIn)
			, OldUVLayerIndex(OldUVLayerIndexIn)
			, NewUVLayerIndex(NewUVLayerIndexIn)
		{
		}

		virtual void Apply(UObject* Object) override
		{
			UUVEditorMode* UVEditorMode = Cast<UUVEditorMode>(Object);
			UVEditorMode->ChangeInputObjectLayer(AssetID, NewUVLayerIndex);
			UVEditorMode->UpdateSelectedLayer();
		}

		virtual void Revert(UObject* Object) override
		{
			UUVEditorMode* UVEditorMode = Cast<UUVEditorMode>(Object);
			UVEditorMode->ChangeInputObjectLayer(AssetID, OldUVLayerIndex);
			UVEditorMode->UpdateSelectedLayer();
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			UUVEditorMode* UVEditorMode = Cast<UUVEditorMode>(Object);
			return !(UVEditorMode && UVEditorMode->IsActive());
		}

		virtual FString ToString() const override
		{
			return TEXT("UVEditorModeLocals::FInputObjectUVLayerChange");
		}

	protected:
		int32 AssetID;
		int32 OldUVLayerIndex;
		int32 NewUVLayerIndex;
	};



}

const FToolTargetTypeRequirements& UUVEditorMode::GetToolTargetRequirements()
{
	static const FToolTargetTypeRequirements ToolTargetRequirements =
		FToolTargetTypeRequirements({
			UMaterialProvider::StaticClass(),

			// What we actually care about is dynamic meshes, but we don't currently have
			// a standardized dynamic mesh commiter/provider interface, because UDynamicMesh
			// doesn't implement IDynamicMeshCommitter
			UMeshDescriptionCommitter::StaticClass(),
			UMeshDescriptionProvider::StaticClass()
			});
	return ToolTargetRequirements;
}

UUVEditorMode::UUVEditorMode()
{
	Info = FEditorModeInfo(
		EM_UVEditorModeId,
		LOCTEXT("UVEditorModeName", "UV"),
		FSlateIcon(),
		false);
}

void UUVEditorMode::Enter()
{
	Super::Enter();

	BackgroundVisualization = NewObject<UUVEditorBackgroundPreview>(this);
	BackgroundVisualization->CreateInWorld(GetWorld(), FTransform::Identity);

	UVChannelProperties = NewObject<UUVEditorUVChannelProperties>(this);
	UVChannelProperties->WatchProperty(UVChannelProperties->Asset, [this](FString UVAsset) {SwitchActiveAsset(UVAsset); });
	UVChannelProperties->WatchProperty(UVChannelProperties->UVChannel, [this](FString UVChannel) {SwitchActiveChannel(UVChannel); });

	BackgroundVisualization->Settings->WatchProperty(BackgroundVisualization->Settings->bVisible, 
		[this](bool IsVisible) { UpdateTriangleMaterialBasedOnBackground(IsVisible); });

	AddDisplayedPropertySet(UVChannelProperties);
	AddDisplayedPropertySet(BackgroundVisualization->Settings);
		
	StaticCastSharedPtr<FUVEditorModeToolkit>(Toolkit)->SetModeDetailsViewObjects(PropertyObjectsToDisplay);	
	
	RegisterTools();

	ActivateDefaultTool();

	if (FEngineAnalytics::IsAvailable())
	{
		AnalyticsLastStartTimestamp = FDateTime::UtcNow();

		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), AnalyticsLastStartTimestamp.ToString()));

		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.UVEditor.Enter"), Attributes);
	}

	bIsActive = true;
}

void UUVEditorMode::RegisterTools()
{
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();

	UUVSelectToolBuilder* UVSelectToolBuilder = NewObject<UUVSelectToolBuilder>();
	UVSelectToolBuilder->Targets = &ToolInputObjects;
	RegisterTool(CommandInfos.BeginSelectTool, TEXT("UVSelectTool"), UVSelectToolBuilder);

	UUVEditorLayoutToolBuilder* UVEditorLayoutToolBuilder = NewObject<UUVEditorLayoutToolBuilder>();
	UVEditorLayoutToolBuilder->Targets = &ToolInputObjects;
	RegisterTool(CommandInfos.BeginLayoutTool, TEXT("UVLayoutTool"), UVEditorLayoutToolBuilder);

	UUVEditorParameterizeMeshToolBuilder* UVEditorParameterizeMeshToolBuilder = NewObject<UUVEditorParameterizeMeshToolBuilder>();
	UVEditorParameterizeMeshToolBuilder->Targets = &ToolInputObjects;
	RegisterTool(CommandInfos.BeginParameterizeMeshTool, TEXT("UVParameterizeMeshTool"), UVEditorParameterizeMeshToolBuilder);

	UUVEditorChannelEditToolBuilder* UVEditorChannelEditToolBuilder = NewObject<UUVEditorChannelEditToolBuilder>();
	UVEditorChannelEditToolBuilder->Targets = &ToolInputObjects;
	RegisterTool(CommandInfos.BeginChannelEditTool, TEXT("UVChannelEditTool"), UVEditorChannelEditToolBuilder);
}

void UUVEditorMode::AddDisplayedPropertySet(const TObjectPtr<UInteractiveToolPropertySet>& PropertySet)
{
	PropertyObjectsToTick.Add(PropertySet);
	PropertyObjectsToDisplay.Add(PropertySet);
}

void UUVEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FUVEditorModeToolkit>();
}

void UUVEditorMode::ActivateDefaultTool()
{
	GetInteractiveToolsContext()->StartTool(UVEditorModeLocals::DefaultToolIdentifier);
}

bool UUVEditorMode::IsDefaultToolActive()
{
	return GetInteractiveToolsContext()->IsToolActive(EToolSide::Mouse, UVEditorModeLocals::DefaultToolIdentifier);
}

void UUVEditorMode::BindCommands()
{
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	// Hook up to Enter/Esc key presses
	CommandList->MapAction(
		CommandInfos.AcceptActiveTool,
		FExecuteAction::CreateLambda([this]() { 
			GetInteractiveToolsContext()->EndTool(EToolShutdownType::Accept); 
			ActivateDefaultTool();
			}),
		FCanExecuteAction::CreateLambda([this]() { return GetInteractiveToolsContext()->CanAcceptActiveTool(); }),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateLambda([this]() {return GetInteractiveToolsContext()->ActiveToolHasAccept(); }),
		EUIActionRepeatMode::RepeatDisabled
	);

	CommandList->MapAction(
		CommandInfos.CancelActiveTool,
		FExecuteAction::CreateLambda([this]() { 
			GetInteractiveToolsContext()->EndTool(EToolShutdownType::Cancel);
			ActivateDefaultTool();
			}),
		FCanExecuteAction::CreateLambda([this]() { return GetInteractiveToolsContext()->CanCancelActiveTool(); }),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateLambda([this]() {return GetInteractiveToolsContext()->ActiveToolHasAccept(); }),
		EUIActionRepeatMode::RepeatDisabled
	);
}

void UUVEditorMode::Exit()
{
	if (FEngineAnalytics::IsAvailable())
	{
		const FDateTime Now = FDateTime::UtcNow();
		const FTimespan ModeUsageDuration = Now - AnalyticsLastStartTimestamp;

		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), Now.ToString()));
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Duration.Seconds"), static_cast<float>(ModeUsageDuration.GetTotalSeconds())));

		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.UVEditor.Exit"), Attributes);
	}

	// ToolsContext->EndTool only shuts the tool on the next tick, and ToolsContext->DeactivateActiveTool is
	// inaccessible, so we end up having to do this to force the shutdown right now.
	GetToolManager()->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Cancel);

	for (TObjectPtr<UUVEditorToolMeshInput> ToolInput : ToolInputObjects)
	{
		ToolInput->Shutdown();
	}
	ToolInputObjects.Reset();
	WireframesToTick.Reset();
	OriginalObjectsToEdit.Reset();
	
	for (TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview : AppliedPreviews)
	{
		Preview->Shutdown();
	}
	AppliedPreviews.Reset();
	AppliedCanonicalMeshes.Reset();
	ToolTargets.Reset();

	if (BackgroundVisualization)
	{
		BackgroundVisualization->Disconnect();
		BackgroundVisualization = nullptr;
	}

	UVChannelProperties = nullptr;
	PropertyObjectsToTick.Empty();
	PropertyObjectsToDisplay.Empty();
	LivePreviewWorld = nullptr;

	bIsActive = false;

	Super::Exit();
}

void UUVEditorMode::InitializeTargets(const TArray<TObjectPtr<UObject>>& AssetsIn, 
	const TArray<FTransform>& TransformsIn)
{
	using namespace UVEditorModeLocals;

	OriginalObjectsToEdit = AssetsIn;
	Transforms = TransformsIn;

	// Build the tool targets that provide us with 3d dynamic meshes
	UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
	UVSubsystem->BuildTargets(AssetsIn, GetToolTargetRequirements(), ToolTargets);

	// For creating the actual input objects, we'll need pointers both to the 2d unwrap world and the
	// 3d preview world. We already have the 2d world in GetWorld(). Get the 3d one.
	UContextObjectStore* ContextStore = GetInteractiveToolsContext()->ToolManager->GetContextObjectStore();
	UUVToolLivePreviewAPI* LivePreviewAPI = ContextStore->FindContext<UUVToolLivePreviewAPI>();
	check(LivePreviewAPI);
	LivePreviewWorld = LivePreviewAPI->GetLivePreviewWorld();

	// Collect the 3d dynamic meshes from targets. There will always be one for each asset, and the AssetID
	// of each asset will be the index into these arrays. Individual input objects (representing a specific
	// UV layer), will point to these existing 3d meshes.
	for (UToolTarget* Target : ToolTargets)
	{
		// The applied canonical mesh is the 3d mesh with all of the layer changes applied. If we switch
		// to a different layer, the changes persist in the applied canonical.
		TSharedPtr<FDynamicMesh3> AppliedCanonical = MakeShared<FDynamicMesh3>(UE::ToolTarget::GetDynamicMeshCopy(Target));
		AppliedCanonicalMeshes.Add(AppliedCanonical);

		// Make a preview version of the applied canonical to show. Tools can attach computes to this, though
		// they would have to take care if we ever allow multiple layers to be displayed for one asset, to
		// avoid trying to attach two computes to the same preview object (in which case one would be thrown out)
		UMeshOpPreviewWithBackgroundCompute* AppliedPreview = NewObject<UMeshOpPreviewWithBackgroundCompute>();
		AppliedPreview->Setup(LivePreviewWorld);
		AppliedPreview->PreviewMesh->UpdatePreview(AppliedCanonical.Get());

		FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Target);
		AppliedPreview->ConfigureMaterials(MaterialSet.Materials, 
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
		AppliedPreviews.Add(AppliedPreview);
	}

	// When creating UV unwraps, these functions will determine the mapping between UV values and the
	// resulting unwrap mesh vertex positions. 
	// If we're looking down on the unwrapped mesh, with the Z axis towards us, we want U's to be right, and
	// V's to be up. In Unreal's left-handed coordinate system, this means that we map U's to world Y
	// and V's to world X.
	// Also, Unreal changes the V coordinates of imported meshes to 1-V internally, and we undo this
	// while displaying the UV's because the users likely expect to see the original UV's (it would
	// be particularly confusing for users working with UDIM assets, where internally stored V's 
	// frequently end up negative).
	// The ScaleFactor just scales the mesh up. Scaling the mesh up makes it easier to zoom in
	// further into the display before getting issues with the camera near plane distance.
	double ScaleFactor = GetUVMeshScalingFactor();
	auto UVToVertPosition = [this, ScaleFactor](const FVector2f& UV)
	{
		return FVector3d((1 - UV.Y) * ScaleFactor, UV.X * ScaleFactor, 0);
	};
	auto VertPositionToUV = [this, ScaleFactor](const FVector3d& VertPosition)
	{
		return FVector2D(VertPosition.Y / ScaleFactor, 1 - (VertPosition.X / ScaleFactor));
	};

	// Construct the full input objects that the tools actually operate on.
	for (int32 AssetID = 0; AssetID < ToolTargets.Num(); ++AssetID)
	{
		UUVEditorToolMeshInput* ToolInputObject = NewObject<UUVEditorToolMeshInput>();

		if (!ToolInputObject->InitializeMeshes(ToolTargets[AssetID], AppliedCanonicalMeshes[AssetID],
			AppliedPreviews[AssetID], AssetID, DefaultUVLayerIndex,
			GetWorld(), LivePreviewWorld, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()),
			UVToVertPosition, VertPositionToUV))
		{
			return;
		}

		if (Transforms.Num() == ToolTargets.Num())
		{
			ToolInputObject->AppliedPreview->PreviewMesh->SetTransform(Transforms[AssetID]);
		}

		ToolInputObject->UnwrapPreview->PreviewMesh->SetMaterial(
			0, ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(
				GetToolManager(),
				(FLinearColor)TriangleColor,
				TriangleDepthOffset,
				TriangleOpacity));

		// Set up the wireframe display of the unwrapped mesh.
		UMeshElementsVisualizer* WireframeDisplay = NewObject<UMeshElementsVisualizer>(this);
		WireframeDisplay->CreateInWorld(GetWorld(), FTransform::Identity);

		WireframeDisplay->Settings->DepthBias = WireframeDepthOffset;
		WireframeDisplay->Settings->bAdjustDepthBiasUsingMeshSize = false;
		WireframeDisplay->Settings->bShowWireframe = true;
		WireframeDisplay->Settings->bShowBorders = true;
		WireframeDisplay->Settings->WireframeColor = WireframeColor;
		WireframeDisplay->Settings->BoundaryEdgeColor = IslandBorderColor;
		WireframeDisplay->Settings->bShowUVSeams = false;
		WireframeDisplay->Settings->bShowNormalSeams = false;
		// These are not exposed at the visualizer level yet
		// TODO: Should they be?
		WireframeDisplay->WireframeComponent->BoundaryEdgeThickness = 2;

		// The wireframe will track the unwrap preview mesh
		WireframeDisplay->SetMeshAccessFunction([ToolInputObject](UMeshElementsVisualizer::ProcessDynamicMeshFunc ProcessFunc) {
			ToolInputObject->UnwrapPreview->ProcessCurrentMesh(ProcessFunc);
		});

		// The settings object and wireframe are not part of a tool, so they won't get ticked like they
		// are supposed to (to enable property watching), unless we add this here.
		PropertyObjectsToTick.Add(WireframeDisplay->Settings);
		WireframesToTick.Add(WireframeDisplay);

		// The tool input object will hold on to the wireframe for the purposes of updating it and cleaning it up
		ToolInputObject->WireframeDisplay = WireframeDisplay;

		// Bind to delegate so that we can detect changes
		ToolInputObject->OnCanonicalModified.AddWeakLambda(this, [this]
		(UUVEditorToolMeshInput* InputObject, const UUVEditorToolMeshInput::FCanonicalModifiedInfo&) {
			ModifiedAssetIDs.Add(InputObject->AssetID);
		});

		ToolInputObjects.Add(ToolInputObject);
	}

	// Initialize our layer selector
	UVChannelProperties->Initialize(ToolTargets, AppliedCanonicalMeshes, true);
	PendingUVLayerIndex.SetNumZeroed(ToolTargets.Num());		

	int32 AssetID = UVChannelProperties->GetSelectedAssetID();
	UUVToolAssetAndChannelAPI* AssetAndLayerAPI = ContextStore->FindContext<UUVToolAssetAndChannelAPI>();
	if (AssetAndLayerAPI)
	{
		AssetAndLayerAPI->RequestChannelVisibilityChangeFunc = [this](const TArray<int32>& LayerPerAsset, bool bForceRebuildUnwrap, bool bEmitUndoTransaction) {
			this->ForceUpdateDisplayChannel(LayerPerAsset, bForceRebuildUnwrap, bEmitUndoTransaction);
		};

		AssetAndLayerAPI->NotifyOfAssetChannelCountChangeFunc = [this](int32 AssetID) {
			UVChannelProperties->Initialize(ToolTargets, AppliedCanonicalMeshes, false);
		};

		AssetAndLayerAPI->GetCurrentChannelVisibilityFunc = [this]() {
			TArray<int32> VisibleLayers;
			VisibleLayers.SetNum(ToolTargets.Num());
			for (int32 AssetID = 0; AssetID < ToolTargets.Num(); ++AssetID)
			{
				VisibleLayers[AssetID] = ToolInputObjects[AssetID]->UVLayerIndex;
			}
			return VisibleLayers;
		};
	}
}

void UUVEditorMode::EmitToolIndependentObjectChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
	GetInteractiveToolsContext()->GetTransactionAPI()->AppendChange(TargetObject, MoveTemp(Change), Description);
}

bool UUVEditorMode::HaveUnappliedChanges()
{
	return ModifiedAssetIDs.Num() > 0;
}

void UUVEditorMode::GetAssetsWithUnappliedChanges(TArray<TObjectPtr<UObject>> UnappliedAssetsOut)
{
	for (int32 AssetID : ModifiedAssetIDs)
	{
		// The asset ID corresponds to the index into OriginalObjectsToEdit
		UnappliedAssetsOut.Add(OriginalObjectsToEdit[AssetID]);
	}
}

void UUVEditorMode::ApplyChanges()
{
	using namespace UVEditorModeLocals;

	GetToolManager()->BeginUndoTransaction(LOCTEXT("UVEditorApplyChangesTransaction", "UV Editor Apply Changes"));

	for (int32 AssetID : ModifiedAssetIDs)
	{
		// The asset ID corresponds to the index into ToolTargets and AppliedCanonicalMeshes
		UE::ToolTarget::CommitDynamicMeshUVUpdate(ToolTargets[AssetID], AppliedCanonicalMeshes[AssetID].Get());
	}

	ModifiedAssetIDs.Reset();

	GetToolManager()->EndUndoTransaction();
}

void UUVEditorMode::UpdateTriangleMaterialBasedOnBackground(bool IsBackgroundVisible)
{
	// We adjust the mesh opacity depending on whether we're layered over the background or not.
	if (IsBackgroundVisible)
	{
		TriangleOpacity = 0.25;
		TriangleDepthOffset = .5;
	}
	else
	{
		TriangleOpacity = 1.0;
		TriangleDepthOffset = -.1;
	}

	// Modify the material of the unwrapped mesh to account for the presence/absence of the background, 
	// changing the opacity as set just above.
	for (UUVEditorToolMeshInput* ToolInputObject : ToolInputObjects)
	{
		ToolInputObject->UnwrapPreview->PreviewMesh->SetMaterial(
			0, ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(
				GetToolManager(),
				(FLinearColor)TriangleColor,
				TriangleDepthOffset, 
				TriangleOpacity));
	}
}

void UUVEditorMode::ModeTick(float DeltaTime)
{
	using namespace UVEditorModeLocals;

	Super::ModeTick(DeltaTime);

	bool bSwitchingLayers = false;
	for (int32 AssetID = 0; AssetID < ToolInputObjects.Num(); ++AssetID)
	{
		if (ToolInputObjects[AssetID]->UVLayerIndex != PendingUVLayerIndex[AssetID] || bForceRebuildUVLayer)
		{
			bSwitchingLayers = true;
			break;
		}
	}

	if (bSwitchingLayers)
	{
		GetToolManager()->BeginUndoTransaction(UVLayerChangeTransactionName);

		// TODO: Perhaps we need our own interactive tools context that allows this kind of "end tool now"
		// call. We can't do the normal GetInteractiveToolsContext()->EndTool() call because we cannot defer
		// shutdown.
		GetInteractiveToolsContext()->ToolManager->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Cancel);

		for (int32 AssetID = 0; AssetID < ToolInputObjects.Num(); ++AssetID)
		{
			if (ToolInputObjects[AssetID]->UVLayerIndex != PendingUVLayerIndex[AssetID] || bForceRebuildUVLayer)
			{
				int32 OldLayerIndex = ToolInputObjects[AssetID]->UVLayerIndex;

				ChangeInputObjectLayer(AssetID, PendingUVLayerIndex[AssetID], bForceRebuildUVLayer);

				GetInteractiveToolsContext()->GetTransactionAPI()->AppendChange(this,
					MakeUnique<FInputObjectUVLayerChange>(AssetID, OldLayerIndex, PendingUVLayerIndex[AssetID]),
					UVLayerChangeTransactionName);
			}
		}
		ActivateDefaultTool();

		GetToolManager()->EndUndoTransaction();

		bForceRebuildUVLayer = false;
	}
	
	for (TObjectPtr<UInteractiveToolPropertySet>& Propset : PropertyObjectsToTick)
	{
		if (Propset)
		{
			if (Propset->IsPropertySetEnabled())
			{
				Propset->CheckAndUpdateWatched();
			}
			else
			{
				Propset->SilentUpdateWatched();
			}
		}
	}

	for (TWeakObjectPtr<UMeshElementsVisualizer> WireframeDisplay : WireframesToTick)
	{
		if (WireframeDisplay.IsValid())
		{
			WireframeDisplay->OnTick(DeltaTime);
		}
	}

	if (BackgroundVisualization)
	{
		BackgroundVisualization->OnTick(DeltaTime);
	}

	for (int i = 0; i < ToolInputObjects.Num(); ++i)
	{
		TObjectPtr<UUVEditorToolMeshInput> ToolInput = ToolInputObjects[i];
		ToolInput->AppliedPreview->Tick(DeltaTime);
		ToolInput->UnwrapPreview->Tick(DeltaTime);
	}
}


void UUVEditorMode::SwitchActiveAsset(const FString& UVAsset)
{
	if (UVChannelProperties)
	{
		// Not doing an ensure here because the "revert to default" can give us an empty string
		UVChannelProperties->ValidateUVAssetSelection(true);
		UpdateSelectedLayer();
		bForceRebuildUVLayer = false;
	}
}

void UUVEditorMode::UpdateSelectedLayer()
{
	int32 AssetID = UVChannelProperties->GetSelectedAssetID();
	if (!ensure(AssetID != INDEX_NONE))
	{
		return;
	}
	const TArray<FString>& ChannelNames = UVChannelProperties->GetUVChannelNames();

	// Find the currently selected layer for the current asset
	bool bFound = false;
	for (auto InputObject : ToolInputObjects)
	{
		if (InputObject->AssetID == AssetID)
		{
			bFound = true;

			UVChannelProperties->UVChannel = ChannelNames[InputObject->UVLayerIndex];
			UVChannelProperties->SilentUpdateWatched();
			break;
		}
	}
	if (!ensure(bFound))
	{
		UVChannelProperties->UVChannel = TEXT("");
	}
	else 
	{
		PendingUVLayerIndex[AssetID] = ToolInputObjects[AssetID]->UVLayerIndex;
	}		
}


void UUVEditorMode::SwitchActiveChannel(const FString& UVChannel)
{
	using namespace UVEditorModeLocals;

	if (UVChannelProperties)
	{
		// Not doing an ensure because the "revert to default" can give us an empty string
		UVChannelProperties->ValidateUVChannelSelection(true);
		int32 NewUVLayerIndex = UVChannelProperties->GetSelectedChannelIndex();
		int32 AssetID = UVChannelProperties->GetSelectedAssetID();
		if (!ensure(AssetID != INDEX_NONE))
		{
			return;
		}

		if (ensure(NewUVLayerIndex != IndexConstants::InvalidID))
		{
			PendingUVLayerIndex[AssetID] = NewUVLayerIndex;
		}
	}
	bForceRebuildUVLayer = false;
}

void UUVEditorMode::ChangeInputObjectLayer(int32 AssetID, int32 NewLayerIndex, bool bForceRebuild)
{
	using namespace UVEditorModeLocals;

	bool bFound = false;
	for (auto InputObject : ToolInputObjects)
	{
		if (InputObject->AssetID == AssetID)
		{
			bFound = true;

			if (InputObject->UVLayerIndex != NewLayerIndex || bForceRebuild)
			{
				InputObject->UVLayerIndex = NewLayerIndex;
				InputObject->UpdateAllFromAppliedCanonical();
			}
			break;
		}
	}

	ensure(bFound);
}

void UUVEditorMode::ForceUpdateDisplayChannel(const TArray<int32>& LayerPerAsset, bool bForceRebuildUnwrap, bool bEmitUndoTransaction)
{
	for (int32 AssetID = 0; AssetID < ToolInputObjects.Num(); ++AssetID)
	{
		if (ToolInputObjects[AssetID]->UVLayerIndex != LayerPerAsset[AssetID] || bForceRebuildUnwrap)
		{			
			if (bEmitUndoTransaction)
			{
				GetInteractiveToolsContext()->GetTransactionAPI()->AppendChange(this,
					MakeUnique<UVEditorModeLocals::FInputObjectUVLayerChange>(AssetID, ToolInputObjects[AssetID]->UVLayerIndex, LayerPerAsset[AssetID]),
					UVEditorModeLocals::UVLayerChangeTransactionName);
			}

			ChangeInputObjectLayer(AssetID, LayerPerAsset[AssetID], true);
			PendingUVLayerIndex[AssetID] = LayerPerAsset[AssetID];
		}
	}
	UpdateSelectedLayer();
}

#undef LOCTEXT_NAMESPACE
