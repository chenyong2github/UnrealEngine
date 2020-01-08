// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditor.h"
#include "Modules/ModuleManager.h"
#include "ControlRigEditorModule.h"
#include "ControlRigBlueprint.h"
#include "SBlueprintEditorToolbar.h"
#include "ControlRigEditorMode.h"
#include "SKismetInspector.h"
#include "Framework/Commands/GenericCommands.h"
#include "Editor.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "BlueprintActionDatabase.h"
#include "ControlRigBlueprintCommands.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "IPersonaToolkit.h"
#include "PersonaModule.h"
#include "ControlRigEditorEditMode.h"
#include "ControlRigEditModeSettings.h"
#include "AssetEditorModeManager.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "IPersonaPreviewScene.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "ControlRig.h"
#include "ControlRigSkeletalMeshComponent.h"
#include "ControlRigSkeletalMeshBinding.h"
#include "ControlRigBlueprintUtils.h"
#include "EditorViewportClient.h"
#include "AnimationEditorPreviewActor.h"
#include "Misc/MessageDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ControlRigEditorStyle.h"
#include "EditorFontGlyphs.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "SRigHierarchy.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_BoneName.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneRotation.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneTranslation.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetRelativeBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetRelativeBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetInitialBoneTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_AddBoneTransform.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetControlTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetSpaceTransform.h"
#include "Graph/NodeSpawners/ControlRigUnitNodeSpawner.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRigObjectVersion.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_Comment.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SNodePanel.h"
#include "Kismet/Private/SMyBlueprint.h"
#include "Kismet/Private/SBlueprintEditorSelectedDebugObjectWidget.h"

#define LOCTEXT_NAMESPACE "ControlRigEditor"

const FName ControlRigEditorAppName(TEXT("ControlRigEditorApp"));

const FName FControlRigEditorModes::ControlRigEditorMode("Rigging");

namespace ControlRigEditorTabs
{
	const FName DetailsTab(TEXT("DetailsTab"));
// 	const FName ViewportTab(TEXT("Viewport"));
// 	const FName AdvancedPreviewTab(TEXT("AdvancedPreviewTab"));
};

FControlRigEditor::FControlRigEditor()
	: ControlRig(nullptr)
	, bControlRigEditorInitialized(false)
	, bIsSelecting(false)
	, bIsSettingObjectBeingDebugged(false)
	, bExecutionControlRig(true)
{
}

FControlRigEditor::~FControlRigEditor()
{
	UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint();
	if (RigBlueprint)
	{
		RigBlueprint->HierarchyContainer.OnElementChanged.Clear();
		RigBlueprint->HierarchyContainer.OnElementAdded.Clear();
		RigBlueprint->HierarchyContainer.OnElementRemoved.Clear();
		RigBlueprint->HierarchyContainer.OnElementRenamed.Clear();
		RigBlueprint->HierarchyContainer.OnElementReparented.Clear();
		RigBlueprint->HierarchyContainer.OnElementSelected.Clear();
		RigBlueprint->HierarchyContainer.OnElementChanged.Clear();
		RigBlueprint->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.Clear();
 	}
}

UControlRigBlueprint* FControlRigEditor::GetControlRigBlueprint() const
{
	return Cast<UControlRigBlueprint>(GetBlueprintObj());
}

void FControlRigEditor::ExtendMenu()
{
	if(MenuExtender.IsValid())
	{
		RemoveMenuExtender(MenuExtender);
		MenuExtender.Reset();
	}

	MenuExtender = MakeShareable(new FExtender);

	AddMenuExtender(MenuExtender);

	// add extensible menu if exists
	FControlRigEditorModule& ControlRigEditorModule = FModuleManager::LoadModuleChecked<FControlRigEditorModule>("ControlRigEditor");
	AddMenuExtender(ControlRigEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FControlRigEditor::InitControlRigEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UControlRigBlueprint* InControlRigBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FPersonaModule& PersonaModule = FModuleManager::GetModuleChecked<FPersonaModule>("Persona");

	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FControlRigEditor::HandlePreviewSceneCreated);
	PersonaToolkit = PersonaModule.CreatePersonaToolkit(InControlRigBlueprint, PersonaToolkitArgs);

	// set delegate prior to setting mesh
	// otherwise, you don't get delegate
	PersonaToolkit->GetPreviewScene()->RegisterOnPreviewMeshChanged(FOnPreviewMeshChanged::CreateSP(this, &FControlRigEditor::HandlePreviewMeshChanged));

	// Set a default preview mesh, if any
	PersonaToolkit->SetPreviewMesh(InControlRigBlueprint->GetPreviewMesh(), false);

	Toolbox = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.f);

	if (!Toolbar.IsValid())
	{
		Toolbar = MakeShareable(new FBlueprintEditorToolbar(SharedThis(this)));
	}

	// Build up a list of objects being edited in this asset editor
	TArray<UObject*> ObjectsBeingEdited;
	ObjectsBeingEdited.Add(InControlRigBlueprint);

	// Initialize the asset editor and spawn tabs
	const TSharedRef<FTabManager::FLayout> DummyLayout = FTabManager::NewLayout("NullLayout")->AddArea(FTabManager::NewPrimaryArea());
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	InitAssetEditor(Mode, InitToolkitHost, ControlRigEditorAppName, DummyLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsBeingEdited);

	TArray<UBlueprint*> ControlRigBlueprints;
	ControlRigBlueprints.Add(InControlRigBlueprint);

	InControlRigBlueprint->InitializeModel();

	CommonInitialization(ControlRigBlueprints);

	for (UBlueprint* Blueprint : ControlRigBlueprints)
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph == nullptr)
			{
				continue;
			}

			RigGraph->Initialize(InControlRigBlueprint);

			if (RigGraph->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
			{
				// recompile in case this control rig requires a rebuild
				// since we've removed the Hierarchy Ref pins of the first version.
				Compile();
			}
		}

	}

	InControlRigBlueprint->OnModified().AddSP(this, &FControlRigEditor::HandleModelModified);

	BindCommands();

	AddApplicationMode(
		FControlRigEditorModes::ControlRigEditorMode,
		MakeShareable(new FControlRigEditorMode(SharedThis(this))));

	ExtendMenu();
	ExtendToolbar();
	RegenerateMenusAndToolbars();

	// Activate the initial mode (which will populate with a real layout)
	SetCurrentMode(FControlRigEditorModes::ControlRigEditorMode);

	// Activate our edit mode
//	GetAssetEditorModeManager()->SetToolkitHost(GetToolkitHost());
	if (GetAssetEditorModeManager() != nullptr)
	{
		GetAssetEditorModeManager()->SetDefaultMode(FControlRigEditorEditMode::ModeName);
		GetAssetEditorModeManager()->ActivateMode(FControlRigEditorEditMode::ModeName);
	}

	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		EditMode->OnGetRigElementTransform() = FOnGetRigElementTransform::CreateSP(this, &FControlRigEditor::GetRigElementTransform);
		EditMode->OnSetRigElementTransform() = FOnSetRigElementTransform::CreateSP(this, &FControlRigEditor::SetRigElementTransform);
		EditMode->OnContextMenu() = FNewMenuDelegate::CreateSP(this, &FControlRigEditor::HandleOnViewportContextMenuDelegate);
		EditMode->OnContextMenuCommands() = FNewMenuCommandsDelegate::CreateSP(this, &FControlRigEditor::HandleOnViewportContextMenuCommandsDelegate);
	}

	UpdateControlRig();

	// Post-layout initialization
	PostLayoutBlueprintEditorInitialization();

	if (ControlRigBlueprints.Num() > 0)
	{
		for(UEdGraph* Graph : ControlRigBlueprints[0]->UbergraphPages)
		{
			if (Graph->GetFName().IsEqual(UControlRigGraphSchema::GraphName_ControlRig))
			{
				OpenGraphAndBringToFront(Graph, false);
				break;
			}
		}

	}

	if (InControlRigBlueprint)
	{
		if (UControlRigModel* Model = InControlRigBlueprint->Model)
		{
			if (Model->Nodes().Num() == 0)
			{
				if (UControlRigController* Controller = InControlRigBlueprint->ModelController)
				{
					Controller->AddNode(FRigUnit_BeginExecution::StaticStruct()->GetFName());
				}
			}
			else
			{
				bool bWasDirty = false;
				UPackage* Package = InControlRigBlueprint->GetOutermost();
				if (Package)
				{
					bWasDirty = Package->IsDirty();
				}
				InControlRigBlueprint->RebuildGraphFromModel();
				if (Package)
				{
					if (Package->IsDirty() != bWasDirty)
					{
						Package->SetDirtyFlag(bWasDirty);
					}
				}
			}
			}

		InControlRigBlueprint->HierarchyContainer.OnElementAdded.AddSP(this, &FControlRigEditor::OnRigElementAdded);
		InControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddSP(this, &FControlRigEditor::OnRigElementRemoved);
		InControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddSP(this, &FControlRigEditor::OnRigElementRenamed);
		InControlRigBlueprint->HierarchyContainer.OnElementReparented.AddSP(this, &FControlRigEditor::OnRigElementReparented);
		InControlRigBlueprint->HierarchyContainer.OnElementSelected.AddSP(this, &FControlRigEditor::OnRigElementSelected);
		InControlRigBlueprint->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.AddSP(this, &FControlRigEditor::OnControlUISettingChanged);

		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			InControlRigBlueprint->HierarchyContainer.OnElementAdded.AddSP(EditMode, &FControlRigEditMode::OnRigElementAdded);
			InControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddSP(EditMode, &FControlRigEditMode::OnRigElementRemoved);
			InControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddSP(EditMode, &FControlRigEditMode::OnRigElementRenamed);
			InControlRigBlueprint->HierarchyContainer.OnElementReparented.AddSP(EditMode, &FControlRigEditMode::OnRigElementReparented);
			InControlRigBlueprint->HierarchyContainer.OnElementSelected.AddSP(EditMode, &FControlRigEditMode::OnRigElementSelected);
			InControlRigBlueprint->HierarchyContainer.OnElementChanged.AddSP(EditMode, &FControlRigEditMode::OnRigElementChanged);
			InControlRigBlueprint->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.AddSP(EditMode, &FControlRigEditMode::OnControlUISettingChanged);
		}
	}

	bControlRigEditorInitialized = true;
}

