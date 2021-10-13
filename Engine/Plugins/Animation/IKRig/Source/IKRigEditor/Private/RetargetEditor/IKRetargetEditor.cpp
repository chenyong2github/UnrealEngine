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

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"

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
	EditorController->Editor = SharedThis(this);
	EditorController->Asset = InAsset;

	BindCommands();
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FIKRetargetEditor::HandlePreviewSceneCreated);
	
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	EditorController->PersonaToolkit = PersonaModule.CreatePersonaToolkit(InAsset, PersonaToolkitArgs, InAsset->GetSourceSkeletonAsset());
	
	// when/if preview mesh is changed, we need to reinitialize the anim instance
    EditorController->PersonaToolkit->GetPreviewScene()->RegisterOnPreviewMeshChanged(FOnPreviewMeshChanged::CreateSP(this, &FIKRetargetEditor::HandlePreviewMeshChanged));

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
        FExecuteAction::CreateSP(this, &FIKRetargetEditor::HandleEditPose),
        FCanExecuteAction::CreateSP(this, &FIKRetargetEditor::CanEditPose),
        FIsActionChecked::CreateSP(this,  &FIKRetargetEditor::IsEditingPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.NewRetargetPose,
		FExecuteAction::CreateSP(this, &FIKRetargetEditor::HandleNewPose),
		FCanExecuteAction(),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.DeleteRetargetPose,
		FExecuteAction::CreateSP(this, &FIKRetargetEditor::HandleDeletePose),
		FCanExecuteAction::CreateSP(this, &FIKRetargetEditor::CanDeletePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetRetargetPose,
		FExecuteAction::CreateSP(this, &FIKRetargetEditor::HandleResetPose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);
		
	ToolkitCommands->MapAction(
		Commands.ExportAnimation,
		FExecuteAction::CreateSP(this, &FIKRetargetEditor::ExportAnimation),
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
		PoseNames.Reset();
		for (TTuple<FName, FIKRetargetPose>& Pose : EditorController->Asset->RetargetPoses)
		{
			PoseNames.Add(MakeShareable(new FName(Pose.Key)));
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
			.OptionsSource(&PoseNames)
			.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
			{
				return SNew(STextBlock).Text(FText::FromName(*InItem.Get()));
			})
			.OnSelectionChanged(this, &FIKRetargetEditor::OnPoseSelected)
			[
				SNew(STextBlock).Text(this, &FIKRetargetEditor::GetCurrentPoseName)
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
	const bool bDirtyState = EditorController->Asset->GetOutermost()->IsDirty();

	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(EditorController->Asset->GetName()));
	Args.Add(TEXT("DirtyState"), bDirtyState ? FText::FromString(TEXT("*")) : FText::GetEmpty());
	return FText::Format(LOCTEXT("RetargetEditorToolkitName", "{AssetName}{DirtyState}"), Args);
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
	Collector.AddReferencedObject(EditorController->Asset);
}

void FIKRetargetEditor::Tick(float DeltaTime)
{
	// apply offset to the target component
	if (EditorController->TargetSkelMeshComponent)
	{
		const float TargetOffset = EditorController->Asset->TargetActorOffset;
		EditorController->TargetSkelMeshComponent->SetRelativeLocation(FVector(TargetOffset,0,0));

		const float TargetScale = EditorController->Asset->TargetActorScale;
		EditorController->TargetSkelMeshComponent->SetRelativeScale3D(FVector(TargetScale,TargetScale,TargetScale));
	}
}

TStatId FIKRetargetEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRetargetEditor, STATGROUP_Tickables);
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
	EditorController->TargetAnimInstance->SetRetargetAssetAndSourceComponent(EditorController->Asset, EditorController->SourceSkelMeshComponent);

	EditorController->SourceSkelMeshComponent->PreviewInstance = EditorController->SourceAnimInstance.Get();
	EditorController->TargetSkelMeshComponent->PreviewInstance = EditorController->TargetAnimInstance.Get();

	EditorController->SourceAnimInstance->InitializeAnimation();
	EditorController->TargetAnimInstance->InitializeAnimation();
}

