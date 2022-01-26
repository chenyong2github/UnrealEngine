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
	if (PersonaToolkit.IsValid())
	{
		static constexpr bool bSetPreviewMeshInAsset = false;
		PersonaToolkit->SetPreviewMesh(nullptr, bSetPreviewMeshInAsset);
	}
}

void FIKRigEditorToolkit::InitAssetEditor(
	const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, 
    UIKRigDefinition* IKRigAsset)
{
	EditorController->Initialize(SharedThis(this), IKRigAsset);

	BindCommands();
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FIKRigEditorToolkit::HandlePreviewSceneCreated);
	
	const FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	PersonaToolkit = PersonaModule.CreatePersonaToolkit(IKRigAsset, PersonaToolkitArgs);
	
	
	// when/if preview mesh is changed, we need to reinitialize the anim instance
    PersonaToolkit->GetPreviewScene()->RegisterOnPreviewMeshChanged(FOnPreviewMeshChanged::CreateSP(this, &FIKRigEditorToolkit::HandlePreviewMeshChanged));

	const TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(IKRigAsset);
	AssetFamily->RecordAssetOpened(FAssetData(IKRigAsset));

	static constexpr bool bCreateDefaultStandaloneMenu = true;
	static constexpr bool bCreateDefaultToolbar = true;
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
	GetEditorModeManager().ActivateMode(FIKRigEditMode::ModeName);
	static_cast<FIKRigEditMode*>(GetEditorModeManager().GetActiveMode(FIKRigEditMode::ModeName))->SetEditorController(EditorController);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FIKRigEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_IKRigEditor", "IK Rig Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

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
		ToolbarBuilder.AddToolBarButton(
			FIKRigCommands::Get().Reset,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Refresh"));
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
	return FText::FromString(EditorController->AssetController->GetAsset()->GetName());
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
	// hold the asset we are working on
	const UIKRigDefinition* Asset = EditorController->AssetController->GetAsset();
	Collector.AddReferencedObject(Asset);
}

TStatId FIKRigEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRigEditorToolkit, STATGROUP_Tickables);
}

void FIKRigEditorToolkit::PostUndo(bool bSuccess)
{
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();
}

void FIKRigEditorToolkit::PostRedo(bool bSuccess)
{
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();
}

void FIKRigEditorToolkit::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);
	
	// create the preview skeletal mesh component
	EditorController->SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);

	// setup an apply an anim instance to the skeletal mesh component
	EditorController->AnimInstance = NewObject<UIKRigAnimInstance>(EditorController->SkelMeshComponent, TEXT("IKRigAnimScriptInstance"));
	SetupAnimInstance();

	// set the skeletal mesh on the component
	// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
	USkeletalMesh* Mesh = EditorController->AssetController->GetSkeletalMesh();
	EditorController->SkelMeshComponent->SetSkeletalMesh(Mesh);

	// apply mesh to the preview scene
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorController->SkelMeshComponent);
	InPersonaPreviewScene->SetAllowMeshHitProxies(false);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);
	EditorController->SkelMeshComponent->bSelectable = false;
	InPersonaPreviewScene->SetPreviewMesh(Mesh);
	InPersonaPreviewScene->AddComponent(EditorController->SkelMeshComponent, FTransform::Identity);
}

void FIKRigEditorToolkit::HandlePreviewMeshChanged(USkeletalMesh* InOldSkeletalMesh, USkeletalMesh* InNewSkeletalMesh)
{
	if (InOldSkeletalMesh == InNewSkeletalMesh)
	{
		return; // already set to this skeletal mesh
	}

	// we do not reset the current skeletal mesh to keep track of the last valid one but we still need to reinit 
	if (!InNewSkeletalMesh)
	{
		EditorController->AssetController->BroadcastNeedsReinitialized();
		return;
	}
	
	// update asset with new skeletal mesh (will copy new skeleton data)
	if (!EditorController->AssetController->SetSkeletalMesh(InNewSkeletalMesh))
	{
		return; // mesh was not set (incompatible for some reason) todo, show reason in UI someplace
	}

	// update anim instance to use new skeletal mesh
	// this is required so that the bone containers passed around during update/eval are correctly sized
	UDebugSkelMeshComponent* EditorSkelComp = Cast<UDebugSkelMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		bool bWasCreated = false;
		EditorController->AnimInstance = FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UIKRigAnimInstance>(EditorSkelComp , bWasCreated);
		SetupAnimInstance();
	}

	EditorController->SkelMeshComponent->SetSkeletalMesh(InNewSkeletalMesh);
	
	EditorController->RefreshAllViews();
}

void FIKRigEditorToolkit::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView) const
{
	EditorController->SetDetailsView(InDetailsView);
	EditorController->ShowEmptyDetails();
}

void FIKRigEditorToolkit::HandleReset()
{
	EditorController.Get().Reset();
}

void FIKRigEditorToolkit::SetupAnimInstance()
{
	UIKRigAnimInstance* InAnimInstance = EditorController->AnimInstance.Get();
	InAnimInstance->SetIKRigAsset(EditorController->AssetController->GetAsset());
	EditorController->SkelMeshComponent->PreviewInstance = InAnimInstance;
	InAnimInstance->InitializeAnimation();
	EditorController->OnIKRigNeedsInitialized(EditorController->AssetController->GetAsset());
}


#undef LOCTEXT_NAMESPACE