void FControlRigEditor::BindCommands()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().ExecuteGraph,
		FExecuteAction::CreateSP(this, &FControlRigEditor::ToggleExecuteGraph), 
		FCanExecuteAction(), 
		FIsActionChecked::CreateSP(this, &FControlRigEditor::IsExecuteGraphOn));
}

void FControlRigEditor::ToggleExecuteGraph()
{
	if (ControlRig)
	{
		bExecutionControlRig = !bExecutionControlRig;

		// enable rig element init pose change, if not executing, we allow rig element change
		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->EnableRigElementEditing(!bExecutionControlRig);
		}

		// This is required now since we update execution/input flag on update controlrig
		// @fixme: change this to just change flag instead of updating whole control rig
		// I'll do this before first check-in
		UpdateControlRig();
	}
}

bool FControlRigEditor::IsExecuteGraphOn() const
{
	return bExecutionControlRig;
}

void FControlRigEditor::ExtendToolbar()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// If the ToolbarExtender is valid, remove it before rebuilding it
	if(ToolbarExtender.IsValid())
	{
		RemoveToolbarExtender(ToolbarExtender);
		ToolbarExtender.Reset();
	}

	ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	FControlRigEditorModule& ControlRigEditorModule = FModuleManager::LoadModuleChecked<FControlRigEditorModule>("ControlRigEditor");
	AddToolbarExtender(ControlRigEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	TArray<IControlRigEditorModule::FControlRigEditorToolbarExtender> ToolbarExtenderDelegates = ControlRigEditorModule.GetAllControlRigEditorToolbarExtenders();

	for (auto& ToolbarExtenderDelegate : ToolbarExtenderDelegates)
	{
		if(ToolbarExtenderDelegate.IsBound())
		{
			AddToolbarExtender(ToolbarExtenderDelegate.Execute(GetToolkitCommands(), SharedThis(this)));
		}
	}

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FControlRigEditor::FillToolbar)
	);
}

void FControlRigEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Toolbar");
	{
		ToolbarBuilder.AddToolBarButton(FControlRigBlueprintCommands::Get().ExecuteGraph,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.ExecuteGraph"));

		ToolbarBuilder.AddWidget(SNew(SBlueprintEditorSelectedDebugObjectWidget, SharedThis(this)));
	}
	ToolbarBuilder.EndSection();
}

void FControlRigEditor::GetCustomDebugObjects(TArray<FCustomDebugObject>& DebugList) const
{
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	if (ControlRig)
	{
		FCustomDebugObject DebugObject;
		DebugObject.Object = ControlRig;
		DebugObject.NameOverride = GetCustomDebugObjectLabel(ControlRig);
		DebugList.Add(DebugObject);
	}

	UControlRigBlueprintGeneratedClass* GeneratedClass = RigBlueprint->GetControlRigBlueprintGeneratedClass();
	if (GeneratedClass)
	{
		struct Local
		{
			static bool IsPendingKillOrUnreachableRecursive(UObject* InObject)
			{
				if (InObject != nullptr)
				{
					if (InObject->IsPendingKillOrUnreachable())
					{
						return true;
					}
					return IsPendingKillOrUnreachableRecursive(InObject->GetOuter());
				}
				return false;
			}

			static bool OuterNameContainsRecursive(UObject* InObject, const FString& InStringToSearch)
			{
				if (InObject == nullptr)
				{
					return false;
				}

				UObject* InObjectOuter = InObject->GetOuter();
				if (InObjectOuter == nullptr)
				{
					return false;
				}

				if (InObjectOuter->GetName().Contains(InStringToSearch))
				{
					return true;
				}

				return OuterNameContainsRecursive(InObjectOuter, InStringToSearch);
			}
		};

		if (UObject* DefaultObject = GeneratedClass->GetDefaultObject(false))
		{
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			for (UObject* Instance : ArchetypeInstances)
			{
				UControlRig* InstanceControlRig = Cast<UControlRig>(Instance);
				if (InstanceControlRig && InstanceControlRig != ControlRig)
				{
					if (InstanceControlRig->GetOuter() == nullptr)
					{
						continue;
					}

					UWorld* World = InstanceControlRig->GetWorld();
					if (World == nullptr)
					{
						continue;
					}

					if (!World->IsGameWorld() && !World->IsPreviewWorld())
					{
						continue;
					}

					// ensure to only allow preview actors in preview worlds
					if (World->IsPreviewWorld())
					{
						if (!Local::OuterNameContainsRecursive(InstanceControlRig, TEXT("Preview")))
						{
							continue;
						}
					}

					if (Local::IsPendingKillOrUnreachableRecursive(InstanceControlRig))
					{
						continue;
					}

					FCustomDebugObject DebugObject;
					DebugObject.Object = InstanceControlRig;
					DebugObject.NameOverride = GetCustomDebugObjectLabel(InstanceControlRig);
					DebugList.Add(DebugObject);
				}
			}
		}
	}
}

void FControlRigEditor::HandleSetObjectBeingDebugged(UObject* InObject)
{
	UControlRig* DebuggedControlRig = Cast<UControlRig>(InObject);

	if (DebuggedControlRig == nullptr)
	{
		// fall back to our default control rig (which still can be nullptr)
		if (ControlRig != nullptr && GetBlueprintObj() && !bIsSettingObjectBeingDebugged)
		{
			TGuardValue<bool> GuardSettingObjectBeingDebugged(bIsSettingObjectBeingDebugged, true);
			GetBlueprintObj()->SetObjectBeingDebugged(ControlRig);
			return;
		}
	}

	if (GetBlueprintObj())
	{
		FBlueprintEditorUtils::UpdateStalePinWatches(GetBlueprintObj());
	}

	if (DebuggedControlRig)
	{
		bool bIsExternalControlRig = DebuggedControlRig != ControlRig;
		// @fixme: I'll also change the "execute" button to be not toggleable during debugging external control rig
		bool bShouldExecute = bIsExternalControlRig && bExecutionControlRig;
		DebuggedControlRig->ControlRigLog = &ControlRigLog;

		UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
		if (EditorSkelComp)
		{
			UControlRigSequencerAnimInstance* AnimInstance = Cast<UControlRigSequencerAnimInstance>(EditorSkelComp->GetAnimInstance());
			if (AnimInstance)
			{
				// we might want to move this into another method
				FInputBlendPose Filter;
				AnimInstance->UpdateControlRig(DebuggedControlRig, 0, false, false, Filter, 1.0f, bShouldExecute, bShouldExecute);
				AnimInstance->RecalcRequiredBones();

				// since rig has changed, rebuild draw skeleton
				EditorSkelComp->RebuildDebugDrawSkeleton();
				if (FControlRigEditMode* EditMode = GetEditMode())
				{
					EditMode->SetObjects(DebuggedControlRig, FGuid(), EditorSkelComp);
				}
			}
		}
	}
	else
	{
		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->SetObjects(nullptr, FGuid(), nullptr);
		}
	}
}

FString FControlRigEditor::GetCustomDebugObjectLabel(UObject* ObjectBeingDebugged) const
{
	if (ObjectBeingDebugged == nullptr)
	{
		return FString();
	}

	if (ObjectBeingDebugged == ControlRig)
	{
		return TEXT("Control Rig Editor Preview");
	}

	if (AActor* ParentActor = ObjectBeingDebugged->GetTypedOuter<AActor>())
	{
		return FString::Printf(TEXT("%s in %s"), *GetBlueprintObj()->GetName(), *ParentActor->GetName());
	}

	return GetBlueprintObj()->GetName();
}

UBlueprint* FControlRigEditor::GetBlueprintObj() const
{
	const TArray<UObject*>& EditingObjs = GetEditingObjects();
	for (UObject* Obj : EditingObjs)
	{
		if (Obj->IsA<UControlRigBlueprint>()) 
		{
			return (UBlueprint*)Obj;
		}
	}
	return nullptr;
}

void FControlRigEditor::SetDetailObjects(const TArray<UObject*>& InObjects)
{
	RigElementInDetailPanel = FRigElementKey();
	SKismetInspector::FShowDetailsOptions Options;
	Options.bForceRefresh = true;
	Inspector->ShowDetailsForObjects(InObjects);
}

void FControlRigEditor::SetDetailObject(UObject* Obj)
{
	RigElementInDetailPanel = FRigElementKey();
	TArray<UObject*> Objects;
	if (Obj)
	{
		Objects.Add(Obj);
	}
	SetDetailObjects(Objects);
}

void FControlRigEditor::SetDetailStruct(const FRigElementKey& InElement, TSharedPtr<FStructOnScope> StructToDisplay)
{
	if (RigElementInDetailPanel == InElement)
	{
		return;
	}
	RigElementInDetailPanel = InElement;
	if (StructToDisplay.IsValid())
	{
		StructToDisplay->SetPackage(GetControlRigBlueprint()->GetOutermost());
	}
	Inspector->ShowSingleStruct(StructToDisplay);
}

