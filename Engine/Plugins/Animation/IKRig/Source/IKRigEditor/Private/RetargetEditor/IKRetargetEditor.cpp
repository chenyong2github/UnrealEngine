// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditor.h"

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
#include "Animation/DebugSkelMeshComponent.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditMode.h"
#include "RetargetEditor/IKRetargetMode.h"
#include "RetargetEditor/IKRetargetEditorController.h"

#define LOCTEXT_NAMESPACE "IKRetargeterEditor"

const FName IKRetargetEditorModes::IKRetargetEditorMode("IKRetargetEditorMode");
const FName IKRetargetEditorAppName = FName(TEXT("IKRetargetEditorApp"));

FIKRetargetEditor::FIKRetargetEditor()
	: EditorController(MakeShared<FIKRetargetEditorController>())
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

	AddApplicationMode(
		IKRetargetEditorModes::IKRetargetEditorMode,
		MakeShareable(new FIKRetargetMode(SharedThis(this), EditorController->PersonaToolkit->GetPreviewScene())));

	SetCurrentMode(IKRetargetEditorModes::IKRetargetEditorMode);

	GetEditorModeManager().SetDefaultMode(FPersonaEditModes::SkeletonSelection);
	GetEditorModeManager().ActivateMode(FIKRetargetEditMode::ModeName);
	FIKRetargetEditMode* EditMode = GetEditorModeManager().GetActiveModeTyped<FIKRetargetEditMode>(FIKRetargetEditMode::ModeName);
	EditMode->SetEditorController(EditorController);
	GetEditorModeManager().DeactivateMode(FIKRetargetEditMode::ModeName);

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
        Commands.EditRetargetPose,
        FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleEditPose),
        FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanEditPose),
        FIsActionChecked::CreateSP(EditorController,  &FIKRetargetEditorController::IsEditingPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.NewRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleNewPose),
		FCanExecuteAction(),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.DeleteRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleDeletePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanDeletePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleResetPose),
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
	ToolbarBuilder.BeginSection("Retarget Pose");
	{
		EditorController->PoseNames.Reset();
		for (const TTuple<FName, FIKRetargetPose>& Pose : EditorController->AssetController->GetRetargetPoses())
		{
			EditorController->PoseNames.Add(MakeShareable(new FName(Pose.Key)));
		}

		TSharedRef<SWidget> PoseListWidget = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("IKRetargetPoseTitleLabel", "Current Retarget Pose: "))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&EditorController->PoseNames)
			.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
			{
				return SNew(STextBlock).Text(FText::FromName(*InItem.Get()));
			})
			.OnSelectionChanged(EditorController, &FIKRetargetEditorController::OnPoseSelected)
			[
				SNew(STextBlock).Text(EditorController, &FIKRetargetEditorController::GetCurrentPoseName)
			]
		];
		ToolbarBuilder.AddWidget(PoseListWidget);
		
		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().EditRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Edit"));

		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().NewRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Plus"));

		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().DeleteRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Delete"));

		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().ResetRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Refresh"));
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
	// apply offset to the target component
	if (EditorController->TargetSkelMeshComponent)
	{
		const UIKRetargeter* Retargeter = EditorController->AssetController->GetAsset();
		
		const float TargetOffset = Retargeter->TargetActorOffset;
		EditorController->TargetSkelMeshComponent->SetRelativeLocation(FVector(TargetOffset,0,0));

		const float TargetScale = Retargeter->TargetActorScale;
		EditorController->TargetSkelMeshComponent->SetRelativeScale3D(FVector(TargetScale,TargetScale,TargetScale));
	}
}

TStatId FIKRetargetEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRetargetEditor, STATGROUP_Tickables);
}

void FIKRetargetEditor::PostUndo(bool bSuccess)
{
	const bool WasEditing = EditorController->IsEditingPose();
	
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();

	// restore pose mode state to avoid stepping out of the edition when undoing things
	// note that BroadcastNeedsReinitialized will unset it in FIKRetargetEditorController::OnRetargeterNeedsInitialized
	if (WasEditing)
	{
		EditorController->HandleEditPose();
	}
}

void FIKRetargetEditor::PostRedo(bool bSuccess)
{
	const bool WasEditing = EditorController->IsEditingPose();
	
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();
	
	// restore pose mode state to avoid stepping out of the edition when undoing things
	// note that BroadcastNeedsReinitialized will unset it in FIKRetargetEditorController::OnRetargeterNeedsInitialized
	if (WasEditing)
	{
		EditorController->HandleEditPose();
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
	InPersonaPreviewScene->SetAllowMeshHitProxies(false);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);
	EditorController->SourceSkelMeshComponent->bSelectable = false;
	EditorController->TargetSkelMeshComponent->bSelectable = false;
	InPersonaPreviewScene->SetPreviewMesh(SourceMesh);
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
	const bool bTargetChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetIKRigPropertyName();
	const bool bPreviewChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetPreviewMeshPropertyName();

	if (bTargetChanged)
	{
		EditorController->BindToIKRigAsset(EditorController->AssetController->GetAsset()->GetTargetIKRigWriteable());
		EditorController->AssetController->CleanChainMapping();
		EditorController->AssetController->AutoMapChains();
	}
	
	if (bTargetChanged || bPreviewChanged)
	{
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
		}
	
		SetupAnimInstance();

		EditorController->RefreshAllViews();
	}
}

#undef LOCTEXT_NAMESPACE