FText FIKRetargetEditor::GetCurrentPoseName() const
{
	return FText::FromName(EditorController->Asset->CurrentRetargetPose);
}

void FIKRetargetEditor::OnPoseSelected(TSharedPtr<FName> InPosePose, ESelectInfo::Type SelectInfo)
{
	EditorController->Asset->SetCurrentRetargetPose(*InPosePose.Get());
}

void FIKRetargetEditor::ExportAnimation() const
{
}

void FIKRetargetEditor::HandleSourceOrTargetIKRigAssetChanged()
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

void FIKRetargetEditor::HandlePreviewMeshChanged(USkeletalMesh* InOldSkeletalMesh, USkeletalMesh* InNewSkeletalMesh)
{
	if (!InOldSkeletalMesh)
	{
		return; // first time setup
	}
	
	HandleSourceOrTargetIKRigAssetChanged();
}

void FIKRetargetEditor::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView)
{
	EditorController->DetailsView = InDetailsView;
	EditorController->DetailsView->OnFinishedChangingProperties().AddSP(this, &FIKRetargetEditor::OnFinishedChangingDetails);
	EditorController->DetailsView->SetObject(EditorController->Asset);
}

void FIKRetargetEditor::OnFinishedChangingDetails(
    const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == "TargetIKRigAsset"
		|| PropertyChangedEvent.GetPropertyName() == "SourceIKRigAsset")
	{
		HandleSourceOrTargetIKRigAssetChanged();
	}
}

void FIKRetargetEditor::HandleEditPose() const
{
	UIKRetargeter* Asset = EditorController.Get().Asset;
	Asset->bEditReferencePoseMode = !EditorController.Get().Asset->bEditReferencePoseMode;
	if (Asset->bEditReferencePoseMode)
	{
		GetEditorModeManager().ActivateMode(FIKRetargetEditMode::ModeName);
		EditorController->SourceSkelMeshComponent->ShowReferencePose(true);
	}
	else
	{
		GetEditorModeManager().DeactivateMode(FIKRetargetEditMode::ModeName);
		EditorController->PlayPreviousAnimationAsset();
	}

	Asset->Modify();
}

bool FIKRetargetEditor::CanEditPose() const
{
	UIKRetargeter* CurrentRetargeter = EditorController->GetCurrentlyRunningRetargeter();
	if (!CurrentRetargeter)
	{
		return false;
	}

	return CurrentRetargeter->bIsLoadedAndValid;
}

bool FIKRetargetEditor::IsEditingPose() const
{
	return EditorController.Get().Asset->bEditReferencePoseMode;
}

void FIKRetargetEditor::HandleNewPose()
{
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("NewRetargetPoseOptions", "Create New Retarget Pose"))
	.ClientSize(FVector2D(250, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SAssignNew(NewPoseEditableText, SEditableTextBox)
				.Text(LOCTEXT("NewRetargetPoseName", "NewPose"))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditor::CreateNewPose)
				]
				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [=]()
					{
						NewPoseWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(NewPoseWindow.ToSharedRef());
	NewPoseWindow.Reset();
}

FReply FIKRetargetEditor::CreateNewPose()
{
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	EditorController->Asset->AddRetargetPose(NewPoseName);
	NewPoseWindow->RequestDestroyWindow();
	RegenerateMenusAndToolbars();
	return FReply::Handled();
}

void FIKRetargetEditor::HandleDeletePose()
{
	const FName CurrentPose = EditorController->Asset->CurrentRetargetPose;
	EditorController->Asset->RemoveRetargetPose(CurrentPose);
	RegenerateMenusAndToolbars();
}

bool FIKRetargetEditor::CanDeletePose() const
{	
	// cannot delete default pose
	return EditorController->Asset->CurrentRetargetPose != UIKRetargeter::DefaultPoseName;
}

void FIKRetargetEditor::HandleResetPose()
{
	const FName CurrentPose = EditorController->Asset->CurrentRetargetPose;
	EditorController->Asset->ResetRetargetPose(CurrentPose);
}


#undef LOCTEXT_NAMESPACE