void FControlRigEditor::ClearDetailObject()
{
	RigElementInDetailPanel = FRigElementKey();
	Inspector->ShowDetailsForObjects(TArray<UObject*>());
	Inspector->ShowSingleStruct(TSharedPtr<FStructOnScope>());
}


void FControlRigEditor::CreateDefaultCommands() 
{
	if (GetBlueprintObj())
	{
		FBlueprintEditor::CreateDefaultCommands();
	}
	else
	{
		ToolkitCommands->MapAction( FGenericCommands::Get().Undo, 
			FExecuteAction::CreateSP( this, &FControlRigEditor::UndoAction ));
		ToolkitCommands->MapAction( FGenericCommands::Get().Redo, 
			FExecuteAction::CreateSP( this, &FControlRigEditor::RedoAction ));
	}
}

void FControlRigEditor::OnCreateGraphEditorCommands(TSharedPtr<FUICommandList> GraphEditorCommandsList)
{

}

void FControlRigEditor::Compile()
{
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		FString LastDebuggedObjectName = GetCustomDebugObjectLabel(GetBlueprintObj()->GetObjectBeingDebugged());
		GetBlueprintObj()->SetObjectBeingDebugged(nullptr);
		ClearDetailObject();

		if (ControlRig)
		{
			ControlRig->OnInitialized().Clear();
			ControlRig->OnExecuted().Clear();
		}

		{
			FBlueprintEditor::Compile();
		}

		if (ControlRig)
		{
			ControlRig->ControlRigLog = &ControlRigLog;

			UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(ControlRig->GetClass());
			if (GeneratedClass)
			{
				if (GeneratedClass->Operators.Num() == 1) // just the "done" operator
				{
					FNotificationInfo Info(LOCTEXT("ControlRigBlueprintCompilerEmptyRigMessage", "The Control Rig you compiled doesn't do anything. Did you forget to add a Begin_Execution node?"));
					Info.bFireAndForget = true;
					Info.FadeOutDuration = 10.0f;
					Info.ExpireDuration = 0.0f;
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
					NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
				}
			}
		}

		TArray<FCustomDebugObject> DebugList;
		GetCustomDebugObjects(DebugList);

		for (const FCustomDebugObject& DebugObject : DebugList)
		{
			if (DebugObject.NameOverride == LastDebuggedObjectName)
			{
				GetBlueprintObj()->SetObjectBeingDebugged(DebugObject.Object);
			}
		}
	}

	// enable this for creating a new unit test
	// DumpUnitTestCode();

	// FStatsHierarchical::EndMeasurements();
	// FMessageLog LogForMeasurements("ControlRigLog");
	// FStatsHierarchical::DumpMeasurements(LogForMeasurements);
}

FName FControlRigEditor::GetToolkitFName() const
{
	return FName("ControlRigEditor");
}

FText FControlRigEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Control Rig Editor");
}

FText FControlRigEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(GetBlueprintObj());
}

FString FControlRigEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Control Rig Editor ").ToString();
}

FLinearColor FControlRigEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.5f, 0.25f, 0.35f, 0.5f );
}

void FControlRigEditor::DeleteSelectedNodes()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	SetUISelectionState(NAME_None);

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*NodeIt))
		{
			if (Node->CanUserDeleteNode())
			{
				AnalyticsTrackNodeEvent(GetBlueprintObj(), Node, true);
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
				{
					RigBlueprint->ModelController->RemoveNode(RigNode->PropertyName);
				}
				else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
				{
					RigBlueprint->ModelController->RemoveNode(CommentNode->GetFName());
				}
				else
				{
					ensure(false);
				}
			}
		}
	}
}

void FControlRigEditor::PasteNodesHere(class UEdGraph* DestinationGraph, const FVector2D& GraphLocation)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	UControlRigGraph* TempGraph = NewObject<UControlRigGraph>(GetTransientPackage());
	TempGraph->bIsTemporaryGraphForCopyPaste = true;
	TempGraph->Schema = DestinationGraph->Schema;

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Import the nodes
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(TempGraph, TextToImport, PastedNodes);
	if (PastedNodes.Num() == 0)
	{
		return;
	}

	FVector2D AvgNodePosition(0.0f, 0.0f);
	for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
	{
		UEdGraphNode* Node = *It;
		AvgNodePosition.X += Node->NodePosX;
		AvgNodePosition.Y += Node->NodePosY;
	}

	float InvNumNodes = 1.0f / float(PastedNodes.Num());
	AvgNodePosition.X *= InvNumNodes;
	AvgNodePosition.Y *= InvNumNodes;

	RigBlueprint->ModelController->OpenUndoBracket(TEXT("Pasted Nodes."));

	TMap<FString, FString> NameMap;
	for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
	{
		UEdGraphNode* Node = *It;
		Node->NodePosX = (Node->NodePosX - AvgNodePosition.X) + GraphLocation.X;
		Node->NodePosY = (Node->NodePosY - AvgNodePosition.Y) + GraphLocation.Y;
		Node->SnapToGrid(SNodePanel::GetSnapGridSize());

		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
		{
			FVector2D NodePosition = FVector2D(Node->NodePosX, Node->NodePosY);
			UScriptStruct* ScriptStruct = RigNode->GetUnitScriptStruct();
			bool bAddedNode = false;
			if (ScriptStruct)
			{
				bAddedNode = RigBlueprint->ModelController->AddNode(ScriptStruct->GetFName(), NodePosition, RigNode->GetPropertyName());
			}
			else
			{
				FName DataType = RigNode->PinType.PinCategory;
				if (UStruct* Struct = Cast<UStruct>(RigNode->PinType.PinSubCategoryObject))
				{
					DataType = Struct->GetFName();
				}

				bAddedNode = RigBlueprint->ModelController->AddParameter(RigNode->GetPropertyName(), DataType, EControlRigModelParameterType::Hidden, NodePosition);
			}

			if(bAddedNode && RigBlueprint->LastNameFromNotification != NAME_None)
			{
				FName AddedNodeName = RigBlueprint->LastNameFromNotification;
				NameMap.Add(Node->GetName(), AddedNodeName.ToString());

				for (UEdGraphPin* Pin : Node->Pins)
				{
					FString Left, Right;
					RigBlueprint->Model->SplitPinPath(Pin->GetName(), Left, Right);
					Left = *AddedNodeName.ToString();

					if (Pin->PinType.ContainerType == EPinContainerType::Array)
					{
						RigBlueprint->ModelController->SetArrayPinSize(*Left, *Right, Pin->SubPins.Num());
					}

					if (RigNode->ExpandedPins.Contains(Pin->GetName()))
					{
						RigBlueprint->ModelController->ExpandPin(*Left, *Right, Pin->Direction == EGPD_Input, true);
					}

					if (Pin->Direction == EGPD_Input)
					{
						FString DefaultValue = Pin->DefaultValue;
						if (DefaultValue.IsEmpty() && Pin->DefaultObject != nullptr)
						{
							DefaultValue = Pin->DefaultObject->GetPathName();
						}
						if (!DefaultValue.IsEmpty())
						{
							RigBlueprint->ModelController->SetPinDefaultValue(*Left, *Right, DefaultValue, false);
						}
					}
				}
			}
		}
		else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
		{
			FVector2D NodePosition = FVector2D((float)CommentNode->NodePosX, (float)CommentNode->NodePosY);
			FVector2D NodeSize = FVector2D((float)CommentNode->NodeWidth, (float)CommentNode->NodeHeight);
			RigBlueprint->ModelController->AddComment(CommentNode->GetFName(), CommentNode->NodeComment, NodePosition, NodeSize, CommentNode->CommentColor, false);
		}
	}

	for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
	{
		UEdGraphNode* Node = *It;
		UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
		if (RigNode != nullptr)
		{
			for (UEdGraphPin* Pin : RigNode->Pins)
			{
				if (Pin->Direction == EGPD_Input)
				{
					continue;
				}

				for (UEdGraphPin* OtherPin : Pin->LinkedTo)
				{
					UControlRigGraphNode* OtherRigNode = Cast<UControlRigGraphNode>(OtherPin->GetOwningNode());
					if (OtherRigNode)
					{
						FString* RemappedNodeName = NameMap.Find(RigNode->GetName());
						FString* OtherRemappedNodeName = NameMap.Find(OtherRigNode->GetName());

						if (RemappedNodeName != nullptr && OtherRemappedNodeName != nullptr)
						{
							FString PinPath = Pin->GetName();
							if (PinPath.StartsWith(RigNode->GetPropertyName().ToString()))
							{
								PinPath = *RemappedNodeName + PinPath.RightChop(RigNode->GetPropertyName().ToString().Len());
							}
							FString OtherPinPath = OtherPin->GetName();
							if (OtherPinPath.StartsWith(OtherRigNode->GetPropertyName().ToString()))
							{
								OtherPinPath = *OtherRemappedNodeName + OtherPinPath.RightChop(OtherRigNode->GetPropertyName().ToString().Len());
							}

							FString SourceLeft, SourceRight, TargetLeft, TargetRight;
							RigBlueprint->Model->SplitPinPath(PinPath, SourceLeft, SourceRight);
							RigBlueprint->Model->SplitPinPath(OtherPinPath, TargetLeft, TargetRight);
							RigBlueprint->ModelController->MakeLink(*SourceLeft, *SourceRight, *TargetLeft, *TargetRight);
						}
					}
				}
			}
		}
	}

	RigBlueprint->ModelController->CloseUndoBracket();
}

