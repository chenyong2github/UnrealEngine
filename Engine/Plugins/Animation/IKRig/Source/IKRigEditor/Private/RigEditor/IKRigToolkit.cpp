// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigToolkit.h"

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

#include "IKRigDefinition.h"
#include "RigEditor/IKRigAnimInstance.h"
#include "RigEditor/IKRigCommands.h"
#include "RigEditor/IKRigEditMode.h"
#include "RigEditor/IKRigMode.h"
#include "RigEditor/IKRigEditorController.h"

#define LOCTEXT_NAMESPACE "IKRigEditorToolkit"

const FName IKRigEditorModes::IKRigEditorMode("IKRigEditorMode");
const FName IKRigEditorAppName = FName(TEXT("IKRigEditorApp"));

FIKRigEditorToolkit::FIKRigEditorToolkit()
	: EditorController(MakeShared<FIKRigEditorController>())
{
}

FIKRigEditorToolkit::~FIKRigEditorToolkit()
{
}

void FIKRigEditorToolkit::InitAssetEditor(
	const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, 
    UIKRigDefinition* IKRigAsset)
{
	EditorController->AssetController = UIKRigController::GetIKRigController(IKRigAsset);
	EditorController->EditorToolkit = SharedThis(this);

	BindCommands();
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FIKRigEditorToolkit::HandlePreviewSceneCreated);
	
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	PersonaToolkit = PersonaModule.CreatePersonaToolkit(IKRigAsset, PersonaToolkitArgs);
	
	// when/if preview mesh is changed, we need to reinitialize the anim instance
    PersonaToolkit->GetPreviewScene()->RegisterOnPreviewMeshChanged(FOnPreviewMeshChanged::CreateSP(this, &FIKRigEditorToolkit::HandlePreviewMeshChanged));

	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(IKRigAsset);
	AssetFamily->RecordAssetOpened(FAssetData(IKRigAsset));

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode, 
		InitToolkitHost, 
		IKRigEditorAppName, 
		FTabManager::FLayout::NullLayout, 
		bCreateDefaultStandaloneMenu, 
		bCreateDefaultToolbar, 
		IKRigAsset);

	AddApplicationMode(
		IKRigEditorModes::IKRigEditorMode,
		MakeShareable(new FIKRigMode(SharedThis(this), PersonaToolkit->GetPreviewScene())));

	SetCurrentMode(IKRigEditorModes::IKRigEditorMode);

	GetEditorModeManager().SetDefaultMode(FIKRigEditMode::ModeName);
	GetEditorModeManager().ActivateMode(FPersonaEditModes::SkeletonSelection);
	GetEditorModeManager().ActivateMode(FIKRigEditMode::ModeName);
	static_cast<FIKRigEditMode*>(GetEditorModeManager().GetActiveMode(FIKRigEditMode::ModeName))->SetEditorController(EditorController);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FIKRigEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_IKRigEditor", "IK Rig Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FIKRigEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FIKRigEditorToolkit::BindCommands()
{
	const FIKRigCommands& Commands = FIKRigCommands::Get();

	ToolkitCommands->MapAction(
        Commands.Reset,
        FExecuteAction::CreateSP(this, &FIKRigEditorToolkit::HandleReset),
		EUIActionRepeatMode::RepeatDisabled);
}

void FIKRigEditorToolkit::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateSP(this, &FIKRigEditorToolkit::FillToolbar)
    );
}

void FIKRigEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Reset");
	{
		ToolbarBuilder.AddToolBarButton(FIKRigCommands::Get().Reset,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FIKRigEditorStyle::Get().GetStyleSetName(), "IKRig.Reset"));
	}
	ToolbarBuilder.EndSection();
}

FName FIKRigEditorToolkit::GetToolkitFName() const
{
	return FName("IKRigEditor");
}

FText FIKRigEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("IKRigEditorAppLabel", "IK Rig Editor");
}

FText FIKRigEditorToolkit::GetToolkitName() const
{
	const bool bDirtyState = EditorController->AssetController->GetAsset()->GetOutermost()->IsDirty();

	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(EditorController->AssetController->GetAsset()->GetName()));
	Args.Add(TEXT("DirtyState"), bDirtyState ? FText::FromString(TEXT("*")) : FText::GetEmpty());
	return FText::Format(LOCTEXT("DemoEditorToolkitName", "{AssetName}{DirtyState}"), Args);
}

FLinearColor FIKRigEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FIKRigEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("IKRigEditor");
}

void FIKRigEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	UIKRigDefinition* Asset = EditorController->AssetController->GetAsset();
	Collector.AddReferencedObject(Asset);
}

TStatId FIKRigEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRigEditorToolkit, STATGROUP_Tickables);
}

void FIKRigEditorToolkit::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);
	
	// create the preview skeletal mesh component
	EditorController->SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);

	// setup an apply an anim instance to the skeletal mesh component
	UIKRigAnimInstance* IKRigAnimInstance = NewObject<UIKRigAnimInstance>(EditorController->SkelMeshComponent, TEXT("IKRigAnimScriptInstance"));
	SetupAnimInstance(IKRigAnimInstance);

	// set the skeletal mesh on the component
	// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
	USkeletalMesh* Mesh = EditorController->AssetController->GetSourceSkeletalMesh();
	EditorController->SkelMeshComponent->SetSkeletalMesh(Mesh);

	// apply mesh to the preview scene
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorController->SkelMeshComponent);
	InPersonaPreviewScene->AddComponent(EditorController->SkelMeshComponent, FTransform::Identity);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);
	InPersonaPreviewScene->SetPreviewMesh(Mesh);
}

void FIKRigEditorToolkit::HandlePreviewMeshChanged(USkeletalMesh* InOldSkeletalMesh, USkeletalMesh* InNewSkeletalMesh)
{
	if (!InOldSkeletalMesh)
	{
		return; // first time setup
	}
	
	// update asset with new skeletal mesh (will copy new skeleton data)
	EditorController->AssetController->SetSourceSkeletalMesh(InNewSkeletalMesh, false);

	// update anim instance to use new skeletal mesh
	// this is required so that the bone containers passed around during update/eval are correctly sized
	UDebugSkelMeshComponent* EditorSkelComp = Cast<UDebugSkelMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		bool bWasCreated = false;
		UIKRigAnimInstance* AnimInstance = FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UIKRigAnimInstance>(EditorSkelComp , bWasCreated);
		SetupAnimInstance(AnimInstance);
	}

	EditorController->SkelMeshComponent->SetSkeletalMesh(InNewSkeletalMesh);
}

void FIKRigEditorToolkit::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView)
{
	EditorController->DetailsView = InDetailsView;
	EditorController->DetailsView->OnFinishedChangingProperties().AddSP(this, &FIKRigEditorToolkit::OnFinishedChangingDetails);
	EditorController->ShowEmptyDetails();
}

void FIKRigEditorToolkit::OnFinishedChangingDetails(
    const FPropertyChangedEvent& PropertyChangedEvent)
{
	UE_LOG(LogTemp, Log, TEXT("FIKRigEditorToolkit::OnFinishedChangingProperties"));
}

void FIKRigEditorToolkit::HandleReset()
{
	EditorController.Get().Reset();
}

void FIKRigEditorToolkit::SetupAnimInstance(UIKRigAnimInstance* InAnimInstance)
{
	InAnimInstance->SetIKRigAsset(EditorController->AssetController->GetAsset());
	EditorController->SkelMeshComponent->PreviewInstance = InAnimInstance;
	InAnimInstance->InitializeAnimation();
}


#undef LOCTEXT_NAMESPACE
