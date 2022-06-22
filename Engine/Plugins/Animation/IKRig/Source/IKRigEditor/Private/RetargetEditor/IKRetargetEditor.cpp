// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditor.h"

#include "Persona/Private/AnimationEditorViewportClient.h"
#include "AnimationEditorPreviewActor.h"
#include "EditorModeManager.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "PersonaModule.h"
#include "IPersonaToolkit.h"
#include "IAssetFamily.h"
#include "ISkeletonEditorModule.h"
#include "Preferences/PersonaOptions.h"
#include "AnimCustomInstanceHelper.h"
#include "IPersonaViewport.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RetargetEditor/IKRetargetApplicationMode.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetEditorController.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "IKRetargeterEditor"

const FName IKRetargetApplicationModes::IKRetargetApplicationMode("IKRetargetApplicationMode");
const FName IKRetargetEditorAppName = FName(TEXT("IKRetargetEditorApp"));

FIKRetargetEditor::FIKRetargetEditor()
	: EditorController(MakeShared<FIKRetargetEditorController>())
	, PreviousTime(-1.0f)
{
}

FIKRetargetEditor::~FIKRetargetEditor()
{
}

void FIKRetargetEditor::InitAssetEditor(
	const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, 
    UIKRetargeter* InAsset)
{
	EditorController->Initialize(SharedThis(this), InAsset);

	BindCommands();
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FIKRetargetEditor::HandlePreviewSceneCreated);
	
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	EditorController->PersonaToolkit = PersonaModule.CreatePersonaToolkit(InAsset, PersonaToolkitArgs);
	
	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(InAsset);
	AssetFamily->RecordAssetOpened(FAssetData(InAsset));

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode, 
		InitToolkitHost, 
		IKRetargetEditorAppName, 
		FTabManager::FLayout::NullLayout, 
		bCreateDefaultStandaloneMenu, 
		bCreateDefaultToolbar, 
		InAsset);

	// this sets the application mode which defines the tab factory that builds the editor layout
	AddApplicationMode(
		IKRetargetApplicationModes::IKRetargetApplicationMode,
		MakeShareable(new FIKRetargetApplicationMode(SharedThis(this),EditorController->PersonaToolkit->GetPreviewScene())));
	SetCurrentMode(IKRetargetApplicationModes::IKRetargetApplicationMode);

	// set the default editing mode to use in the editor
	GetEditorModeManager().SetDefaultMode(FIKRetargetDefaultMode::ModeName);
	
	// give default editing mode a pointer to the editor controller
	GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);
	FIKRetargetDefaultMode* DefaultMode = GetEditorModeManager().GetActiveModeTyped<FIKRetargetDefaultMode>(FIKRetargetDefaultMode::ModeName);
	DefaultMode->SetEditorController(EditorController);

	// give edit pose mode a pointer to the editor controller
	GetEditorModeManager().ActivateMode(FIKRetargetEditPoseMode::ModeName);
	FIKRetargetEditPoseMode* EditPoseMode = GetEditorModeManager().GetActiveModeTyped<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName);
	EditPoseMode->SetEditorController(EditorController);
	GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FIKRetargetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_IKRigEditor", "IK Rig Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FIKRetargetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FIKRetargetEditor::BindCommands()
{
	const FIKRetargetCommands& Commands = FIKRetargetCommands::Get();

	ToolkitCommands->MapAction(
		Commands.GoToRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleGoToRetargetPose),
		FCanExecuteAction(),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);
	
	ToolkitCommands->MapAction(
        Commands.EditRetargetPose,
        FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleEditPose),
        FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanEditPose),
        FIsActionChecked::CreateSP(EditorController,  &FIKRetargetEditorController::IsEditingPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetAllBones,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleResetAllBones),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanResetPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetSelectedBones,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleResetSelectedBones),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanResetSelected),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetSelectedAndChildrenBones,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleResetSelectedAndChildrenBones),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanResetSelected),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.NewRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleNewPose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanCreatePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.DuplicateRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleDuplicatePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanCreatePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.DeleteRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleDeletePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanDeletePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.RenameRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleRenamePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanRenamePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ImportRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleImportPose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanCreatePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ImportRetargetPoseFromAnim,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleImportPoseFromSequence),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanCreatePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ExportRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleExportPose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanCreatePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);
}

void FIKRetargetEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateSP(this, &FIKRetargetEditor::FillToolbar)
    );
}

void FIKRetargetEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Go To Retarget Pose");
	{
		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().GoToRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(),"GenericStop"));
	}
	ToolbarBuilder.EndSection();
}

FName FIKRetargetEditor::GetToolkitFName() const
{
	return FName("IKRetargetEditor");
}

FText FIKRetargetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("IKRetargetEditorAppLabel", "IK Retarget Editor");
}

FText FIKRetargetEditor::GetToolkitName() const
{
	return FText::FromString(EditorController->AssetController->GetAsset()->GetName());
}

FLinearColor FIKRetargetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FIKRetargetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("IKRetargetEditor");
}

void FIKRetargetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	// hold the asset we are working on
	const UIKRetargeter* Retargeter = EditorController->AssetController->GetAsset();
	Collector.AddReferencedObject(Retargeter);
}

void FIKRetargetEditor::Tick(float DeltaTime)
{
	// update with latest offsets
	EditorController->AddOffsetToMeshComponent(FVector::ZeroVector, EditorController->SourceSkelMeshComponent);
	EditorController->AddOffsetToMeshComponent(FVector::ZeroVector, EditorController->TargetSkelMeshComponent);

	// retargeter IK planting must be reset when time is reversed or playback jumps ahead 
	const float CurrentTime = EditorController->SourceAnimInstance->GetCurrentTime();
	constexpr float MaxSkipTimeBeforeReset = 0.25f;
	if (CurrentTime < PreviousTime || CurrentTime > PreviousTime + MaxSkipTimeBeforeReset)
	{
		EditorController->ResetIKPlantingState();
	}
	PreviousTime = CurrentTime;
}

TStatId FIKRetargetEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRetargetEditor, STATGROUP_Tickables);
}

void FIKRetargetEditor::PostUndo(bool bSuccess)
{
	EditorController->OnRetargeterNeedsInitialized(EditorController->AssetController->GetAsset());
}

void FIKRetargetEditor::PostRedo(bool bSuccess)
{
	EditorController->OnRetargeterNeedsInitialized(EditorController->AssetController->GetAsset());
}

void FIKRetargetEditor::HandleViewportCreated(const TSharedRef<class IPersonaViewport>& InViewport)
{
	// register callbacks to allow the asset to store the Bone Size viewport setting
	FEditorViewportClient& ViewportClient = InViewport->GetViewportClient();
	if (FAnimationViewportClient* AnimViewportClient = static_cast<FAnimationViewportClient*>(&ViewportClient))
	{
		AnimViewportClient->OnSetBoneSize.BindLambda([this](float InBoneSize)
		{
			if (UIKRetargeter* Asset = EditorController->AssetController->GetAsset())
			{
				Asset->Modify();
				Asset->BoneDrawSize = InBoneSize;
			}
		});
		
		AnimViewportClient->OnGetBoneSize.BindLambda([this]() -> float
		{
			if (const UIKRetargeter* Asset = EditorController->AssetController->GetAsset())
			{
				return Asset->BoneDrawSize;
			}

			return 1.0f;
		});
	}
}

void FIKRetargetEditor::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);
	
	// create the skeletal mesh components
	EditorController->SourceSkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
	EditorController->TargetSkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);

	// setup an apply an anim instance to the skeletal mesh component
	EditorController->SourceAnimInstance = NewObject<UAnimPreviewInstance>(EditorController->SourceSkelMeshComponent, TEXT("IKRetargetSourceAnimScriptInstance"));
	EditorController->TargetAnimInstance = NewObject<UIKRetargetAnimInstance>(EditorController->TargetSkelMeshComponent, TEXT("IKRetargetTargetAnimScriptInstance"));
	SetupAnimInstance();
	
	// set the source and target skeletal meshes on the component
	// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
	USkeletalMesh* SourceMesh = EditorController->GetSourceSkeletalMesh();
	USkeletalMesh* TargetMesh = EditorController->GetTargetSkeletalMesh();
	EditorController->SourceSkelMeshComponent->SetSkeletalMesh(SourceMesh);
	EditorController->TargetSkelMeshComponent->SetSkeletalMesh(TargetMesh);

	// apply mesh to the preview scene
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorController->SourceSkelMeshComponent);
	InPersonaPreviewScene->SetPreviewMesh(SourceMesh);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);

	// SetPreviewMesh() sets this flag true, which the render uses to filter out objects for selection highlighting...
	// but since we want to be able to select the mesh in this viewport, we have to set it back to false
	EditorController->SourceSkelMeshComponent->bCanHighlightSelectedSections = false;
	
	InPersonaPreviewScene->AddComponent(EditorController->SourceSkelMeshComponent, FTransform::Identity);
	InPersonaPreviewScene->AddComponent(EditorController->TargetSkelMeshComponent, FTransform::Identity);
}