void FControlRigEditor::OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit)
{
	TSharedPtr<SWidget> InlineContent = Toolkit->GetInlineContent();
	if (InlineContent.IsValid())
	{
		Toolbox->SetContent(InlineContent.ToSharedRef());
	}
}

void FControlRigEditor::OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit)
{
	Toolbox->SetContent(SNullWidget::NullWidget);
}

void FControlRigEditor::OnActiveTabChanged( TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated )
{
	if (!NewlyActivated.IsValid())
	{
		TArray<UObject*> ObjArray;
		Inspector->ShowDetailsForObjects(ObjArray);
	}
	else 
	{
		FBlueprintEditor::OnActiveTabChanged(PreviouslyActive, NewlyActivated);
	}
}

void FControlRigEditor::PostUndo(bool bSuccess)
{
	//DocumentManager->CleanInvalidTabs();
	//DocumentManager->RefreshAllTabs();

	FBlueprintEditor::PostUndo(bSuccess);
}

void FControlRigEditor::PostRedo(bool bSuccess)
{
	//DocumentManager->RefreshAllTabs();

	FBlueprintEditor::PostRedo(bSuccess);
}

void FControlRigEditor::UndoAction()
{
	GEditor->UndoTransaction();
}

void FControlRigEditor::RedoAction()
{
	GEditor->RedoTransaction();
}

void FControlRigEditor::CreateDefaultTabContents(const TArray<UBlueprint*>& InBlueprints)
{
	FBlueprintEditor::CreateDefaultTabContents(InBlueprints);
}

bool FControlRigEditor::IsSectionVisible(NodeSectionID::Type InSectionID) const
{
	switch (InSectionID)
	{
		case NodeSectionID::GRAPH:
		{
			return true;
		}
		default:
		{
			break;
		}
	}
	return false;
}

FGraphAppearanceInfo FControlRigEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);

	if (GetBlueprintObj()->IsA(UControlRigBlueprint::StaticClass()))
	{
		AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_ControlRig", "RIG");
	}

	return AppearanceInfo;
}

void FControlRigEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	FBlueprintEditor::NotifyPostChange(PropertyChangedEvent, PropertyThatChanged);
}

void FControlRigEditor::HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	switch (InType)
	{
		case EControlRigModelNotifType::NodeSelected:
		case EControlRigModelNotifType::NodeDeselected:
		{
			const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
			if (Node != nullptr)
			{
					if (FocusedGraphEdPtr.IsValid())
					{
						TSharedPtr<SGraphEditor> FocusedGraphEd = FocusedGraphEdPtr.Pin();
						if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(FocusedGraphEd->GetCurrentGraph()))
						{
						TArray<FControlRigModelNode> Selection;
						bool bSelect = InType == EControlRigModelNotifType::NodeSelected;
						if (bSelect)
						{
							Selection = InModel->SelectedNodes();
						}
						else
						{
							Selection.Add(*Node);
						}

						TArray<FRigElementKey> RigElementsToSelect;
						for (const FControlRigModelNode& SelectedNode : Selection)
						{
							if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeFromPropertyName(SelectedNode.Name)))
							{
								for (const FControlRigModelPin& Pin : Node->Pins)
								{
									if (Pin.Type.PinCategory == UEdGraphSchema_K2::PC_Name)
									{
										if (Pin.CustomWidgetName == TEXT("BoneName"))
										{
											RigElementsToSelect.Add(FRigElementKey(*Pin.DefaultValue, ERigElementType::Bone));
										}
										else if (Pin.CustomWidgetName == TEXT("ControlName"))
										{
											RigElementsToSelect.Add(FRigElementKey(*Pin.DefaultValue, ERigElementType::Control));
										}
										else if (Pin.CustomWidgetName == TEXT("SpaceName"))
										{
											RigElementsToSelect.Add(FRigElementKey(*Pin.DefaultValue, ERigElementType::Space));
										}
										else if (Pin.CustomWidgetName == TEXT("CurveName"))
										{
											RigElementsToSelect.Add(FRigElementKey(*Pin.DefaultValue, ERigElementType::Curve));
										}
									}
								}
							}
						}

						if (RigElementsToSelect.Num() > 0)
						{
							if (bSelect)
							{
								RigBlueprint->HierarchyContainer.ClearSelection();
							}
							for (const FRigElementKey& RigElementToSelect : RigElementsToSelect)
							{
								RigBlueprint->HierarchyContainer.Select(RigElementToSelect, bSelect);
							}
						}

						if (!bIsSelecting)
						{
							TGuardValue<bool> SelectingGuard(bIsSelecting, true);
							if (UEdGraphNode* EdNode = RigGraph->FindNodeFromPropertyName(Node->Name))
							{
								FocusedGraphEd->SetNodeSelection(EdNode, InType == EControlRigModelNotifType::NodeSelected);
							}
							break;
						}
					}
				}

				if (InType == EControlRigModelNotifType::NodeSelected)
				{
					UClass* Class = RigBlueprint->GeneratedClass.Get();
					if (Class)
					{
						if (FProperty* Property = Class->FindPropertyByName(Node->Name))
						{
							TSet<class UObject*> SelectedObjects;
							SelectedObjects.Add(Property->GetUPropertyWrapper());
							FBlueprintEditor::OnSelectedNodesChangedImpl(SelectedObjects);
						}
						else
						{
							TSharedPtr<SGraphEditor> FocusedGraphEd = FocusedGraphEdPtr.Pin();
							if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(FocusedGraphEd->GetCurrentGraph()))
							{
								if(UEdGraphNode* EdNode = RigGraph->FindNodeFromPropertyName(Node->Name))
								{
									TSet<class UObject*> SelectedObjects;
									SelectedObjects.Add(EdNode);
									FBlueprintEditor::OnSelectedNodesChangedImpl(SelectedObjects);
								}
							}
						}
					}
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

void FControlRigEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);
}

bool FControlRigEditor::IsEditable(UEdGraph* InGraph) const
{
	return IsGraphInCurrentBlueprint(InGraph);
}

bool FControlRigEditor::IsCompilingEnabled() const
{
	return true;
}

FText FControlRigEditor::GetGraphDecorationString(UEdGraph* InGraph) const
{
	return FText::GetEmpty();
}

TStatId FControlRigEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FControlRigEditor, STATGROUP_Tickables);
}

void FControlRigEditor::OnSelectedNodesChangedImpl(const TSet<class UObject*>& NewSelection)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (bIsSelecting)
	{
		return;
	}

	TGuardValue<bool> SelectingGuard(bIsSelecting, true);

	UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj());
	if (ControlRigBlueprint)
	{
		if (ControlRigBlueprint->ModelController)
		{
			TArray<FName> NodeNamesToSelect;
			for (UObject* Object : NewSelection)
			{
				if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(Object))
				{
					NodeNamesToSelect.Add(ControlRigGraphNode->GetPropertyName());
				}
				else if(UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
				{
					NodeNamesToSelect.Add(Node->GetFName());
				}
			}
			ControlRigBlueprint->ModelController->SetSelection(NodeNamesToSelect);
		}
	}
}

void FControlRigEditor::HandleHideItem()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj());

	TSet<UObject*> SelectedNodes = GetSelectedNodes();
	if(SelectedNodes.Num() > 0)
	{
		FScopedTransaction Transaction(LOCTEXT("HideRigItem", "Hide rig item"));

		ControlRigBlueprint->Modify();

		for(UObject* SelectedNodeObject : SelectedNodes)
		{
			if(UControlRigGraphNode* SelectedNode = Cast<UControlRigGraphNode>(SelectedNodeObject))
			{
				FBlueprintEditorUtils::RemoveNode(ControlRigBlueprint, SelectedNode, true);
			}
		}
	}
}

bool FControlRigEditor::CanHideItem() const
{
	return GetNumberOfSelectedNodes() > 0;
}

void FControlRigEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (!bControlRigEditorInitialized)
	{
		return;
	}

	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	if(InBlueprint == GetBlueprintObj())
	{
		if(bIsJustBeingCompiled)
		{
			UpdateControlRig();
		}

		OnSelectedNodesChangedImpl(GetSelectedNodes());

		UClass* Class = GetBlueprintObj()->GeneratedClass.Get();
		if (Class)
		{
			TSet<class UObject*> SelectedObjects;
			FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
			for (UObject* SelectedNode : SelectedNodes)
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(SelectedNode))
				{
					FProperty* Property = Class->FindPropertyByName(RigNode->GetPropertyName());
					if (Property)
					{
						SelectedObjects.Add(Property->GetUPropertyWrapper());
					}
				}
				else
				{
					SelectedObjects.Add(SelectedNode);
				}
			}
			if (SelectedObjects.Num() > 0)
			{
				FBlueprintEditor::OnSelectedNodesChangedImpl(SelectedObjects);
			}
		}
	}
}

void FControlRigEditor::HandleViewportCreated(const TSharedRef<class IPersonaViewport>& InViewport)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// TODO: this is duplicated code from FAnimBlueprintEditor, would be nice to consolidate. 
	auto GetCompilationStateText = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			switch (Blueprint->Status)
			{
			case BS_UpToDate:
			case BS_UpToDateWithWarnings:
				// Fall thru and return empty string
				break;
			case BS_Dirty:
				return LOCTEXT("ControlRigBP_Dirty", "Preview out of date");
			case BS_Error:
				return LOCTEXT("ControlRigBP_CompileError", "Compile Error");
			default:
				return LOCTEXT("ControlRigBP_UnknownStatus", "Unknown Status");
			}
		}

		return FText::GetEmpty();
	};

	auto GetCompilationStateVisibility = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			const bool bUpToDate = (Blueprint->Status == BS_UpToDate) || (Blueprint->Status == BS_UpToDateWithWarnings);
			return bUpToDate ? EVisibility::Collapsed : EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	};

	auto GetCompileButtonVisibility = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			return (Blueprint->Status == BS_Dirty) ? EVisibility::Visible : EVisibility::Collapsed;
		}

		return EVisibility::Collapsed;
	};

	auto CompileBlueprint = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			if (!Blueprint->IsUpToDate())
			{
				Compile();
			}
		}

		return FReply::Handled();
	};

	auto GetErrorSeverity = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			return (Blueprint->Status == BS_Error) ? EMessageSeverity::Error : EMessageSeverity::Warning;
		}

		return EMessageSeverity::Warning;
	};

	auto GetIcon = [this]()
	{
		if (UBlueprint* Blueprint = GetBlueprintObj())
		{
			return (Blueprint->Status == BS_Error) ? FEditorFontGlyphs::Exclamation_Triangle : FEditorFontGlyphs::Eye;
		}

		return FEditorFontGlyphs::Eye;
	};

	InViewport->AddNotification(MakeAttributeLambda(GetErrorSeverity),
		false,
		SNew(SHorizontalBox)
		.Visibility_Lambda(GetCompilationStateVisibility)
		+SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			.ToolTipText_Lambda(GetCompilationStateText)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "AnimViewport.MessageText")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
				.Text_Lambda(GetIcon)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text_Lambda(GetCompilationStateText)
				.TextStyle(FEditorStyle::Get(), "AnimViewport.MessageText")
			]
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.Visibility_Lambda(GetCompileButtonVisibility)
			.ToolTipText(LOCTEXT("ControlRigBPViewportCompileButtonToolTip", "Compile this Animation Blueprint to update the preview to reflect any recent changes."))
			.OnClicked_Lambda(CompileBlueprint)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "AnimViewport.MessageText")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FEditorFontGlyphs::Cog)
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "AnimViewport.MessageText")
					.Text(LOCTEXT("ControlRigBPViewportCompileButtonLabel", "Compile"))
				]
			]
		]
	);

	InViewport->AddToolbarExtender(TEXT("AnimViewportDefaultCamera"), FMenuExtensionDelegate::CreateLambda(
		[&](FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.AddMenuSeparator(TEXT("Control Rig"));
			InMenuBuilder.BeginSection("ControlRig", LOCTEXT("ControlRig_Label", "Control Rig"));
			{
				InMenuBuilder.AddWidget(
					SNew(SBox)
					.HAlign(HAlign_Right)
					[
						SNew(SBox)
						.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
						.WidthOverride(100.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
							.AllowSpin(true)
							.MinSliderValue(0.0f)
							.MaxSliderValue(100.0f)
							.Value(this, &FControlRigEditor::GetToolbarAxesScale)
							.OnValueChanged(this, &FControlRigEditor::OnToolbarAxesScaleChanged)
							.ToolTipText(LOCTEXT("ControlRigAxesScaleToolTip", "Scale of axes drawn for selected rig elements"))
						]
					], 
					LOCTEXT("ControlRigAxesScale", "Axes Scale")
				);
			}
			InMenuBuilder.EndSection();
		}
	));

	InViewport->GetKeyDownDelegate().BindLambda([&](const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) -> FReply {
		if (OnKeyDownDelegate.IsBound())
		{
			return OnKeyDownDelegate.Execute(MyGeometry, InKeyEvent);
		}
		return FReply::Unhandled();
	});
}

TOptional<float> FControlRigEditor::GetToolbarAxesScale() const
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		return EditMode->Settings->AxisScale;
	}
	return 0.f;
}

void FControlRigEditor::OnToolbarAxesScaleChanged(float InValue)
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		EditMode->Settings->AxisScale = InValue;
	}
}

void FControlRigEditor::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	InPersonaPreviewScene->SetActor(Actor);

	// Create the preview component
	UControlRigSkeletalMeshComponent* EditorSkelComp = NewObject<UControlRigSkeletalMeshComponent>(Actor);
	EditorSkelComp->SetSkeletalMesh(InPersonaPreviewScene->GetPersonaToolkit()->GetPreviewMesh());
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorSkelComp);
	bool bWasCreated = false;
	UAnimCustomInstance::BindToSkeletalMeshComponent<UControlRigSequencerAnimInstance>(EditorSkelComp, bWasCreated);
	InPersonaPreviewScene->AddComponent(EditorSkelComp, FTransform::Identity);

	// set root component, so we can attach to it. 
	Actor->SetRootComponent(EditorSkelComp);

	// set to use custom default mode defined in mesh component
	InPersonaPreviewScene->SetDefaultAnimationMode(EPreviewSceneDefaultAnimationMode::Custom);
}

void FControlRigEditor::UpdateControlRig()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(UClass* Class = GetBlueprintObj()->GeneratedClass)
	{
		UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
		UControlRigSequencerAnimInstance* AnimInstance = Cast<UControlRigSequencerAnimInstance>(EditorSkelComp->GetAnimInstance());

		if (AnimInstance)
		{
			if (ControlRig == nullptr)
			{
				ControlRig = NewObject<UControlRig>(EditorSkelComp, Class);
				// this is editing time rig
				ControlRig->ExecutionType = ERigExecutionType::Editing;
				ControlRig->ControlRigLog = &ControlRigLog;

				ControlRig->InitializeFromCDO();
				ControlRig->InstantiateOperatorsFromGeneratedClass();
 			}

			if (UControlRig* CDO = Cast<UControlRig>(Class->GetDefaultObject()))
			{
				CDO->GizmoLibrary = GetControlRigBlueprint()->GizmoLibrary;
 			}

			CacheNameLists();

			// When the control rig is re-instanced on compile, it loses its binding, so we refresh it here if needed
			if (!ControlRig->GetObjectBinding().IsValid())
			{
				ControlRig->SetObjectBinding(MakeShared<FControlRigSkeletalMeshBinding>());
			}
			
			// Make sure the object being debugged is the preview instance
			GetBlueprintObj()->SetObjectBeingDebugged(ControlRig);

			// initialize is moved post reinstance
			FInputBlendPose Filter;
			AnimInstance->UpdateControlRig(ControlRig, 0, false, false, Filter, 1.0f, true, bExecutionControlRig);
			AnimInstance->RecalcRequiredBones();

			// since rig has changed, rebuild draw skeleton
			EditorSkelComp->RebuildDebugDrawSkeleton();
			if (FControlRigEditMode* EditMode = GetEditMode())
			{
				EditMode->SetObjects(ControlRig, FGuid(), EditorSkelComp);
			}

			// update the graph with the compiler errors
			UpdateGraphCompilerErrors();
		}
	}
}

void FControlRigEditor::CacheNameLists()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint())
	{
		// make sure the bone name list is up 2 date for the editor graph
		for (UEdGraph* Graph : ControlRigBP->UbergraphPages)
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph == nullptr)
			{
				continue;
			}

			RigGraph->CacheNameLists(&ControlRigBP->HierarchyContainer);
		}
	}
}

void FControlRigEditor::AddReferencedObjects( FReferenceCollector& Collector )
{
	FBlueprintEditor::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(ControlRig);
}

void FControlRigEditor::HandlePreviewMeshChanged(USkeletalMesh* InOldSkeletalMesh, USkeletalMesh* InNewSkeletalMesh)
{
	RebindToSkeletalMeshComponent();
}

void FControlRigEditor::RebindToSkeletalMeshComponent()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UDebugSkelMeshComponent* MeshComponent = GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent();
	if (MeshComponent)
	{
		bool bWasCreated = false;
		UAnimCustomInstance::BindToSkeletalMeshComponent<UControlRigSequencerAnimInstance>(MeshComponent , bWasCreated);
	}
}

void FControlRigEditor::SetupGraphEditorEvents(UEdGraph* InGraph, SGraphEditor::FGraphEditorEvents& InEvents)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FBlueprintEditor::SetupGraphEditorEvents(InGraph, InEvents);

	InEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &FControlRigEditor::HandleCreateGraphActionMenu);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FControlRigEditor::OnNodeTitleCommitted);
}

FActionMenuContent FControlRigEditor::HandleCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	return FBlueprintEditor::OnCreateGraphActionMenu(InGraph, InNodePosition, InDraggedPins, bAutoExpand, InOnMenuClosed);
}

void FControlRigEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (UEdGraphNode_Comment* CommentBeingChanged = Cast<UEdGraphNode_Comment>(NodeBeingChanged))
	{
		if (UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint())
		{
			if (ControlRigBP->ModelController)
			{
				ControlRigBP->ModelController->SetCommentText(CommentBeingChanged->GetFName(), NewText.ToString(), true);
			}
		}
	}
}

FTransform FControlRigEditor::GetRigElementTransform(const FRigElementKey& InElement, bool bLocal) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	if (bLocal)
	{
		return GetControlRigBlueprint()->HierarchyContainer.GetLocalTransform(InElement);
	}
	return GetControlRigBlueprint()->HierarchyContainer.GetGlobalTransform(InElement);
}