void FIKRetargetEditor::SetupAnimInstance()
{
	// connect the retarget asset and the source component ot the target anim instance
	EditorController->TargetAnimInstance->SetRetargetAssetAndSourceComponent(EditorController->AssetController->GetAsset(), EditorController->SourceSkelMeshComponent);

	EditorController->SourceSkelMeshComponent->PreviewInstance = EditorController->SourceAnimInstance.Get();
	EditorController->TargetSkelMeshComponent->PreviewInstance = EditorController->TargetAnimInstance.Get();

	EditorController->SourceAnimInstance->InitializeAnimation();
	EditorController->TargetAnimInstance->InitializeAnimation();
}

void FIKRetargetEditor::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView)
{
	EditorController->DetailsView = InDetailsView;
	EditorController->DetailsView->OnFinishedChangingProperties().AddSP(this, &FIKRetargetEditor::OnFinishedChangingDetails);
	EditorController->DetailsView->SetObject(EditorController->AssetController->GetAsset());
}

void FIKRetargetEditor::OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent)
{
	const UIKRetargeterController* AssetController = EditorController->AssetController;

	// determine which properties were modified
	const bool bSourceIKRigChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetSourceIKRigPropertyName();
	const bool bTargetIKRigChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetIKRigPropertyName();
	const bool bSourcePreviewChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetSourcePreviewMeshPropertyName();
	const bool bTargetPreviewChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetPreviewMeshPropertyName();

	// if no override target mesh has been specified, update the override to reflect the mesh in the ik rig asset
	if (bTargetIKRigChanged)
	{
		AssetController->OnTargetIKRigChanged();
	}

	// if no override source mesh has been specified, update the override to reflect the mesh in the ik rig asset
	if (bSourceIKRigChanged)
	{
		AssetController->OnSourceIKRigChanged();
	}

	// if either IK Rig asset has been modified, rebind and refresh UI
	if (bTargetIKRigChanged || bSourceIKRigChanged)
	{
		EditorController->ClearOutputLog();
		EditorController->BindToIKRigAsset(AssetController->GetAsset()->GetTargetIKRigWriteable());
		EditorController->BindToIKRigAsset(AssetController->GetAsset()->GetSourceIKRigWriteable());
		EditorController->AssetController->CleanChainMapping();
		EditorController->AssetController->AutoMapChains();
	}

	// if either the source or target meshes are possibly modified, update scene components, anim instance and UI
	if (bTargetIKRigChanged || bSourceIKRigChanged || bTargetPreviewChanged || bSourcePreviewChanged)
	{
		EditorController->ClearOutputLog();
		
		// set the source and target skeletal meshes on the component
		// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
		USkeletalMesh* SourceMesh = EditorController->GetSourceSkeletalMesh();
		USkeletalMesh* TargetMesh = EditorController->GetTargetSkeletalMesh();
		EditorController->SourceSkelMeshComponent->SetSkeletalMesh(SourceMesh);
		EditorController->TargetSkelMeshComponent->SetSkeletalMesh(TargetMesh);
	
		// apply mesh to the preview scene
		TSharedRef<IPersonaPreviewScene> PreviewScene = GetPersonaToolkit()->GetPreviewScene();
		if (PreviewScene->GetPreviewMesh() != SourceMesh)
		{
			PreviewScene->SetPreviewMeshComponent(EditorController->SourceSkelMeshComponent);
			PreviewScene->SetPreviewMesh(SourceMesh);
			EditorController->SourceSkelMeshComponent->bCanHighlightSelectedSections = false;
		}
	
		SetupAnimInstance();

		EditorController->RefreshAllViews();
	}
}

#undef LOCTEXT_NAMESPACE