void FControlRigEditor::SetRigElementTransform(const FRigElementKey& InElement, const FTransform& InTransform, bool bLocal)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FScopedTransaction Transaction(LOCTEXT("Move Bone", "Move Bone transform"));
	UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint();
	ControlRigBP->Modify();

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			FTransform Transform = InTransform;
			if (bLocal)
			{
				FTransform ParentTransform = FTransform::Identity;
				FRigElementKey ParentKey = ControlRigBP->HierarchyContainer.BoneHierarchy[InElement.Name].GetParentElementKey();
				if (ParentKey.IsValid())
				{
					ParentTransform = GetRigElementTransform(ParentKey, false);
				}
				Transform = Transform * ParentTransform;
				Transform.NormalizeRotation();
			}

			ControlRigBP->HierarchyContainer.BoneHierarchy.SetInitialTransform(InElement.Name, Transform);
			ControlRigBP->HierarchyContainer.BoneHierarchy.SetGlobalTransform(InElement.Name, Transform);
			OnHierarchyChanged();
			break;
		}
		case ERigElementType::Control:
		{
			FTransform LocalTransform = InTransform;
			FTransform GlobalTransform = InTransform;
			if (!bLocal)
			{
				ControlRigBP->HierarchyContainer.ControlHierarchy.SetGlobalTransform(InElement.Name, InTransform);
				LocalTransform = ControlRigBP->HierarchyContainer.ControlHierarchy.GetLocalTransform(InElement.Name);
			}
			else
			{
				ControlRigBP->HierarchyContainer.ControlHierarchy.SetLocalTransform(InElement.Name, InTransform);
				GlobalTransform = ControlRigBP->HierarchyContainer.ControlHierarchy.GetGlobalTransform(InElement.Name);
			}

			ControlRigBP->HierarchyContainer.ControlHierarchy.SetInitialValue<FTransform>(InElement.Name, LocalTransform);
			ControlRigBP->HierarchyContainer.ControlHierarchy.SetGlobalTransform(InElement.Name, GlobalTransform);
			OnHierarchyChanged();
			break;
		}
		case ERigElementType::Space:
		{
			FTransform LocalTransform = InTransform;
			FTransform GlobalTransform = InTransform;
			if (!bLocal)
			{
				ControlRigBP->HierarchyContainer.SpaceHierarchy.SetGlobalTransform(InElement.Name, InTransform);
				LocalTransform = ControlRigBP->HierarchyContainer.SpaceHierarchy.GetLocalTransform(InElement.Name);
			}
			else
			{
				ControlRigBP->HierarchyContainer.SpaceHierarchy.SetLocalTransform(InElement.Name, InTransform);
				GlobalTransform = ControlRigBP->HierarchyContainer.SpaceHierarchy.GetGlobalTransform(InElement.Name);
			}

			ControlRigBP->HierarchyContainer.SpaceHierarchy.SetInitialTransform(InElement.Name, LocalTransform);
			ControlRigBP->HierarchyContainer.SpaceHierarchy.SetGlobalTransform(InElement.Name, GlobalTransform);
			OnHierarchyChanged();
			break;
		}
		default:
		{
			ensureMsgf(false, TEXT("Unsupported RigElement Type : %d"), InElement.Type);
			break;
		}
	}
	
	UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		EditorSkelComp->RebuildDebugDrawSkeleton();
	}
}

void FControlRigEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint();
	if (ControlRig && ControlRigBP && RigElementInDetailPanel)
	{
		UControlRig* DebuggedControlRig = Cast<UControlRig>(GetBlueprintObj()->GetObjectBeingDebugged());

		if (DebuggedControlRig && DebuggedControlRig->GetHierarchy()->GetIndex(RigElementInDetailPanel) == INDEX_NONE)
		{
			return;
		}

		UScriptStruct* ScriptStruct = PropertyChangedEvent.Property->GetOwner<UScriptStruct>();
		if (ScriptStruct)
		{
			if (ScriptStruct == FRigBone::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Bone)
			{
				ControlRig->Hierarchy.BoneHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.BoneHierarchy[RigElementInDetailPanel.Name];
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
				{
					DebuggedControlRig->Hierarchy.BoneHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.BoneHierarchy[RigElementInDetailPanel.Name];
				}
			}
			else if (ScriptStruct == FRigSpace::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Space)
	{
				ControlRig->Hierarchy.SpaceHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.SpaceHierarchy[RigElementInDetailPanel.Name];
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
				{
					DebuggedControlRig->Hierarchy.SpaceHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.SpaceHierarchy[RigElementInDetailPanel.Name];
				}
			}
			else if (ScriptStruct == FRigControl::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Control)
			{
				ControlRig->Hierarchy.ControlHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.ControlHierarchy[RigElementInDetailPanel.Name];
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
		{
					DebuggedControlRig->Hierarchy.ControlHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.ControlHierarchy[RigElementInDetailPanel.Name];
				}
			}
			else if (ScriptStruct == FRigCurve::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Curve)
			{
				ControlRig->Hierarchy.CurveContainer[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.CurveContainer[RigElementInDetailPanel.Name];
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
				{
					DebuggedControlRig->Hierarchy.CurveContainer[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.CurveContainer[RigElementInDetailPanel.Name];
				}
			}

			ControlRigBP->MarkPackageDirty();
		}

		if (PropertyChangedEvent.Property->GetName().StartsWith(TEXT("Gizmo")) && RigElementInDetailPanel.Type == ERigElementType::Control)
		{
			ControlRig->Hierarchy.ControlHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.ControlHierarchy[RigElementInDetailPanel.Name];
			if (DebuggedControlRig && DebuggedControlRig != ControlRig)
			{
				DebuggedControlRig->Hierarchy.ControlHierarchy[RigElementInDetailPanel.Name] = ControlRigBP->HierarchyContainer.ControlHierarchy[RigElementInDetailPanel.Name];
			}
			ControlRigBP->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.Broadcast(&ControlRigBP->HierarchyContainer, FRigElementKey(RigElementInDetailPanel.Name, ERigElementType::Control));
			ControlRigBP->MarkPackageDirty();
		}
	}
}

void FControlRigEditor::OnHierarchyChanged()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	CacheNameLists();
	if (UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint())
	{
		ControlRigBP->PropagateHierarchyFromBPToInstances();

		TArray<FRigElementKey> SelectedElements = ControlRigBP->HierarchyContainer.CurrentSelection();
		for(const FRigElementKey& SelectedElement : SelectedElements)
		{
			ControlRigBP->HierarchyContainer.OnElementSelected.Broadcast(&ControlRigBP->HierarchyContainer, SelectedElement, true);
		}

		if (ControlRigBP->HierarchyContainer.GetIndex(RigElementInDetailPanel) == INDEX_NONE)
		{
			ClearDetailObject();
		}
	}
	else
	{
		ClearDetailObject();
	}
}

void FControlRigEditor::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	OnHierarchyChanged();
}

void FControlRigEditor::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	OnHierarchyChanged();
}

void FControlRigEditor::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = GetControlRigBlueprint();
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}

		CacheNameLists();

		for (UEdGraphNode* Node : RigGraph->Nodes)
		{
			UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
			if (RigNode == nullptr)
			{
				continue;
			}

			FStructProperty* UnitProperty = RigNode->GetUnitProperty();
			UStruct* UnitStruct = RigNode->GetUnitScriptStruct();
			if (UnitProperty && UnitStruct)
			{
				for (TFieldIterator<FNameProperty> It(UnitStruct); It; ++It)
				{
					if (It->HasMetaData(UControlRig::BoneNameMetaName) && ElementType == ERigElementType::Bone)
					{
						FString PinName = FString::Printf(TEXT("%s.%s"), *UnitProperty->GetName(), *It->GetName());
						UEdGraphPin* Pin = Node->FindPin(PinName, EEdGraphPinDirection::EGPD_Input);
						if (Pin)
						{
							FName CurrentBone = FName(*Pin->GetDefaultAsString());
							if (CurrentBone == InOldName)
							{
								Pin->GetSchema()->TrySetDefaultValue(*Pin, InNewName.ToString());
							}
						}
					}
					if (It->HasMetaData(UControlRig::ControlNameMetaName) && ElementType == ERigElementType::Control)
					{
						FString PinName = FString::Printf(TEXT("%s.%s"), *UnitProperty->GetName(), *It->GetName());
						UEdGraphPin* Pin = Node->FindPin(PinName, EEdGraphPinDirection::EGPD_Input);
						if (Pin)
						{
							FName CurrentControl = FName(*Pin->GetDefaultAsString());
							if (CurrentControl == InOldName)
							{
								Pin->GetSchema()->TrySetDefaultValue(*Pin, InNewName.ToString());
							}
						}
					}
					if (It->HasMetaData(UControlRig::SpaceNameMetaName) && ElementType == ERigElementType::Space)
					{
						FString PinName = FString::Printf(TEXT("%s.%s"), *UnitProperty->GetName(), *It->GetName());
						UEdGraphPin* Pin = Node->FindPin(PinName, EEdGraphPinDirection::EGPD_Input);
						if (Pin)
						{
							FName CurrentSpace = FName(*Pin->GetDefaultAsString());
							if (CurrentSpace == InOldName)
							{
								Pin->GetSchema()->TrySetDefaultValue(*Pin, InNewName.ToString());
							}
						}
					}
					if (It->HasMetaData(UControlRig::CurveNameMetaName) && ElementType == ERigElementType::Curve)
					{
						FString PinName = FString::Printf(TEXT("%s.%s"), *UnitProperty->GetName(), *It->GetName());
						UEdGraphPin* Pin = Node->FindPin(PinName, EEdGraphPinDirection::EGPD_Input);
						if (Pin)
						{
							FName CurrentCurve = FName(*Pin->GetDefaultAsString());
							if (CurrentCurve == InOldName)
							{
								Pin->GetSchema()->TrySetDefaultValue(*Pin, InNewName.ToString());
							}
						}
					}
				}
			}
		}

		CacheNameLists();
	}
}

void FControlRigEditor::OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	OnHierarchyChanged();
}

void FControlRigEditor::OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected)
{
	UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint();
	if (RigBlueprint == nullptr)
	{
		return;
	}

	switch (InKey.Type)
	{
		case ERigElementType::Bone:
		{
			FRigBoneHierarchy& BoneHierarchy = RigBlueprint->HierarchyContainer.BoneHierarchy;
			if (bSelected)
							{
				SetDetailStruct(InKey, MakeShareable(new FStructOnScope(FRigBone::StaticStruct(), (uint8*)&(BoneHierarchy[InKey.Name]))));
							}
			else
			{
				ClearDetailObject();
						}

			UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
			if (EditorSkelComp)
			{
				int32 Index = ControlRig->Hierarchy.BoneHierarchy.GetIndex(InKey.Name);
				if (Index != INDEX_NONE)
				{
					if (bSelected)
					{
						EditorSkelComp->BonesOfInterest.Add(Index);
					}
					else
					{
						EditorSkelComp->BonesOfInterest.Remove(Index);
				}
			}
		}

			break;
		}
		case ERigElementType::Space:
		{
			if (bSelected)
			{
				FRigSpaceHierarchy& SpaceHierarchy = RigBlueprint->HierarchyContainer.SpaceHierarchy;
				SetDetailStruct(InKey, MakeShareable(new FStructOnScope(FRigSpace::StaticStruct(), (uint8*)&(SpaceHierarchy[InKey.Name]))));
			}
			else
			{
				ClearDetailObject();
			}
			break;
		}
		case ERigElementType::Control:
		{
			if (bSelected)
			{
				FRigControlHierarchy& ControlHierarchy = RigBlueprint->HierarchyContainer.ControlHierarchy;
				SetDetailStruct(InKey, MakeShareable(new FStructOnScope(FRigControl::StaticStruct(), (uint8*)&(ControlHierarchy[InKey.Name]))));
			}
			else
			{
				ClearDetailObject();
			}
			break;
		}
		case ERigElementType::Curve:
		{
			if (bSelected)
			{
				FRigCurveContainer& CurveContainer = RigBlueprint->HierarchyContainer.CurveContainer;
				SetDetailStruct(InKey, MakeShareable(new FStructOnScope(FRigCurve::StaticStruct(), (uint8*)&(CurveContainer[InKey.Name]))));
			}
			else
			{
				ClearDetailObject();
			}
			break;
		}
	}
}

void FControlRigEditor::OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	OnHierarchyChanged();
}

FControlRigEditorEditMode* FControlRigEditor::GetEditMode() const
{
	if (GetAssetEditorModeManager() == nullptr)
	{
		return nullptr;
	}
	return static_cast<FControlRigEditorEditMode*>(GetAssetEditorModeManager()->GetActiveMode(FControlRigEditorEditMode::ModeName));
}


void FControlRigEditor::OnCurveContainerChanged()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	ClearDetailObject();

	FBlueprintEditorUtils::MarkBlueprintAsModified(GetControlRigBlueprint());

	UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		// restart animation 
		EditorSkelComp->InitAnim(true);
		UpdateControlRig();
	}
	CacheNameLists();

	// notification
	FNotificationInfo Info(LOCTEXT("CurveContainerChangeHelpMessage", "CurveContainer has been successfully modified."));
	Info.bFireAndForget = true;
	Info.FadeOutDuration = 10.0f;
	Info.ExpireDuration = 0.0f;

	TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
}

void FControlRigEditor::OnGraphNodeDropToPerform(TSharedPtr<FGraphNodeDragDropOp> DragDropOp, UEdGraph* Graph, const FVector2D& NodePosition, const FVector2D& ScreenPosition)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (DragDropOp->IsOfType<FRigElementHierarchyDragDropOp>())
	{
		TSharedPtr<FRigElementHierarchyDragDropOp> RigHierarchyOp = StaticCastSharedPtr<FRigElementHierarchyDragDropOp>(DragDropOp);

		TArray<FRigElementKey> DraggedKeys = RigHierarchyOp->GetElements();
		uint8 DraggedTypes = 0;
		for (const FRigElementKey& DraggedKey : DraggedKeys)
		{
			DraggedTypes = DraggedTypes | (uint8)DraggedKey.Type;
		}

		if (DraggedTypes != 0 && FocusedGraphEdPtr.IsValid())
		{
			FMenuBuilder MenuBuilder(true, NULL);
			const FText SectionText = FText::FromString(RigHierarchyOp->GetJoinedElementNames());

			MenuBuilder.BeginSection("RigHierarchyDroppedOn", SectionText);

			if ((DraggedTypes & (uint8)ERigElementType::Bone) != 0)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetBoneTransform", "Get Bone Transform"),
					LOCTEXT("CreateGetBoneTransformTooltip", "Getter for bone transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetBoneTransform", "Set Bone Transform"),
					LOCTEXT("CreateSetBoneTransformTooltip", "Setter for bone transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateSetBoneRotation", "Set Bone Rotation"),
				LOCTEXT("CreateSetBoneRotationTooltip", "Setter for bone Rotation\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Rotation, false, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateSetBoneTranslation", "Set Bone Translation"),
				LOCTEXT("CreateSetBoneTranslationTooltip", "Setter for bone translation\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Translation, false, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateSetBoneOffset", "Set Bone Offset"),
				LOCTEXT("CreateSetBoneOffsetTooltip", "Setter for bone offset\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Offset, false, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateGetInitialBoneTransform", "Get Initial Bone Transform"),
				LOCTEXT("CreateGetInitialBoneTransformTooltip", "Getter for initial bone transform\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Initial, true, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateGetBoneRelativeTransform", "Get Bone Relative Transform"),
				LOCTEXT("CreateGetBoneRelativeTransformTooltip", "Getter for bone relative transform\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Relative, true, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateSetBoneRelativeTransform", "Set Bone Relative Transform"),
				LOCTEXT("CreateSetBoneRelativeTransformTooltip", "Setter for bone relative transform\n"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Relative, false, DraggedKeys, Graph, NodePosition),
					FCanExecuteAction()
				)
			);

			}

			if ((DraggedTypes & (uint8)ERigElementType::Control) != 0)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetControlTransform", "Get Control Transform"),
					LOCTEXT("CreateGetControlTransformTooltip", "Getter for control transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
			}

			if ((DraggedTypes & (uint8)ERigElementType::Space) != 0)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetSpaceTransform", "Get Space Transform"),
					LOCTEXT("CreateGetSpaceTransformTooltip", "Getter for space transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
			}

			TSharedRef<SWidget> GraphEditorPanel = FocusedGraphEdPtr.Pin().ToSharedRef();

			// Show dialog to choose getter vs setter
			FSlateApplication::Get().PushMenu(
				GraphEditorPanel,
				FWidgetPath(),
				MenuBuilder.MakeWidget(),
				ScreenPosition,
				FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
			);

			MenuBuilder.EndSection();
		}
	}
}

void FControlRigEditor::HandleMakeElementGetterSetter(ERigElementGetterSetterType Type, bool bIsGetter, TArray<FRigElementKey> Keys, UEdGraph* Graph, FVector2D NodePosition)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Keys.Num() == 0)
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (Blueprint == nullptr)
	{
		return;
	}
	if (Blueprint->ModelController == nullptr)
	{
		return;
	}

	Blueprint->ModelController->OpenUndoBracket(TEXT("Adding Nodes from Hierarchy"));

	TArray<FName> NewNodeNames;

	for (const FRigElementKey& Key : Keys)
	{
	UStruct* StructTemplate = nullptr;

		FName PinName;
		switch (Key.Type)
		{
			case ERigElementType::Bone:
			{
				PinName = TEXT("Bone");

	if (bIsGetter)
	{
		switch (Type)
		{
						case ERigElementGetterSetterType_Transform:
			{
				StructTemplate = FRigUnit_GetBoneTransform::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Initial:
			{
				StructTemplate = FRigUnit_GetInitialBoneTransform::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Relative:
			{
				StructTemplate = FRigUnit_GetRelativeBoneTransform::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Name:
			{
				StructTemplate = FRigUnit_BoneName::StaticStruct();
				break;
			}
			default:
			{
				break;
			}
		}
	}
	else
	{
		switch (Type)
		{
						case ERigElementGetterSetterType_Transform:
			{
				StructTemplate = FRigUnit_SetBoneTransform::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Relative:
			{
				StructTemplate = FRigUnit_SetRelativeBoneTransform::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Rotation:
			{
				StructTemplate = FRigUnit_SetBoneRotation::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Translation:
			{
				StructTemplate = FRigUnit_SetBoneTranslation::StaticStruct();
				break;
			}
						case ERigElementGetterSetterType_Offset:
			{
				StructTemplate = FRigUnit_AddBoneTransform::StaticStruct();
				break;
			}
			default:
			{
				break;
			}
		}
	}
				break;
			}
			case ERigElementType::Control:
			{
				PinName = TEXT("Control");
				if (bIsGetter)
	{
					StructTemplate = FRigUnit_GetControlTransform::StaticStruct();
				}
				break;
	}
			case ERigElementType::Space:
			{
				PinName = TEXT("Space");
				if (bIsGetter)
	{
					StructTemplate = FRigUnit_GetSpaceTransform::StaticStruct();
				}
				break;
	}
			default:
	{
				break;
			}
	}

		if (StructTemplate == nullptr)
		{
			continue;
		}

		FVector2D NodePositionIncrement(0.f, 120.f);
		if (!bIsGetter)
		{
			NodePositionIncrement = FVector2D(380.f, 0.f);
		}

		FName Name = FControlRigBlueprintUtils::ValidateName(Blueprint, StructTemplate->GetName());
		if (Blueprint->ModelController->AddNode(StructTemplate->GetFName(), NodePosition, Name))
		{
			const FControlRigModelNode* Node = Blueprint->Model->FindNode(Blueprint->LastNameFromNotification);
			if (Node)
			{
				NewNodeNames.Add(Node->Name);
				Blueprint->ModelController->SetPinDefaultValueName(Node->Name, PinName, Key.Name, true);
			}
		}

		NodePosition += NodePositionIncrement;
	}

	if (NewNodeNames.Num() > 0)
	{
		Blueprint->ModelController->CloseUndoBracket();
		Blueprint->ModelController->SetSelection(NewNodeNames);
	}
	else
	{
		Blueprint->ModelController->CancelUndoBracket();
	}
}

void FControlRigEditor::UpdateGraphCompilerErrors()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (Blueprint)
	{
		if (Blueprint->Status == BS_Error)
		{
			return;
		}

		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph == nullptr)
			{
				continue;
			}

			// reset all nodes and store them in the map
			bool bFoundWarning = false;
			bool bFoundError = false;
			TMap<FName, UControlRigGraphNode*> UnitNameToNode;
			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
				{
					bFoundError = bFoundError || ControlRigGraphNode->ErrorType <= (int32)EMessageSeverity::Error;
					bFoundWarning = bFoundWarning || ControlRigGraphNode->ErrorType <= (int32)EMessageSeverity::Warning;

					if (ControlRigGraphNode->GetUnitProperty())
					{
						UnitNameToNode.Add(ControlRigGraphNode->GetUnitProperty()->GetFName(), ControlRigGraphNode);
					}
				}
			}

			// update the nodes' error messages
			bool bFoundErrorOrWarningInLog = false;
			for (const FControlRigLog::FLogEntry& Entry : ControlRigLog.Entries)
			{
				UControlRigGraphNode** RigNodePtr = UnitNameToNode.Find(Entry.Unit);
				if (RigNodePtr == nullptr)
				{
					continue;
				}
				UControlRigGraphNode* RigNode = *RigNodePtr;

				bFoundError = bFoundError || Entry.Severity <= EMessageSeverity::Error;
				bFoundWarning = bFoundWarning || Entry.Severity <= EMessageSeverity::Warning;
				bFoundErrorOrWarningInLog = bFoundErrorOrWarningInLog || Entry.Severity <= EMessageSeverity::Warning;

				int32 ErrorType = (int32)Entry.Severity;
				if (RigNode->ErrorType < ErrorType)
				{
					continue;
				}
				else if (RigNode->ErrorType == ErrorType)
				{
					RigNode->ErrorMsg = FString::Printf(TEXT("%s\n%s"), *RigNode->ErrorMsg, *Entry.Message);
				}
				else
				{
					RigNode->ErrorMsg = Entry.Message;
					RigNode->ErrorType = ErrorType;
				}
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
				{
					bool PreviouslyHadError = ControlRigGraphNode->bHasCompilerMessage;
					bool CurrentlyHasError = ControlRigGraphNode->ErrorType <= int32(EMessageSeverity::Info);
					if (CurrentlyHasError != PreviouslyHadError)
					{
						ControlRigGraphNode->bHasCompilerMessage = CurrentlyHasError;
						ControlRigGraphNode->Modify();
					}
				}
			}

			if (bFoundError)
			{
				Blueprint->Status = BS_Error;
				Blueprint->MarkPackageDirty();

				if (bFoundErrorOrWarningInLog)
				{
					FNotificationInfo Info(LOCTEXT("ControlRigBlueprintCompilerUnitErrorMessage", "There has been a compiler error.\nCheck the Execution Stack view."));
					Info.bUseSuccessFailIcons = true;
					Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Error"));
					Info.bFireAndForget = true;
					Info.FadeOutDuration = 10.0f;
					Info.ExpireDuration = 0.0f;
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
					NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
				}
			}
			else if (bFoundWarning)
			{
				if (bFoundErrorOrWarningInLog)
				{
					FNotificationInfo Info(LOCTEXT("ControlRigBlueprintCompilerUnitWarningMessage", "There has been a compiler warning.\nCheck the Execution Stack view."));
					Info.bUseSuccessFailIcons = true;
					Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
					Info.bFireAndForget = true;
					Info.FadeOutDuration = 10.0f;
					Info.ExpireDuration = 0.0f;
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
					NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
				}
			}
		}

		//Stack
	}

}

void FControlRigEditor::DumpUnitTestCode()
{
	/*
	if (UEdGraph* Graph = GetFocusedGraph())
	{
		TArray<FString> Code;

		// dump the hierarchy
		if (ControlRig)
		{
			const FRigBoneHierarchy& Hierarchy = ControlRig->GetBoneHierarchy();
			if (Hierarchy.Bones.Num() > 0)
			{
				Code.Add(TEXT("FRigBoneHierarchy& Hierarchy = Rig->GetBoneHierarchy();"));
			}
			for (const FRigBone& Bone : Hierarchy.Bones)
			{
				FString ParentName = Bone.ParentName.IsNone() ? TEXT("NAME_None") : FString::Printf(TEXT("TEXT(\"%s\")"), *Bone.ParentName.ToString());
				FTransform T = Bone.InitialTransform;
				FString QuaternionString = FString::Printf(TEXT("FQuat(%.03f, %.03f, %.03f, %.03f)"), T.GetRotation().X, T.GetRotation().Y, T.GetRotation().Z, T.GetRotation().W);
				FString TranslationString = FString::Printf(TEXT("FVector(%.03f, %.03f, %.03f)"), T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z);
				FString ScaleString = FString::Printf(TEXT("FVector(%.03f, %.03f, %.03f)"), T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z);
				FString TransformString = FString::Printf(TEXT("FTransform(%s, %s, %s)"), *QuaternionString, *TranslationString, *ScaleString);
				Code.Add(FString::Printf(TEXT("Hierarchy.AddBone(TEXT(\"%s\"), %s, %s);"), *Bone.Name.ToString(), *ParentName, *TransformString));
			}
		}

		// dump the nodes
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
			{
				FStructProperty* Property = RigNode->GetUnitProperty();
				if (Property == nullptr)
				{
					return;
				}
				
				Code.Add(FString::Printf(TEXT("FString %s = Rig->AddUnit(TEXT(\"%s\"));"), *Property->GetName(), *Property->Struct->GetName()));
			}
		}

		// dump the pin links
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
			{
				for (UEdGraphPin* Pin : RigNode->Pins)
				{
					if (Pin->Direction != EEdGraphPinDirection::EGPD_Output)
					{
						continue;
					}

					for (UEdGraphPin* Linkedpin : Pin->LinkedTo)
					{
						if (UControlRigGraphNode* LinkedRigNode = Cast<UControlRigGraphNode>(Linkedpin->GetOwningNode()))
						{
							FString PropertyPathA = Pin->GetName();
							FString PropertyPathB = Linkedpin->GetName();
							FString NodeNameA, PinNameA, NodeNameB, PinNameB;
							PropertyPathA.Split(TEXT("."), &NodeNameA, &PinNameA);
							PropertyPathB.Split(TEXT("."), &NodeNameB, &PinNameB);

							Code.Add(FString::Printf(TEXT("Rig->LinkProperties(%s + TEXT(\".%s\"), %s + TEXT(\".%s\"));"), *NodeNameA, *PinNameA, *NodeNameB, *PinNameB));
						}
					}
				}
			}
		}

		// set the pin values
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
			{
				for (UEdGraphPin* Pin : RigNode->Pins)
				{
					if (Pin->Direction != EEdGraphPinDirection::EGPD_Input)
					{
						continue;
					}

					if (Pin->ParentPin != nullptr)
					{
						continue;
					}

					if (Pin->LinkedTo.Num() > 0)
					{
						continue;
					}

					if (!Pin->DefaultValue.IsEmpty())
					{
						FString PropertyPath = Pin->GetName();
						FString NodeName, PinName;
						PropertyPath.Split(TEXT("."), &NodeName, &PinName);
						Code.Add(FString::Printf(TEXT("Rig->SetPinDefault(%s + TEXT(\".%s\"), TEXT(\"%s\"));"), *NodeName, *PinName, *Pin->DefaultValue));
					}
				}
			}
		}
		Code.Add(TEXT("Rig->Compile();"));

		UE_LOG(LogControlRigEditor, Display, TEXT("\n%s\n"), *FString::Join(Code, TEXT("\n")));
	}
	*/
}

void FControlRigEditor::HandleOnViewportContextMenuDelegate(class FMenuBuilder& MenuBuilder)
{
	if (OnViewportContextMenuDelegate.IsBound())
	{
		OnViewportContextMenuDelegate.Execute(MenuBuilder);
	}
}

TSharedPtr<FUICommandList> FControlRigEditor::HandleOnViewportContextMenuCommandsDelegate()
{
	if (OnViewportContextMenuCommandsDelegate.IsBound())
	{
		return OnViewportContextMenuCommandsDelegate.Execute();
	}
	return TSharedPtr<FUICommandList>();
}

#undef LOCTEXT_NAMESPACE
