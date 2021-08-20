// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditor.h"
#include "Modules/ModuleManager.h"
#include "ControlRigEditorModule.h"
#include "ControlRigBlueprint.h"
#include "SBlueprintEditorToolbar.h"
#include "ControlRigEditorMode.h"
#include "SKismetInspector.h"
#include "SEnumCombobox.h"
#include "Framework/Commands/GenericCommands.h"
#include "Editor.h"
#include "Graph/ControlRigGraph.h"
#include "BlueprintActionDatabase.h"
#include "ControlRigBlueprintCommands.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "IPersonaToolkit.h"
#include "PersonaModule.h"
#include "ControlRigEditorEditMode.h"
#include "ControlRigEditModeSettings.h"
#include "AssetEditorModeManager.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "AnimCustomInstanceHelper.h"
#include "Sequencer/ControlRigLayerInstance.h"
#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "IPersonaPreviewScene.h"
#include "Persona/Private/AnimationEditorPreviewScene.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "ControlRig.h"
#include "ControlRigSkeletalMeshComponent.h"
#include "ControlRigObjectBinding.h"
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
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetTransform.h"
#include "ControlRig/Public/Units/Hierarchy/RigUnit_SetTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetRelativeTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetRelativeTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_OffsetTransform.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "ControlRig/Private/Units/Execution/RigUnit_PrepareForExecution.h"
#include "ControlRig/Private/Units/Execution/RigUnit_InverseExecution.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_GetControlTransform.h"
#include "ControlRig/Private/Units/Hierarchy/RigUnit_SetControlTransform.h"
#include "ControlRig/Private/Units/Execution/RigUnit_Collection.h"
#include "Units/Hierarchy/RigUnit_AddBoneTransform.h"
#include "Graph/NodeSpawners/ControlRigUnitNodeSpawner.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRigObjectVersion.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_Comment.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SNodePanel.h"
#include "Kismet/Private/SMyBlueprint.h"
#include "Kismet/Private/SBlueprintEditorSelectedDebugObjectWidget.h"
#include "Exporters/Exporter.h"
#include "UnrealExporter.h"
#include "ControlRigElementDetails.h"
#include "PropertyEditorModule.h"
#include "Settings/ControlRigSettings.h"
#include "Widgets/Docking/SDockTab.h"
#include "BlueprintCompilationManager.h"

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
	, bIsSettingObjectBeingDebugged(false)
	, NodeDetailStruct(nullptr)
	, NodeDetailName(NAME_None)
	, bExecutionControlRig(true)
	, bSetupModeEnabled(false)
	, bFirstTimeSelecting(true)
	, bAnyErrorsLeft(false)
	, LastEventQueue(EControlRigEditorEventQueue::Setup)
	, LastDebuggedRig()
{
}

FControlRigEditor::~FControlRigEditor()
{
	UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint();
	if (RigBlueprint)
	{
		UControlRigBlueprint::sCurrentlyOpenedRigBlueprints.Remove(RigBlueprint);

		RigBlueprint->HierarchyContainer.OnElementChanged.RemoveAll(this);
		RigBlueprint->HierarchyContainer.OnElementAdded.RemoveAll(this);
		RigBlueprint->HierarchyContainer.OnElementRemoved.RemoveAll(this);
		RigBlueprint->HierarchyContainer.OnElementRenamed.RemoveAll(this);
		RigBlueprint->HierarchyContainer.OnElementReparented.RemoveAll(this);
		RigBlueprint->HierarchyContainer.OnElementSelected.RemoveAll(this);
		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			RigBlueprint->HierarchyContainer.OnElementSelected.RemoveAll(EditMode);
		}
		RigBlueprint->OnRefreshEditor().RemoveAll(this);
		RigBlueprint->OnVariableDropped().RemoveAll(this);
	}

	if (NodeDetailBuffer.Num() > 0 && NodeDetailStruct != nullptr)
	{
		NodeDetailStruct->DestroyStruct(NodeDetailBuffer.GetData(), 1);
	}

	if (UWorld* PreviewWorld = GetPersonaToolkit()->GetPreviewScene()->GetWorld())
	{
		PreviewWorld->MarkObjectsPendingKill();
		PreviewWorld->MarkPendingKill();
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

	check(InControlRigBlueprint);

	FBlueprintCompilationManager::FlushCompilationQueue(nullptr);

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
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	InitAssetEditor(Mode, InitToolkitHost, ControlRigEditorAppName, FTabManager::FLayout::NullLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsBeingEdited);

	CreateDefaultCommands();

	UControlRigBlueprint::sCurrentlyOpenedRigBlueprints.AddUnique(InControlRigBlueprint);

	TArray<UBlueprint*> ControlRigBlueprints;
	ControlRigBlueprints.Add(InControlRigBlueprint);

	InControlRigBlueprint->InitializeModelIfRequired();

	CommonInitialization(ControlRigBlueprints, false);

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
		}

	}

	InControlRigBlueprint->OnModified().AddSP(this, &FControlRigEditor::HandleModifiedEvent);
	InControlRigBlueprint->OnVMCompiled().AddSP(this, &FControlRigEditor::HandleVMCompiledEvent);

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
		EditMode->OnAnimSystemInitialized().Add(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FControlRigEditor::OnAnimInitialized));
		
		PersonaToolkit->GetPreviewScene()->SetRemoveAttachedComponentFilter(FOnRemoveAttachedComponentFilter::CreateSP(EditMode, &FControlRigEditMode::CanRemoveFromPreviewScene));
	}

	UpdateControlRig();

	// Post-layout initialization
	PostLayoutBlueprintEditorInitialization();

	if (ControlRigBlueprints.Num() > 0)
	{
		bool bBroughtGraphToFront = false;
		for(UEdGraph* Graph : ControlRigBlueprints[0]->UbergraphPages)
		{
			if (Graph->GetFName().IsEqual(UControlRigGraphSchema::GraphName_ControlRig))
			{
				if (!bBroughtGraphToFront)
				{
					OpenGraphAndBringToFront(Graph, false);
					bBroughtGraphToFront = true;
				}
			}

			if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph))
			{
				RigGraph->OnGraphNodeClicked.AddSP(this, &FControlRigEditor::OnGraphNodeClicked);
			}
		}
	}

	{
		if (URigVMGraph* Model = InControlRigBlueprint->Model)
		{
			if (Model->GetNodes().Num() == 0)
			{
				URigVMNode* Node = InControlRigBlueprint->Controller->AddStructNode(FRigUnit_BeginExecution::StaticStruct(), TEXT("Execute"), FVector2D::ZeroVector, FString(), false);
				if (Node)
				{
					TArray<FName> NodeNames;
					NodeNames.Add(Node->GetFName());
					InControlRigBlueprint->Controller->SetNodeSelection(NodeNames, false);
				}
			}
			else
			{
				InControlRigBlueprint->RebuildGraphFromModel();

				// selection state does not need to be persistent, even though it is saved in the RigVM.
				InControlRigBlueprint->Controller->ClearNodeSelection(false);

				if (UPackage* Package = InControlRigBlueprint->GetOutermost())
				{
					Package->SetDirtyFlag(InControlRigBlueprint->bDirtyDuringLoad);
				}
			}
		}

		InControlRigBlueprint->HierarchyContainer.OnElementAdded.AddSP(this, &FControlRigEditor::OnRigElementAdded);
		InControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddSP(this, &FControlRigEditor::OnRigElementRemoved, false);
		InControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddSP(this, &FControlRigEditor::OnRigElementRenamed);
		InControlRigBlueprint->HierarchyContainer.OnElementReparented.AddSP(this, &FControlRigEditor::OnRigElementReparented);
		InControlRigBlueprint->HierarchyContainer.OnElementSelected.AddSP(this, &FControlRigEditor::OnRigElementSelected);
		InControlRigBlueprint->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.AddSP(this, &FControlRigEditor::OnControlUISettingChanged);
		InControlRigBlueprint->OnRefreshEditor().AddSP(this, &FControlRigEditor::HandleRefreshEditorFromBlueprint);
		InControlRigBlueprint->OnVariableDropped().AddSP(this, &FControlRigEditor::HandleVariableDroppedFromBlueprint);

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

	UpdateStaleWatchedPins();
	bControlRigEditorInitialized = true;
}

void FControlRigEditor::BindCommands()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	/*
	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().ExecuteGraph,
		FExecuteAction::CreateSP(this, &FControlRigEditor::ToggleExecuteGraph), 
		FCanExecuteAction(), 
		FIsActionChecked::CreateSP(this, &FControlRigEditor::IsExecuteGraphOn));
	*/

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().AutoCompileGraph,
		FExecuteAction::CreateSP(this, &FControlRigEditor::ToggleAutoCompileGraph), 
		FIsActionChecked::CreateSP(this, &FControlRigEditor::CanAutoCompileGraph),
		FIsActionChecked::CreateSP(this, &FControlRigEditor::IsAutoCompileGraphOn));

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().ToggleEventQueue,
		FExecuteAction::CreateSP(this, &FControlRigEditor::ToggleEventQueue),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().SetupEvent,
		FExecuteAction::CreateSP(this, &FControlRigEditor::SetEventQueue, EControlRigEditorEventQueue::Setup),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().UpdateEvent,
		FExecuteAction::CreateSP(this, &FControlRigEditor::SetEventQueue, EControlRigEditorEventQueue::Update),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().InverseEvent,
		FExecuteAction::CreateSP(this, &FControlRigEditor::SetEventQueue, EControlRigEditorEventQueue::Inverse),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FControlRigBlueprintCommands::Get().InverseAndUpdateEvent,
		FExecuteAction::CreateSP(this, &FControlRigEditor::SetEventQueue, EControlRigEditorEventQueue::InverseAndUpdate),
		FCanExecuteAction());
}

void FControlRigEditor::ToggleExecuteGraph()
{
	if (ControlRig)
	{
		bExecutionControlRig = !bExecutionControlRig;

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

void FControlRigEditor::ToggleAutoCompileGraph()
{
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		RigBlueprint->bAutoRecompileVM = !RigBlueprint->bAutoRecompileVM;
		if (RigBlueprint->bAutoRecompileVM)
		{
			RigBlueprint->RequestAutoVMRecompilation();
		}
	}
}

bool FControlRigEditor::IsAutoCompileGraphOn() const
{
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		return RigBlueprint->bAutoRecompileVM;
	}
	return false;
}

void FControlRigEditor::ToggleEventQueue()
{
	SetEventQueue(LastEventQueue);
}

TSharedRef<SWidget> FControlRigEditor::GenerateEventQueueMenuContent()
{
	FMenuBuilder MenuBuilder(true, GetToolkitCommands());

	MenuBuilder.BeginSection(TEXT("Events"));
	MenuBuilder.AddMenuEntry(FControlRigBlueprintCommands::Get().SetupEvent, TEXT("Setup"), TAttribute<FText>(), TAttribute<FText>(), GetEventQueueIcon(EControlRigEditorEventQueue::Setup));
	MenuBuilder.AddMenuEntry(FControlRigBlueprintCommands::Get().UpdateEvent, TEXT("Update"), TAttribute<FText>(), TAttribute<FText>(), GetEventQueueIcon(EControlRigEditorEventQueue::Update));
	MenuBuilder.AddMenuEntry(FControlRigBlueprintCommands::Get().InverseEvent, TEXT("Inverse"), TAttribute<FText>(), TAttribute<FText>(), GetEventQueueIcon(EControlRigEditorEventQueue::Inverse));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("Validation"));
	MenuBuilder.AddMenuEntry(FControlRigBlueprintCommands::Get().InverseAndUpdateEvent, TEXT("InverseAndUpdate"), TAttribute<FText>(), TAttribute<FText>(), GetEventQueueIcon(EControlRigEditorEventQueue::InverseAndUpdate));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
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
		/*
		ToolbarBuilder.AddToolBarButton(FControlRigBlueprintCommands::Get().ExecuteGraph,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.ExecuteGraph"));
		*/

		ToolbarBuilder.AddToolBarButton(
			FControlRigBlueprintCommands::Get().ToggleEventQueue,
			NAME_None, 
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FControlRigEditor::GetEventQueueLabel)),
			TAttribute<FText>(), 
			TAttribute<FSlateIcon>::Create(TAttribute<FSlateIcon>::FGetter::CreateSP(this, &FControlRigEditor::GetEventQueueIcon))
		);

		FUIAction DefaultAction;
		ToolbarBuilder.AddComboButton(
			DefaultAction,
			FOnGetContent::CreateSP(this, &FControlRigEditor::GenerateEventQueueMenuContent),
			LOCTEXT("EventQueue_Label", "Available Events"),
			LOCTEXT("EventQueue_ToolTip", "Pick between different events / modes for testing the Control Rig"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Recompile"),
			true);

		ToolbarBuilder.AddToolBarButton(FControlRigBlueprintCommands::Get().AutoCompileGraph,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.AutoCompileGraph"));

		ToolbarBuilder.AddWidget(SNew(SBlueprintEditorSelectedDebugObjectWidget, SharedThis(this)));
	}
	ToolbarBuilder.EndSection();
}

EControlRigEditorEventQueue FControlRigEditor::GetEventQueue() const
{
	if (ControlRig)
	{
		if (bSetupModeEnabled)
		{
			return EControlRigEditorEventQueue::Setup;
		}

		TArray<FName> EventQueue = ControlRig->EventQueue;
		if (EventQueue.Num() == 1)
		{
			if (EventQueue[0] == FRigUnit_PrepareForExecution::EventName)
			{
				return EControlRigEditorEventQueue::Setup;
			}
			else if (EventQueue[0] == FRigUnit_BeginExecution::EventName)
			{
				return EControlRigEditorEventQueue::Update;
			}
			else if (EventQueue[0] == FRigUnit_InverseExecution::EventName)
			{
				return EControlRigEditorEventQueue::Inverse;
			}
		}
		else if (EventQueue.Num() == 2)
		{
			if (EventQueue[0] == FRigUnit_InverseExecution::EventName &&
				EventQueue[1] == FRigUnit_BeginExecution::EventName)
			{
				return EControlRigEditorEventQueue::InverseAndUpdate;
			}
		}
	}

	return EControlRigEditorEventQueue::Update;
}

void FControlRigEditor::SetEventQueue(EControlRigEditorEventQueue InEventQueue)
{
	if (GetEventQueue() == InEventQueue)
	{
		return;
	}

	LastEventQueue = GetEventQueue();

	if (ControlRig)
	{
		TArray<FName> EventNames;
		switch (InEventQueue)
		{
			case EControlRigEditorEventQueue::Setup:
			{
				if (!bSetupModeEnabled)
				{
					ToggleSetupMode();
				}
				return;
			}
			case EControlRigEditorEventQueue::Update:
			{
				EventNames.Add(FRigUnit_BeginExecution::EventName);
				break;
			}
			case EControlRigEditorEventQueue::Inverse:
			{
				EventNames.Add(FRigUnit_InverseExecution::EventName);
				break;
			}
			case EControlRigEditorEventQueue::InverseAndUpdate:
			{
				EventNames.Add(FRigUnit_InverseExecution::EventName);
				EventNames.Add(FRigUnit_BeginExecution::EventName);
				break;
			}
		}

		if (EventNames.Num() > 0)
		{
			ControlRig->SetEventQueue(EventNames);

			if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
			{
				RigBlueprint->Validator->SetControlRig(ControlRig);
			}
		}

		if (bSetupModeEnabled)
		{
			ToggleSetupMode();
		}
	}
}

int32 FControlRigEditor::GetEventQueueComboValue() const
{
	return (int32)GetEventQueue();
}

FText FControlRigEditor::GetEventQueueLabel() const
{
	EControlRigEditorEventQueue EventQueue = GetEventQueue();
	switch (EventQueue)
	{
		case EControlRigEditorEventQueue::Setup:
		{
			return FRigUnit_PrepareForExecution::StaticStruct()->GetDisplayNameText();
		}
		case EControlRigEditorEventQueue::Update:
		{
			return FRigUnit_BeginExecution::StaticStruct()->GetDisplayNameText();
		}
		case EControlRigEditorEventQueue::Inverse:
		{
			return FRigUnit_InverseExecution::StaticStruct()->GetDisplayNameText();
		}
		case EControlRigEditorEventQueue::InverseAndUpdate:
		{
			return FText::FromString(FString::Printf(TEXT("%s and %s"),
				*FRigUnit_InverseExecution::StaticStruct()->GetDisplayNameText().ToString(),
				*FRigUnit_BeginExecution::StaticStruct()->GetDisplayNameText().ToString()));
		}
	}
	return StaticEnum<EControlRigEditorEventQueue>()->GetDisplayNameTextByValue((int64)EventQueue);
}

FSlateIcon FControlRigEditor::GetEventQueueIcon(EControlRigEditorEventQueue InEventQueue)
{
	switch (InEventQueue)
	{
		case EControlRigEditorEventQueue::Setup:
		{
			return FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.SetupMode");
		}
		case EControlRigEditorEventQueue::Update:
		{
			return FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.UpdateEvent");
		}
		case EControlRigEditorEventQueue::Inverse:
		{
			return FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.InverseEvent");
		}
		case EControlRigEditorEventQueue::InverseAndUpdate:
		{
			return FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.InverseAndUpdate");
		}
	}

	return FSlateIcon();
}

FSlateIcon FControlRigEditor::GetEventQueueIcon() const
{
	return GetEventQueueIcon(GetEventQueue());
}

void FControlRigEditor::OnEventQueueComboChanged(int32 InValue, ESelectInfo::Type InSelectInfo)
{
	SetEventQueue((EControlRigEditorEventQueue)InValue);
}

void FControlRigEditor::ToggleSetupMode()
{
	bSetupModeEnabled = !bSetupModeEnabled;

	FRigElementKey PreviousRigElementInDetailPanel = RigElementInDetailPanel;
	TArray<FRigElementKey> PreviousSelection;

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (RigBlueprint->bAutoRecompileVM)
		{
			RigBlueprint->RequestAutoVMRecompilation();
		}

		RigBlueprint->Validator->SetControlRig(ControlRig);

		// need to copy here since the removal changes the iterator
		if (ControlRig)
		{
			TArray<FRigControl> TransientControls = ControlRig->TransientControls;
			for (FRigControl TransientControl : TransientControls)
			{
				RigBlueprint->RemoveTransientControl(TransientControl.GetElementKey());
			}
		}

		PreviousSelection = RigBlueprint->HierarchyContainer.CurrentSelection();
		RigBlueprint->HierarchyContainer.ClearSelection();
	}

	if (ControlRig)
	{
		ControlRig->bSetupModeEnabled = bSetupModeEnabled;
		if (bSetupModeEnabled)
		{
			ControlRig->Initialize(true);
			ControlRig->RequestSetup();
		}
	}

	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
		{
			EditMode->RecreateGizmoActors(RigBlueprint->HierarchyContainer.CurrentSelection());
		}

		EditMode->Settings->bDisplaySpaces = bSetupModeEnabled;
	}

	if (PreviousSelection.Num() > 0)
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
		{
			for (FRigElementKey SelectedKey : PreviousSelection)
			{
				RigBlueprint->HierarchyContainer.Select(SelectedKey, true);
			}
		}
	}

	if (PreviousRigElementInDetailPanel.IsValid())
	{
		ClearDetailObject();
		SetDetailStruct(PreviousRigElementInDetailPanel);
	}
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

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (UControlRigBlueprintGeneratedClass* GeneratedClass = RigBlueprint->GetControlRigBlueprintGeneratedClass())
		{
			UControlRig* CDO = Cast<UControlRig>(GeneratedClass->GetDefaultObject(true /* create if needed */));
			if (CDO->VM->GetInstructions().Num() <= 1 /* only exit */)
			{
				RigBlueprint->RecompileVM();
				RigBlueprint->RequestControlRigInit();
			}
		}

		RigBlueprint->Validator->SetControlRig(DebuggedControlRig);
	}

	if (DebuggedControlRig)
	{
		bool bIsExternalControlRig = DebuggedControlRig != ControlRig;
		bool bShouldExecute = (!bIsExternalControlRig) && bExecutionControlRig;
		DebuggedControlRig->ControlRigLog = &ControlRigLog;

		UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
		if (EditorSkelComp)
		{
			UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(EditorSkelComp->GetAnimInstance());
			if (AnimInstance)
			{
				FControlRigIOSettings IOSettings = FControlRigIOSettings::MakeEnabled();
				IOSettings.bUpdatePose = bShouldExecute;
				IOSettings.bUpdateCurves = bShouldExecute;

				// we might want to move this into another method
				FInputBlendPose Filter;
				AnimInstance->ResetControlRigTracks();
				AnimInstance->AddControlRigTrack(0, DebuggedControlRig);
				AnimInstance->UpdateControlRigTrack(0, 1.0f, IOSettings, bShouldExecute);
				AnimInstance->RecalcRequiredBones();

				// since rig has changed, rebuild draw skeleton
				EditorSkelComp->RebuildDebugDrawSkeleton();
				if (FControlRigEditMode* EditMode = GetEditMode())
				{
					EditMode->SetObjects(DebuggedControlRig, EditorSkelComp,nullptr);
				}
			}
		}
	}
	else
	{
		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->SetObjects(nullptr,  nullptr,nullptr);
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
	ClearDetailObject();

	if (InObjects.Num() == 1)
	{
		if (InObjects[0]->GetClass()->GetDefaultObject() == InObjects[0])
		{
			EditClassDefaults_Clicked();
			return;
		}
		else if (InObjects[0] == GetBlueprintObj())
		{
			EditGlobalOptions_Clicked();
			return;
		}
	}

	RigElementInDetailPanel = FRigElementKey();
	StructToDisplay.Reset();
	SKismetInspector::FShowDetailsOptions Options;
	Options.bForceRefresh = true;
	Inspector->ShowDetailsForObjects(InObjects);
}

void FControlRigEditor::SetDetailObject(UObject* Obj)
{
	if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(Obj))
	{
		ClearDetailObject();
		NodeDetailStruct = StructNode->GetScriptStruct();
		NodeDetailName = StructNode->GetFName();

		if (NodeDetailStruct)
		{
			NodeDetailBuffer.AddUninitialized(NodeDetailStruct->GetStructureSize());
			NodeDetailStruct->InitializeDefaultValue(NodeDetailBuffer.GetData());

			FString StructDefaultValue = StructNode->GetStructDefaultValue();
			NodeDetailStruct->ImportText(*StructDefaultValue, NodeDetailBuffer.GetData(), nullptr, PPF_None, nullptr, NodeDetailStruct->GetName());

			StructToDisplay = MakeShareable(new FStructOnScope(NodeDetailStruct, (uint8*)NodeDetailBuffer.GetData()));
			if (StructToDisplay.IsValid())
			{
				StructToDisplay->SetPackage(GetControlRigBlueprint()->GetOutermost());
			}

			// mark all input properties with editanywhere
			for (TFieldIterator<FProperty> PropertyIt(NodeDetailStruct); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (!Property->HasMetaData(TEXT("Input")))
				{
					continue;
				}

				// filter out execute pins
				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
					{
						continue;
					}
				}

				bool bEditable = true;
				if (URigVMPin* Pin = StructNode->FindPin(Property->GetName()))
				{
					Pin = Pin->GetPinForLink();
					if (Pin->GetDirection() == ERigVMPinDirection::Output)
					{
						if (Pin->GetTargetLinks().Num() > 0)
						{
							bEditable = false;
						}
					}
					else if (Pin->GetDirection() == ERigVMPinDirection::Input ||
						Pin->GetDirection() == ERigVMPinDirection::IO)
					{
						if (Pin->GetSourceLinks().Num() > 0)
						{
							bEditable = false;
						}
					}
				}

				Property->SetPropertyFlags(Property->GetPropertyFlags() | CPF_Edit);

				if (bEditable)
				{
					Property->ClearPropertyFlags(CPF_EditConst);
				}
				else
				{
					Property->SetPropertyFlags(Property->GetPropertyFlags() | CPF_EditConst);
				}
			}

			FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			if (!PropertyEditorModule.GetClassNameToDetailLayoutNameMap().Contains(NodeDetailStruct->GetFName()))
			{
				PropertyEditorModule.RegisterCustomClassLayout(NodeDetailStruct->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigUnitDetails::MakeInstance));
			}

			Inspector->ShowSingleStruct(StructToDisplay);
		}
		return;
	}

	TArray<UObject*> Objects;
	if (Obj)
	{
		Objects.Add(Obj);
	}
	SetDetailObjects(Objects);
}

void FControlRigEditor::SetDetailStruct(const FRigElementKey& InElement)
{
	if (RigElementInDetailPanel == InElement)
	{
		return;
	}

	ClearDetailObject();
	
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	FRigHierarchyContainer* Container = &RigBlueprint->HierarchyContainer;
	
	if (!bSetupModeEnabled)
	{
		if (UControlRig* DebuggedControlRig = Cast<UControlRig>(RigBlueprint->GetObjectBeingDebugged()))
		{
			Container = &DebuggedControlRig->Hierarchy;
		}
	}

	if (Container->GetIndex(InElement) == INDEX_NONE)
	{
		return;
	}

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
			StructToDisplay = MakeShareable(new FStructOnScope(FRigBone::StaticStruct(), (uint8*)&(BoneHierarchy[InElement.Name])));
			break;
		}
		case ERigElementType::Control:
		{
			FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
			StructToDisplay = MakeShareable(new FStructOnScope(FRigControl::StaticStruct(), (uint8*)&(ControlHierarchy[InElement.Name])));
			break;
		}
		case ERigElementType::Space:
		{
			FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;
			StructToDisplay = MakeShareable(new FStructOnScope(FRigSpace::StaticStruct(), (uint8*)&(SpaceHierarchy[InElement.Name])));
			break;
		}
		case ERigElementType::Curve:
		{
			FRigCurveContainer& CurveContainer = Container->CurveContainer;
			StructToDisplay = MakeShareable(new FStructOnScope(FRigBone::StaticStruct(), (uint8*)&(CurveContainer[InElement.Name])));
			break;
		}
		default:
		{
			break;
		}
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

	if (NodeDetailBuffer.Num() > 0 && NodeDetailStruct != nullptr)
	{
		NodeDetailStruct->DestroyStruct(NodeDetailBuffer.GetData(), 1);
		NodeDetailBuffer.Reset();
		NodeDetailStruct = nullptr;
	}
	NodeDetailName = NAME_None;

	Inspector->ShowDetailsForObjects(TArray<UObject*>());
	Inspector->ShowSingleStruct(TSharedPtr<FStructOnScope>());

	SetUISelectionState(FBlueprintEditor::SelectionState_Graph);
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

		TUniquePtr<UControlRigBlueprint::FControlValueScope> ValueScope;
		if (!UControlRigSettings::Get()->bResetControlsOnCompile) // if we need to retain the controls
		{
			ValueScope = MakeUnique<UControlRigBlueprint::FControlValueScope>(GetControlRigBlueprint());
		}

		LastDebuggedRig.Empty();

		// force to disable the supended notif brackets
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
		{
			RigBlueprint->bSuspendModelNotificationsForOthers = false;
			RigBlueprint->bSuspendModelNotificationsForSelf = false;
		}

		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
		if (RigBlueprint == nullptr)
		{
			return;
		}

		FString LastDebuggedObjectName = GetCustomDebugObjectLabel(RigBlueprint->GetObjectBeingDebugged());
		RigBlueprint->SetObjectBeingDebugged(nullptr);

		FRigElementKey SelectedKey = RigElementInDetailPanel;
		TArray< TWeakObjectPtr<UObject> > SelectedObjects;
		if (SelectedKey.IsValid())
		{
			ClearDetailObject();
		}
		else
		{
			SelectedObjects = Inspector->GetSelectedObjects();
		}

		if (ControlRig)
		{
			ControlRig->OnInitialized_AnyThread().Clear();
			ControlRig->OnExecuted_AnyThread().Clear();
		}

		if (bSetupModeEnabled)
		{
			bSetupModeEnabled = false;
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
				UControlRig* CDO = Cast<UControlRig>(GeneratedClass->GetDefaultObject(true /* create if needed */));
				FRigVMInstructionArray Instructions = CDO->VM->GetInstructions();

				if (Instructions.Num() <= 1) // just the "done" operator
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
				RigBlueprint->SetObjectBeingDebugged(DebugObject.Object);
			}
		}

		if(SelectedKey.IsValid())
		{
			SetDetailStruct(SelectedKey);
		}
		else if (SelectedObjects.Num() > 0)
		{
			for (TWeakObjectPtr<UObject> SelectedObject : SelectedObjects)
			{
				if (SelectedObject.IsValid())
				{
					SetDetailObject(SelectedObject.Get());
					break;
				}
			}
		}

		if (PreviewInstance)
		{
			PreviewInstance->ResetModifiedBone();
		}

		if (UControlRigSettings::Get()->bResetControlTransformsOnCompile)
		{
			for (const FRigControl& Control : RigBlueprint->HierarchyContainer.ControlHierarchy)
			{
				FRigElementKey Key = Control.GetElementKey();
				FTransform Transform = RigBlueprint->HierarchyContainer.ControlHierarchy.GetLocalTransform(Control.Index, ERigControlValueType::Initial);

				/*/
				if (ControlRig)
				{
					ControlRig->Modify();
					ControlRig->GetControlHierarchy().SetLocalTransform(Control.Index, Transform);
					ControlRig->ControlModified().Broadcast(ControlRig, Control, EControlRigSetKey::DoNotCare);
				}
				*/

				RigBlueprint->HierarchyContainer.SetLocalTransform(Key, Transform);
			}
		}

		RigBlueprint->PropagatePoseFromBPToInstances();

		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->RecreateGizmoActors(RigBlueprint->HierarchyContainer.CurrentSelection());
		}
	}

	// enable this for creating a new unit test
	// DumpUnitTestCode();

	// FStatsHierarchical::EndMeasurements();
	// FMessageLog LogForMeasurements("ControlRigLog");
	// FStatsHierarchical::DumpMeasurements(LogForMeasurements);
}

void FControlRigEditor::SaveAsset_Execute()
{
	LastDebuggedRig = GetCustomDebugObjectLabel(GetBlueprintObj()->GetObjectBeingDebugged());
	FBlueprintEditor::SaveAsset_Execute();
}

void FControlRigEditor::SaveAssetAs_Execute()
{
	LastDebuggedRig = GetCustomDebugObjectLabel(GetBlueprintObj()->GetObjectBeingDebugged());
	FBlueprintEditor::SaveAssetAs_Execute();
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

bool FControlRigEditor::TransactionObjectAffectsBlueprint(UObject* InTransactedObject)
{
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return false;
	}

	if (InTransactedObject->GetOuter() == RigBlueprint->Controller)
	{
		return false;
	}
	return FBlueprintEditor::TransactionObjectAffectsBlueprint(InTransactedObject);
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

	bool DeletedAnything = false;
	RigBlueprint->Controller->OpenUndoBracket(TEXT("Delete selected nodes"));

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*NodeIt))
		{
			if (Node->CanUserDeleteNode())
			{
				AnalyticsTrackNodeEvent(GetBlueprintObj(), Node, true);
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
				{
					if(RigBlueprint->Controller->RemoveNodeByName(*RigNode->ModelNodePath))
					{
						DeletedAnything = true;
					}
				}
				else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
				{
					if(RigBlueprint->Controller->RemoveNodeByName(CommentNode->GetFName()))
					{
						DeletedAnything = true;
					}
				}
				else
				{
					Node->GetGraph()->RemoveNode(Node);
				}
			}
		}
	}

	if(DeletedAnything)
	{
		RigBlueprint->Controller->CloseUndoBracket();
	}
	else
	{
		RigBlueprint->Controller->CancelUndoBracket();
	}
}

bool FControlRigEditor::CanDeleteNodes() const
{
	return true;
}

void FControlRigEditor::CopySelectedNodes()
{
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	FString ExportedText = RigBlueprint->Controller->ExportSelectedNodesToText();
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FControlRigEditor::CanCopyNodes() const
{
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return false;
	}
	return RigBlueprint->Model->GetSelectNodes().Num() > 0;
}

bool FControlRigEditor::CanPasteNodes() const
{
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return false;
	}

	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	return RigBlueprint->Controller->CanImportNodesFromText(TextToImport);
}

void FControlRigEditor::PasteNodes()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (RigBlueprint == nullptr)
	{
		return;
	}

	RigBlueprint->Controller->OpenUndoBracket(TEXT("Pasted Nodes."));

	FVector2D PasteLocation = FSlateApplication::Get().GetCursorPos();

	TSharedPtr<SDockTab> ActiveTab = DocumentManager->GetActiveTab();
	if (ActiveTab.IsValid())
	{
		TSharedPtr<SGraphEditor> GraphEditor = StaticCastSharedRef<SGraphEditor>(ActiveTab->GetContent());
		if (GraphEditor.IsValid())
		{
			PasteLocation = GraphEditor->GetPasteLocation();

		}
	}

	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	TArray<FName> NodeNames = RigBlueprint->Controller->ImportNodesFromText(TextToImport);

	if (NodeNames.Num() > 0)
	{
		FBox2D Bounds;
		Bounds.bIsValid = false;

		for (const FName& NodeName : NodeNames)
		{
			const URigVMNode* Node = RigBlueprint->Model->FindNodeByName(NodeName);
			check(Node);

			FVector2D Position = Node->GetPosition();
			FVector2D Size = Node->GetSize();

			if (!Bounds.bIsValid)
			{
				Bounds.Min = Bounds.Max = Position;
				Bounds.bIsValid = true;
			}
			Bounds += Position;
			Bounds += Position + Size;
		}

		for (const FName& NodeName : NodeNames)
		{
			const URigVMNode* Node = RigBlueprint->Model->FindNodeByName(NodeName);
			check(Node);

			FVector2D Position = Node->GetPosition();
			RigBlueprint->Controller->SetNodePositionByName(NodeName, PasteLocation + Position - Bounds.GetCenter());
		}

		RigBlueprint->Controller->SetNodeSelection(NodeNames);
		RigBlueprint->Controller->CloseUndoBracket();
	}
	else
	{
		RigBlueprint->Controller->CancelUndoBracket();
	}
}

void FControlRigEditor::PostUndo(bool bSuccess)
{
	IControlRigEditor::PostUndo(bSuccess);
	EnsureValidRigElementInDetailPanel();

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (RigBlueprint->Status == BS_Dirty)
		{
			Compile();
		}

		USkeletalMesh* PreviewMesh = GetPersonaToolkit()->GetPreviewScene()->GetPreviewMesh();
		if (PreviewMesh != RigBlueprint->GetPreviewMesh())
		{
			RigBlueprint->SetPreviewMesh(PreviewMesh);
			GetPersonaToolkit()->SetPreviewMesh(PreviewMesh, true);
		}

		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->RecreateGizmoActors(RigBlueprint->HierarchyContainer.CurrentSelection());
		}
	}
}

void FControlRigEditor::PostRedo(bool bSuccess)
{
	IControlRigEditor::PostRedo(bSuccess);
	EnsureValidRigElementInDetailPanel();

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (RigBlueprint->Status == BS_Dirty)
		{
			Compile();
		}

		USkeletalMesh* PreviewMesh = GetPersonaToolkit()->GetPreviewScene()->GetPreviewMesh();
		if (PreviewMesh != RigBlueprint->GetPreviewMesh())
		{
			RigBlueprint->SetPreviewMesh(PreviewMesh);
			GetPersonaToolkit()->SetPreviewMesh(PreviewMesh, true);
		}

		if (FControlRigEditMode* EditMode = GetEditMode())
		{
			EditMode->RecreateGizmoActors(RigBlueprint->HierarchyContainer.CurrentSelection());
		}
	}
}

void FControlRigEditor::EnsureValidRigElementInDetailPanel()
{
	if (RigElementInDetailPanel.IsValid())
	{
		if (UControlRigBlueprint * RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
		{
			if (RigBlueprint->HierarchyContainer.GetIndex(RigElementInDetailPanel) == INDEX_NONE)
			{
				ClearDetailObject();
			}
		}
	}
}

void FControlRigEditor::OnStartWatchingPin()
{
	if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (UEdGraphPin* Pin = GetCurrentlySelectedPin())
		{
			ControlRigBlueprint->Controller->SetPinIsWatched(Pin->GetName(), true);
		}
	}
}

bool FControlRigEditor::CanStartWatchingPin() const
{
	if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (UEdGraphPin* Pin = GetCurrentlySelectedPin())
		{
			if (URigVMPin* ModelPin = ControlRigBlueprint->Model->FindPin(Pin->GetName()))
			{
				return ModelPin->GetParentPin() == nullptr && !ModelPin->RequiresWatch();
			}
		}
	}
	return false;
}

void FControlRigEditor::OnStopWatchingPin()
{
	if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (UEdGraphPin* Pin = GetCurrentlySelectedPin())
		{
			ControlRigBlueprint->Controller->SetPinIsWatched(Pin->GetName(), false);
		}
	}
}

bool FControlRigEditor::CanStopWatchingPin() const
{
	if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (UEdGraphPin* Pin = GetCurrentlySelectedPin())
		{
			if (URigVMPin* ModelPin = ControlRigBlueprint->Model->FindPin(Pin->GetName()))
			{
				return ModelPin->RequiresWatch();
			}
		}
	}
	return false;
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

void FControlRigEditor::OnAnimInitialized()
{
	UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		EditorSkelComp->bRequiredBonesUpToDateDuringTick = 0;

		UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(EditorSkelComp->GetAnimInstance());
		if (AnimInstance && ControlRig)
		{
			// update control rig data to anim instance since animation system has been reinitialized
			FInputBlendPose Filter;
			AnimInstance->ResetControlRigTracks();
			AnimInstance->AddControlRigTrack(0, ControlRig);
			AnimInstance->UpdateControlRigTrack(0, 1.0f, FControlRigIOSettings::MakeEnabled(), bExecutionControlRig);
		}
	}
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
		case NodeSectionID::VARIABLE:
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

void FControlRigEditor::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj());
	if (ControlRigBlueprint == nullptr)
	{
		return;
	}

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::NodeSelected:
		case ERigVMGraphNotifType::NodeDeselected:
		{
			URigVMNode* Node = Cast<URigVMNode>(InSubject);

			if (FocusedGraphEdPtr.IsValid())
			{
				TSharedPtr<SGraphEditor> FocusedGraphEd = FocusedGraphEdPtr.Pin();
				if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(FocusedGraphEd->GetCurrentGraph()))
				{
					if (InNotifType == ERigVMGraphNotifType::NodeSelected)
					{
						SetDetailObject(Node);
					}

					// if we used to have a rig unit selected, clear the details panel
					else if(InGraph->GetSelectNodes().Num() == 0)
					{
						if (StructToDisplay.IsValid())
						{
							if (StructToDisplay->GetStruct()->IsChildOf(FRigUnit::StaticStruct()))
							{
								ClearDetailObject();
							}
						}
					}

					if (!RigGraph->bIsSelecting)
					{
						TGuardValue<bool> SelectingGuard(RigGraph->bIsSelecting, true);
						if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
						{
							if (UEdGraphNode* EdNode = RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()))
							{
								FocusedGraphEd->SetNodeSelection(EdNode, InNotifType == ERigVMGraphNotifType::NodeSelected);
							}
						}
					}
				}
			}
			break;
		}
		case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			URigVMPin* Pin = Cast<URigVMPin>(InSubject);

			if (ControlRigBlueprint && NodeDetailBuffer.Num() > 0 && NodeDetailStruct != nullptr && !NodeDetailName.IsNone())
			{
				if (Pin->GetNode()->GetFName() == NodeDetailName)
				{
					URigVMPin* RootPin = Pin->GetRootPin();
					if (FProperty* Property = NodeDetailStruct->FindPropertyByName(RootPin->GetFName()))
					{
						FString DefaultValue = RootPin->GetDefaultValue();
						if (!DefaultValue.IsEmpty())
						{
							uint8* PropertyValuePtr = (uint8*)NodeDetailBuffer.GetData();
							PropertyValuePtr += Property->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
							Property->ImportText(*DefaultValue, PropertyValuePtr, PPF_None, nullptr);
						}
					}
				}
			}

			break;
		}
		case ERigVMGraphNotifType::PinArraySizeChanged:
		{
			URigVMPin* Pin = Cast<URigVMPin>(InSubject);

			if (ControlRigBlueprint && NodeDetailBuffer.Num() > 0 && NodeDetailStruct != nullptr && !NodeDetailName.IsNone())
			{
				if (Pin->GetNode()->GetFName() == NodeDetailName)
				{
					// refresh the details panel
					SetDetailObject(Pin->GetNode());
				}
			}

			break;
		}
		case ERigVMGraphNotifType::NodeSelectionChanged:
		default:
		{
			break;
		}
	}
}

void FControlRigEditor::HandleVMCompiledEvent(UBlueprint* InBlueprint, URigVM* InVM)
{
}

void FControlRigEditor::HandleControlRigExecutedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
	UpdateGraphCompilerErrors();
}

void FControlRigEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);

	bool bDrawHierarchyBones = false;

	// tick the control rig in case we don't have skeletal mesh
	if (UControlRigBlueprint* Blueprint = GetControlRigBlueprint())
	{
		if (Blueprint->GetPreviewMesh() == nullptr && 
			ControlRig != nullptr && 
			bExecutionControlRig)
		{
			ControlRig->SetDeltaTime(DeltaTime);
			ControlRig->Evaluate_AnyThread();
			bDrawHierarchyBones = true;
		}
	}

	if (bDrawHierarchyBones)
	{
		if (FControlRigEditorEditMode* EditMode = GetEditMode())
		{
			EditMode->bDrawHierarchyBones = bDrawHierarchyBones;
		}
	}
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

	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetFocusedGraph());
	if (RigGraph == nullptr)
	{
		return;
	}

	if (RigGraph->bIsSelecting || GIsTransacting)
	{
		return;
	}

	if (bFirstTimeSelecting)
	{
		bFirstTimeSelecting = false;
		return;
	}

	TGuardValue<bool> SelectGuard(RigGraph->bIsSelecting, true);

	UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj());
	if (ControlRigBlueprint)
	{
		TArray<FName> NodeNamesToSelect;
		for (UObject* Object : NewSelection)
		{
			if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(Object))
			{
				NodeNamesToSelect.Add(ControlRigGraphNode->GetModelNodeName());
			}
			else if(UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
			{
				if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
				{
					URigVMNode* ModelNode = ControlRigBlueprint->Model->FindNodeByName(Node->GetFName());
					if (ModelNode == nullptr)
					{
						TGuardValue<bool> BlueprintNotifGuard(ControlRigBlueprint->bSuspendModelNotificationsForOthers, true);
						FVector2D NodePos(CommentNode->NodePosX, CommentNode->NodePosY);
						FVector2D NodeSize(CommentNode->NodeWidth, CommentNode->NodeHeight);
						FLinearColor NodeColor = CommentNode->CommentColor;
						ControlRigBlueprint->Controller->AddCommentNode(CommentNode->NodeComment, NodePos, NodeSize, NodeColor, CommentNode->GetName(), true);
					}
				}
				NodeNamesToSelect.Add(Node->GetFName());
			}
		}
		ControlRigBlueprint->Controller->SetNodeSelection(NodeNamesToSelect, true);
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

			if (!LastDebuggedRig.IsEmpty())
			{
				TArray<FCustomDebugObject> DebugList;
				GetCustomDebugObjects(DebugList);

				for (const FCustomDebugObject& DebugObject : DebugList)
				{
					if (DebugObject.NameOverride == LastDebuggedRig)
					{
						GetBlueprintObj()->SetObjectBeingDebugged(DebugObject.Object);
						LastDebuggedRig.Empty();
						break;
					}
				}
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
						.IsEnabled(this, &FControlRigEditor::IsToolbarDrawSpacesEnabled)
						[
							SNew(SCheckBox)
							.IsChecked(this, &FControlRigEditor::GetToolbarDrawSpaces)
							.OnCheckStateChanged(this, &FControlRigEditor::OnToolbarDrawSpacesChanged)
							.ToolTipText(LOCTEXT("ControlRigDrawSpacesToolTip", "If checked all spaces are drawn as axes."))
						]
					],
					LOCTEXT("ControlRigDisplaySpaces", "Display Spaces")
				);

				InMenuBuilder.AddWidget(
					SNew(SBox)
					.HAlign(HAlign_Right)
					[
						SNew(SBox)
						.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
						.WidthOverride(100.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &FControlRigEditor::GetToolbarDrawAxesOnSelection)
							.OnCheckStateChanged(this, &FControlRigEditor::OnToolbarDrawAxesOnSelectionChanged)
							.ToolTipText(LOCTEXT("ControlRigDisplayAxesOnSelectionToolTip", "If checked axes will be drawn for all selected rig elements."))
						]
					],
					LOCTEXT("ControlRigDisplayAxesOnSelection", "Display Axes On Selection")
				);

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

				if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
				{
					for (UEdGraph* Graph : ControlRigBlueprint->UbergraphPages)
					{
						if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph))
						{
							const TArray<TSharedPtr<FString>>* BoneNameList = &RigGraph->GetBoneNameList();

							InMenuBuilder.AddWidget(
								SNew(SBox)
								.HAlign(HAlign_Right)
								[
									SNew(SBox)
									.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
									.WidthOverride(100.0f)
									.IsEnabled(this, &FControlRigEditor::IsPinControlNameListEnabled)
									[
										SAssignNew(PinControlNameList, SControlRigGraphPinNameListValueWidget)
										.OptionsSource(BoneNameList)
										.OnGenerateWidget(this, &FControlRigEditor::MakePinControlNameListItemWidget)
										.OnSelectionChanged(this, &FControlRigEditor::OnPinControlNameListChanged)
										.OnComboBoxOpening(this, &FControlRigEditor::OnPinControlNameListComboBox, BoneNameList)
										.InitiallySelectedItem(GetPinControlCurrentlySelectedItem(BoneNameList))
										.Content()
										[
											SNew(STextBlock)
											.Text(this, &FControlRigEditor::GetPinControlNameListText)
										]
									]
								],
								LOCTEXT("ControlRigAuthoringSpace", "Pin Control Space")
							);
							break;
						}
					}
				}
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

ECheckBoxState FControlRigEditor::GetToolbarDrawAxesOnSelection() const
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		return EditMode->Settings->bDisplayAxesOnSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void FControlRigEditor::OnToolbarDrawAxesOnSelectionChanged(ECheckBoxState InNewValue)
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		EditMode->Settings->bDisplayAxesOnSelection = InNewValue == ECheckBoxState::Checked;
	}
}

bool FControlRigEditor::IsToolbarDrawSpacesEnabled() const
{
	if (ControlRig)
	{
		if (!ControlRig->IsSetupModeEnabled())
		{
			return true;
		}
	}
	return false;
}

ECheckBoxState FControlRigEditor::GetToolbarDrawSpaces() const
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		return EditMode->Settings->bDisplaySpaces ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void FControlRigEditor::OnToolbarDrawSpacesChanged(ECheckBoxState InNewValue)
{
	if (FControlRigEditMode* EditMode = GetEditMode())
	{
		EditMode->Settings->bDisplaySpaces = InNewValue == ECheckBoxState::Checked;
	}
}

bool FControlRigEditor::IsPinControlNameListEnabled() const
{
	if (ControlRig)
	{
		if (ControlRig->TransientControls.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

TSharedRef<SWidget> FControlRigEditor::MakePinControlNameListItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));
}

FText FControlRigEditor::GetPinControlNameListText() const
{
	if (ControlRig)
	{
		if (ControlRig->TransientControls.Num() > 0)
		{
			return FText::FromName(ControlRig->TransientControls[0].ParentName);
		}
	}
	return FText::FromName(NAME_None);
}

TSharedPtr<FString> FControlRigEditor::GetPinControlCurrentlySelectedItem(const TArray<TSharedPtr<FString>>* InNameList) const
{
	FString CurrentItem = GetPinControlNameListText().ToString();
	for (const TSharedPtr<FString>& Item : *InNameList)
	{
		if (Item->Equals(CurrentItem))
		{
			return Item;
		}
	}
	return TSharedPtr<FString>();
}

void FControlRigEditor::SetPinControlNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if (ControlRig)
	{
		if (ControlRig->TransientControls.Num() > 0)
		{
			FRigControl& Control = ControlRig->TransientControls[0];

			Control.ParentIndex = ControlRig->Hierarchy.BoneHierarchy.GetIndex(*NewTypeInValue.ToString());
			if (Control.ParentIndex == INDEX_NONE)
			{
				Control.ParentName = NAME_None;
			}
			else
			{
				Control.ParentName = ControlRig->Hierarchy.BoneHierarchy[Control.ParentIndex].Name;
			}

			// find out if the controlled pin is part of a visual debug node
			if (UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(GetBlueprintObj()))
			{
				if (URigVMPin* ControlledPin = ControlRigBlueprint->Model->FindPin(Control.Name.ToString()))
				{
					URigVMNode* ControlledNode = ControlledPin->GetPinForLink()->GetNode();
					if (URigVMPin* BoneSpacePin = ControlledNode->FindPin(TEXT("BoneSpace")))
					{
						if (BoneSpacePin->GetCPPType() == TEXT("FName") && BoneSpacePin->GetCustomWidgetName() == TEXT("BoneName"))
						{
							ControlRigBlueprint->Controller->SetPinDefaultValue(BoneSpacePin->GetPinPath(), Control.ParentName.ToString(), false, false, false);
						}
					}
				}
			}
		}
	}
}

void FControlRigEditor::OnPinControlNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetPinControlNameListText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void FControlRigEditor::OnPinControlNameListComboBox(const TArray<TSharedPtr<FString>>* InNameList)
{
	TSharedPtr<FString> CurrentlySelected = GetPinControlCurrentlySelectedItem(InNameList);
	PinControlNameList->SetSelectedItem(CurrentlySelected);
}

void FControlRigEditor::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);

	// Create the preview component
	UControlRigSkeletalMeshComponent* EditorSkelComp = NewObject<UControlRigSkeletalMeshComponent>(Actor);
	EditorSkelComp->SetSkeletalMesh(InPersonaPreviewScene->GetPersonaToolkit()->GetPreviewMesh());
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorSkelComp);
	bool bWasCreated = false;
	FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UControlRigLayerInstance>(EditorSkelComp, bWasCreated);
	InPersonaPreviewScene->AddComponent(EditorSkelComp, FTransform::Identity);

	// set root component, so we can attach to it. 
	Actor->SetRootComponent(EditorSkelComp);

	PreviewInstance = nullptr;
	if (UControlRigLayerInstance* ControlRigLayerInstance = Cast<UControlRigLayerInstance>(EditorSkelComp->GetAnimInstance()))
	{
		PreviewInstance = Cast<UAnimPreviewInstance>(ControlRigLayerInstance->GetSourceAnimInstance());
	}
	else
	{
		PreviewInstance = Cast<UAnimPreviewInstance>(EditorSkelComp->GetAnimInstance());
	}

	if (GEditor)
	{
		// remove the preview scene undo handling - it has unwanted side effects
		FAnimationEditorPreviewScene* AnimationEditorPreviewScene = static_cast<FAnimationEditorPreviewScene*>(&InPersonaPreviewScene.Get());
		if (AnimationEditorPreviewScene)
		{
			GEditor->UnregisterForUndo(AnimationEditorPreviewScene);
		}
	}
}

void FControlRigEditor::UpdateControlRig()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if(UClass* Class = Blueprint->GeneratedClass)
	{
		UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
		UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(EditorSkelComp->GetAnimInstance());

		if (AnimInstance)
		{
			if (ControlRig)
			{
				// if this control rig is from a temporary step,
				// for example the reinstancing class, clear it 
				// and create a new one!
				if (ControlRig->GetClass() != Class)
				{
					ControlRig = nullptr;
				}
			}

			if (ControlRig == nullptr)
			{
				ControlRig = NewObject<UControlRig>(EditorSkelComp, Class);
				// this is editing time rig
				ControlRig->ExecutionType = ERigExecutionType::Editing;
				ControlRig->ControlRigLog = &ControlRigLog;

				ControlRig->InitializeFromCDO();
 			}

			ControlRig->PreviewInstance = PreviewInstance;
			ControlRig->bSetupModeEnabled = bSetupModeEnabled;

			if (UControlRig* CDO = Cast<UControlRig>(Class->GetDefaultObject()))
			{
				CDO->GizmoLibrary = GetControlRigBlueprint()->GizmoLibrary;
			}

			CacheNameLists();

			// When the control rig is re-instanced on compile, it loses its binding, so we refresh it here if needed
			if (!ControlRig->GetObjectBinding().IsValid())
			{
				ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
			}
			
			// Make sure the object being debugged is the preview instance
			GetBlueprintObj()->SetObjectBeingDebugged(ControlRig);

			// initialize is moved post reinstance
			FInputBlendPose Filter;
			AnimInstance->ResetControlRigTracks();
			AnimInstance->AddControlRigTrack(0, ControlRig);
			AnimInstance->UpdateControlRigTrack(0, 1.0f, FControlRigIOSettings::MakeEnabled(), bExecutionControlRig);
			AnimInstance->RecalcRequiredBones();

			// since rig has changed, rebuild draw skeleton
			EditorSkelComp->RebuildDebugDrawSkeleton();
			if (FControlRigEditMode* EditMode = GetEditMode())
			{
				EditMode->SetObjects(ControlRig, EditorSkelComp,nullptr);
			}

			Blueprint->SetFlags(RF_Transient);
			Blueprint->RecompileVM();
			Blueprint->ClearFlags(RF_Transient);

			ControlRig->OnInitialized_AnyThread().AddSP(this, &FControlRigEditor::HandleControlRigExecutedEvent);
			ControlRig->OnExecuted_AnyThread().AddSP(this, &FControlRigEditor::HandleControlRigExecutedEvent);
			ControlRig->RequestInit();
			ControlRig->ControlModified().AddSP(this, &FControlRigEditor::HandleOnControlModified);
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

			RigGraph->CacheNameLists(&ControlRigBP->HierarchyContainer, &ControlRigBP->DrawContainer);
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

	if (GetObjectsCurrentlyBeingEdited()->Num() > 0)
	{
		if (UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint())
		{
			ControlRigBP->SetPreviewMesh(InNewSkeletalMesh);
		}
	}
}

void FControlRigEditor::RebindToSkeletalMeshComponent()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UDebugSkelMeshComponent* MeshComponent = GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent();
	if (MeshComponent)
	{
		bool bWasCreated = false;
		FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UControlRigLayerInstance>(MeshComponent , bWasCreated);
	}
}

void FControlRigEditor::UpdateStaleWatchedPins()
{
	UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint();
	if (ControlRigBP == nullptr)
	{
		return;
	}

	TSet<UEdGraphPin*> AllPins;

	// Find all unique pins being watched
	for (const FEdGraphPinReference& PinRef : ControlRigBP->WatchedPins)
	{
		UEdGraphPin* Pin = PinRef.Get();
		if (Pin == nullptr)
		{
			continue;
		}

		UEdGraphNode* OwningNode = Pin->GetOwningNode();
		// during node reconstruction, dead pins get moved to the transient 
		// package (so just in case this blueprint got saved with dead pin watches)
		if (OwningNode == nullptr)
		{
			continue;
		}

		if (!OwningNode->Pins.Contains(Pin))
		{
			continue;
		}

		AllPins.Add(Pin);
	}

	// Refresh watched pins with unique pins (throw away null or duplicate watches)
	if (ControlRigBP->WatchedPins.Num() != AllPins.Num())
	{
		ControlRigBP->Status = BS_Dirty;
	}

	ControlRigBP->WatchedPins.Empty();

	for (URigVMNode* ModelNode : ControlRigBP->Model->GetNodes())
	{
		TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
		for (URigVMPin* ModelPin : ModelPins)
		{
			if (ModelPin->RequiresWatch())
			{
				ControlRigBP->Controller->SetPinIsWatched(ModelPin->GetPinPath(), false, false);
			}
		}
	}

	for (UEdGraphPin* Pin : AllPins)
	{
		ControlRigBP->WatchedPins.Add(Pin);
		ControlRigBP->Controller->SetPinIsWatched(Pin->GetName(), true, false);
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
			ControlRigBP->Controller->SetCommentTextByName(CommentBeingChanged->GetFName(), NewText.ToString(), true);
		}
	}
}

FTransform FControlRigEditor::GetRigElementTransform(const FRigElementKey& InElement, bool bLocal, bool bOnDebugInstance) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(bOnDebugInstance)
	{
		UControlRig* DebuggedControlRig = Cast<UControlRig>(GetBlueprintObj()->GetObjectBeingDebugged());
		if (DebuggedControlRig == nullptr)
		{
			DebuggedControlRig = ControlRig;
		}

		if (DebuggedControlRig)
		{
			if (bLocal)
			{
				return DebuggedControlRig->GetHierarchy()->GetLocalTransform(InElement);
			}
			return DebuggedControlRig->GetHierarchy()->GetGlobalTransform(InElement);
		}
	}

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
					ParentTransform = GetRigElementTransform(ParentKey, false, false);
				}
				Transform = Transform * ParentTransform;
				Transform.NormalizeRotation();
			}

			ControlRigBP->HierarchyContainer.BoneHierarchy.SetInitialGlobalTransform(InElement.Name, Transform);
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
			ControlRigBP->HierarchyContainer.ControlHierarchy.SetLocalTransform(InElement.Name, LocalTransform, ERigControlValueType::Initial);
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

void FControlRigEditor::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	FBlueprintEditor::NotifyPreChange(PropertyAboutToChange);

	if (UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint())
	{
		if (RigElementInDetailPanel.IsValid())
		{
			ControlRigBP->Modify();
		}
	}
}

void FControlRigEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	FBlueprintEditor::NotifyPostChange(PropertyChangedEvent, PropertyThatChanged);
}

void FControlRigEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* ControlRigBP = GetControlRigBlueprint();

	if (ControlRigBP)
	{
		if (PropertyChangedEvent.MemberProperty->GetNameCPP() == TEXT("VMCompileSettings"))
		{
			ControlRigBP->RecompileVM();
			return;
		}

		if (PropertyChangedEvent.MemberProperty->GetNameCPP() == TEXT("DrawContainer"))
		{
			ControlRigBP->PropagateDrawInstructionsFromBPToInstances();
			return;
		}
	}

	if (ControlRig && ControlRigBP && NodeDetailBuffer.Num() > 0 && NodeDetailStruct != nullptr && !NodeDetailName.IsNone())
	{
		const FRigUnit* RigUnitPtr = (const FRigUnit * )NodeDetailBuffer.GetData();
		const uint8* MemberMemoryPtr = PropertyChangedEvent.MemberProperty->ContainerPtrToValuePtr<uint8>(RigUnitPtr);
		FString DefaultValue = FRigVMStruct::ExportToFullyQualifiedText(PropertyChangedEvent.MemberProperty, MemberMemoryPtr);
		if (!DefaultValue.IsEmpty())
		{
			FString PinPath = FString::Printf(TEXT("%s.%s"), *NodeDetailName.ToString(), *PropertyChangedEvent.MemberProperty->GetName());
			ControlRigBP->Controller->SetPinDefaultValue(PinPath, DefaultValue, true, true);
		}
	}

	if (ControlRig && ControlRigBP && RigElementInDetailPanel)
	{
		UControlRig* DebuggedControlRig = Cast<UControlRig>(GetBlueprintObj()->GetObjectBeingDebugged());
		check(DebuggedControlRig);

		if (DebuggedControlRig->GetHierarchy()->GetIndex(RigElementInDetailPanel) == INDEX_NONE)
		{
			return;
		}

		UScriptStruct* ScriptStruct = PropertyChangedEvent.MemberProperty->GetOwner<UScriptStruct>();
		if (ScriptStruct)
		{
			FRigHierarchyContainer* Container = &ControlRigBP->HierarchyContainer;
			if (!bSetupModeEnabled)
			{
				Container = &DebuggedControlRig->Hierarchy;
			}

			if (ScriptStruct == FRigBone::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Bone)
			{
				if (PropertyChangedEvent.MemberProperty->GetFName() == TEXT("LocalTransform"))
				{
					FRigBone& Bone = Container->BoneHierarchy[RigElementInDetailPanel.Name];
					FTransform ParentTransform = FTransform::Identity;
					if (Bone.ParentIndex != INDEX_NONE)
					{
						ParentTransform = Container->BoneHierarchy.GetGlobalTransform(Bone.ParentIndex);
					}
					Bone.GlobalTransform = Bone.LocalTransform * ParentTransform;

					if (!bSetupModeEnabled)
					{
						ControlRigBP->PropagatePropertyFromInstanceToBP(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty, DebuggedControlRig);
					}
					ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty);
					ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, ScriptStruct->FindPropertyByName(TEXT("GlobalTransform")));
				}
				else if (PropertyChangedEvent.MemberProperty->GetFName() == TEXT("GlobalTransform"))
				{
					FRigBone& Bone = Container->BoneHierarchy[RigElementInDetailPanel.Name];
					FTransform ParentTransform = FTransform::Identity;
					if (Bone.ParentIndex != INDEX_NONE)
					{
						ParentTransform = Container->BoneHierarchy.GetGlobalTransform(Bone.ParentIndex);
					}
					Bone.LocalTransform = Bone.GlobalTransform.GetRelativeTransform(ParentTransform);

					if (!bSetupModeEnabled)
					{
						ControlRigBP->PropagatePropertyFromInstanceToBP(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty, DebuggedControlRig);
					}
					ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty);
					ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, ScriptStruct->FindPropertyByName(TEXT("LocalTransform")));
				}

				ControlRig->SetTransientControlValue(RigElementInDetailPanel);
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
				{
					DebuggedControlRig->SetTransientControlValue(RigElementInDetailPanel);
				}

				if (PreviewInstance)
				{
					if (FAnimNode_ModifyBone* Modify = PreviewInstance->FindModifiedBone(RigElementInDetailPanel.Name))
					{
						FTransform LocalTransform = Container->BoneHierarchy[RigElementInDetailPanel.Name].LocalTransform;
						Modify->Translation = LocalTransform.GetTranslation();
						Modify->Rotation = LocalTransform.GetRotation().Rotator();
						Modify->TranslationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
						Modify->RotationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
					}
				}
			}
			else if (ScriptStruct == FRigSpace::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Space)
			{
				ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty);

				if (PropertyChangedEvent.MemberProperty->GetName() == TEXT("InitialTransform"))
				{
					FRigSpace& Space = Container->SpaceHierarchy[RigElementInDetailPanel.Name];
					Space.LocalTransform = Space.InitialTransform;

					if (!bSetupModeEnabled)
					{
						ControlRigBP->PropagatePropertyFromInstanceToBP(RigElementInDetailPanel, ScriptStruct->FindPropertyByName(TEXT("LocalTransform")), DebuggedControlRig);
					}
					ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, ScriptStruct->FindPropertyByName(TEXT("LocalTransform")));
				}

				ControlRig->SetTransientControlValue(RigElementInDetailPanel);
				if (DebuggedControlRig && DebuggedControlRig != ControlRig)
				{
					DebuggedControlRig->SetTransientControlValue(RigElementInDetailPanel);
				}
			}
			else if (ScriptStruct == FRigControl::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Control)
			{
				if (PropertyChangedEvent.MemberProperty->GetName() == TEXT("GizmoColor"))
				{
					FRigControl& Control = Container->ControlHierarchy[RigElementInDetailPanel.Name];
					Control.GizmoColor.R = FMath::Clamp<float>(Control.GizmoColor.R, 0.f, 1.f);
					Control.GizmoColor.G = FMath::Clamp<float>(Control.GizmoColor.G, 0.f, 1.f);
					Control.GizmoColor.B = FMath::Clamp<float>(Control.GizmoColor.B, 0.f, 1.f);
					Control.GizmoColor.A = FMath::Clamp<float>(Control.GizmoColor.A, 0.f, 1.f);
				}

				if (!bSetupModeEnabled)
				{
					ControlRigBP->PropagatePropertyFromInstanceToBP(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty, DebuggedControlRig);
				}
				ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty);

				if (PropertyChangedEvent.MemberProperty->GetName().Contains(TEXT("Gizmo")))
				{
					ControlRigBP->HierarchyContainer.ControlHierarchy.OnControlUISettingsChanged.Broadcast(&ControlRigBP->HierarchyContainer, FRigElementKey(RigElementInDetailPanel.Name, ERigElementType::Control));
				}
			}
			else if (ScriptStruct == FRigCurve::StaticStruct() && RigElementInDetailPanel.Type == ERigElementType::Curve)
			{
				if (!bSetupModeEnabled)
				{
					ControlRigBP->PropagatePropertyFromInstanceToBP(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty, DebuggedControlRig);
				}
				ControlRigBP->PropagatePropertyFromBPToInstances(RigElementInDetailPanel, PropertyChangedEvent.MemberProperty);
			}

			ControlRigBP->Modify();
			ControlRigBP->MarkPackageDirty();
		}
	}
}

void FControlRigEditor::OnCreateComment()
{
	TSharedPtr<SGraphEditor> GraphEditor = FocusedGraphEdPtr.Pin();
	if (GraphEditor.IsValid())
	{
		if (UEdGraph* Graph = GraphEditor->GetCurrentGraph())
		{
			FEdGraphSchemaAction_K2AddComment CommentAction;
			CommentAction.PerformAction(Graph, NULL, GraphEditor->GetPasteLocation());
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

		FBlueprintEditorUtils::MarkBlueprintAsModified(GetControlRigBlueprint());
		TArray<FRigElementKey> SelectedElements = ControlRigBP->HierarchyContainer.CurrentSelection();
		for(const FRigElementKey& SelectedElement : SelectedElements)
		{
			ControlRigBP->HierarchyContainer.OnElementSelected.Broadcast(&ControlRigBP->HierarchyContainer, SelectedElement, true);
		}
		GetControlRigBlueprint()->RecompileVM();

		SynchronizeViewportBoneSelection();

		UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
		// since rig has changed, rebuild draw skeleton
		if (EditorSkelComp)
		{ 
			EditorSkelComp->RebuildDebugDrawSkeleton(); 
		}

		if (NodeDetailStruct != nullptr && NodeDetailBuffer.Num() > 0 && NodeDetailName != NAME_None)
		{
			if (URigVMNode* Node = ControlRigBP->Model->FindNode(NodeDetailName.ToString()))
			{
				SetDetailObject(Node);
			}
			else
			{
				ClearDetailObject();
			}
		}
		else if (ControlRigBP->HierarchyContainer.GetIndex(RigElementInDetailPanel) == INDEX_NONE)
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
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (RigBlueprint->bSuspendAllNotifications)
		{
			return;
		}
	}
	OnHierarchyChanged();
}

void FControlRigEditor::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bForce)
{
	UControlRigBlueprint* Blueprint = GetControlRigBlueprint();

	if (Blueprint->bSuspendAllNotifications && !bForce)
	{
		return;
	}

	UEnum* RigElementTypeEnum = StaticEnum<ERigElementType>();
	if (RigElementTypeEnum == nullptr)
	{
		return;
	}

	FString NoneStr = FName(NAME_None).ToString();
	FString RemovedElementName = InKey.Name.ToString();
	ERigElementType RemovedElementType = InKey.Type;

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}

		for (UEdGraphNode* Node : RigGraph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				if (URigVMNode* ModelNode = RigNode->GetModelNode())
				{
					TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
					for (URigVMPin* ModelPin : ModelPins)
					{
						if ((ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("BoneName") && RemovedElementType == ERigElementType::Bone) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("ControlName") && RemovedElementType == ERigElementType::Control) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("SpaceName") && RemovedElementType == ERigElementType::Space) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("CurveName") && RemovedElementType == ERigElementType::Curve))
						{
							if (ModelPin->GetDefaultValue() == RemovedElementName)
							{
								Blueprint->Controller->SetPinDefaultValue(ModelPin->GetPinPath(), FName(NAME_None).ToString());
							}
						}
						else if (ModelPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
						{

							if (URigVMPin* TypePin = ModelPin->FindSubPin(TEXT("Type")))
							{
								FString TypeStr = TypePin->GetDefaultValue();
								int64 TypeValue = RigElementTypeEnum->GetValueByNameString(TypeStr);
								if (TypeValue == (int64)RemovedElementType)
								{
									if (URigVMPin* NamePin = ModelPin->FindSubPin(TEXT("Name")))
									{
										FString NameStr = NamePin->GetDefaultValue();
										if (NameStr == RemovedElementName)
										{
											Blueprint->Controller->SetPinDefaultValue(NamePin->GetPinPath(), NoneStr);
										}
									}
								}
							}
						}
					}
				}
			}
		}

		CacheNameLists();
	}

	OnHierarchyChanged();
}

void FControlRigEditor::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = GetControlRigBlueprint();

	if (Blueprint->bSuspendAllNotifications)
	{
		return;
	}

	UEnum* RigElementTypeEnum = StaticEnum<ERigElementType>();
	if (RigElementTypeEnum == nullptr)
	{
		return;
	}

	FString OldNameStr = InOldName.ToString();
	FString NewNameStr = InNewName.ToString();

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}

		for (UEdGraphNode* Node : RigGraph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				if (URigVMNode* ModelNode = RigNode->GetModelNode())
				{
					TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
					for (URigVMPin * ModelPin : ModelPins)
					{
						if ((ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("BoneName") && ElementType == ERigElementType::Bone) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("ControlName") && ElementType == ERigElementType::Control) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("SpaceName") && ElementType == ERigElementType::Space) ||
							(ModelPin->GetCPPType() == TEXT("FName") && ModelPin->GetCustomWidgetName() == TEXT("CurveName") && ElementType == ERigElementType::Curve))
						{
							if (ModelPin->GetDefaultValue() == InOldName.ToString())
							{
								Blueprint->Controller->SetPinDefaultValue(ModelPin->GetPinPath(), InNewName.ToString(), false);
							}
						}
						else if (ModelPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
						{
							if (URigVMPin* TypePin = ModelPin->FindSubPin(TEXT("Type")))
							{
								FString TypeStr = TypePin->GetDefaultValue();
								int64 TypeValue = RigElementTypeEnum->GetValueByNameString(TypeStr);
								if (TypeValue == (int64)ElementType)
								{
									if (URigVMPin* NamePin = ModelPin->FindSubPin(TEXT("Name")))
									{
										FString NameStr = NamePin->GetDefaultValue();
										if (NameStr == OldNameStr)
										{
											Blueprint->Controller->SetPinDefaultValue(NamePin->GetPinPath(), NewNameStr);
										}
									}
								}
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
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprintObj()))
	{
		if (RigBlueprint->bSuspendAllNotifications)
		{
			return;
		}
	}

	OnHierarchyChanged();
}

void FControlRigEditor::SynchronizeViewportBoneSelection()
{
	UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint();
	if (RigBlueprint == nullptr)
	{
		return;
	}

	UControlRigSkeletalMeshComponent* EditorSkelComp = Cast<UControlRigSkeletalMeshComponent>(GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent());
	if (EditorSkelComp)
	{
		EditorSkelComp->BonesOfInterest.Reset();

		TArray<FName> SelectedBones = RigBlueprint->HierarchyContainer.BoneHierarchy.CurrentSelection();
		for (const FName& Bone : SelectedBones)
		{
			int32 Index = ControlRig->Hierarchy.BoneHierarchy.GetIndex(Bone); 
			EditorSkelComp->BonesOfInterest.AddUnique(Index);
		}
	}
}

void FControlRigEditor::OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected)
{
	UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint();
	if (RigBlueprint == nullptr)
	{
		return;
	}

	if (RigBlueprint->bSuspendAllNotifications)
	{
		return;
	}

	if (Container->GetIndex(InKey) == INDEX_NONE)
	{
		return;
	}

	if (InKey.Type == ERigElementType::Bone)
	{
		SynchronizeViewportBoneSelection();
	}

	if (bSelected)
	{
		SetDetailStruct(InKey);
	}
	else
	{
		TArray<FRigElementKey> CurrentSelection = RigBlueprint->HierarchyContainer.CurrentSelection();
		if (CurrentSelection.Num() > 0)
		{
			OnRigElementSelected(Container, CurrentSelection.Last(), true);
		}
		else
		{
			ClearDetailObject();
		}
	}
}

void FControlRigEditor::OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
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
		UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
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
			
			if ((DraggedTypes & (uint8)ERigElementType::Control) != 0)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetControl", "Get Control"),
					LOCTEXT("CreateGetControlTooltip", "Getter for control\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetControl", "Set Control"),
					LOCTEXT("CreateSetControlTooltip", "Setter for control\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
			}

			if (((DraggedTypes & (uint8)ERigElementType::Bone) != 0) ||
				((DraggedTypes & (uint8)ERigElementType::Space) != 0))
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetTransform", "Get Transform"),
					LOCTEXT("CreateGetTransformTooltip", "Getter for transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetTransform", "Set Transform"),
					LOCTEXT("CreateSetTransformTooltip", "Setter for transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Transform, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
			}

			if (((DraggedTypes & (uint8)ERigElementType::Bone) != 0) ||
				((DraggedTypes & (uint8)ERigElementType::Control) != 0) ||
				((DraggedTypes & (uint8)ERigElementType::Space) != 0))
			{
				MenuBuilder.AddMenuSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetRotation", "Set Rotation"),
					LOCTEXT("CreateSetRotationTooltip", "Setter for Rotation\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Rotation, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetTranslation", "Set Translation"),
					LOCTEXT("CreateSetTranslationTooltip", "Setter for translation\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Translation, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetOffset", "Add Offset"),
					LOCTEXT("CreateSetOffsetTooltip", "Setter for offset\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Offset, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);

				MenuBuilder.AddMenuSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateGetRelativeTransform", "Get Relative Transform"),
					LOCTEXT("CreateGetRelativeTransformTooltip", "Getter for relative transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Relative, true, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateSetRelativeTransform", "Set Relative Transform"),
					LOCTEXT("CreateSetRelativeTransformTooltip", "Setter for relative transform\n"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigEditor::HandleMakeElementGetterSetter, ERigElementGetterSetterType_Relative, false, DraggedKeys, Graph, NodePosition),
						FCanExecuteAction()
					)
				);

			}

			if (DraggedKeys.Num() > 0 && Blueprint != nullptr)
			{
				MenuBuilder.AddMenuSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateCollectionFromKeys", "Create Collection"),
					LOCTEXT("CreateCollectionFromKeysTooltip", "Creates a collection from the selected elements in the hierarchy"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([Blueprint, DraggedKeys, NodePosition]()
							{
								if (URigVMController* Controller = Blueprint->Controller)
								{
									Controller->OpenUndoBracket(TEXT("Create Collection from Items"));

									if (URigVMNode* ItemsNode = Controller->AddStructNode(FRigUnit_CollectionItems::StaticStruct(), TEXT("Execute"), NodePosition))
									{
										if (URigVMPin* ItemsPin = ItemsNode->FindPin(TEXT("Items")))
										{
											Controller->SetArrayPinSize(ItemsPin->GetPinPath(), DraggedKeys.Num());

											TArray<URigVMPin*> ItemPins = ItemsPin->GetSubPins();
											ensure(ItemPins.Num() == DraggedKeys.Num());

											for (int32 ItemIndex = 0; ItemIndex < DraggedKeys.Num(); ItemIndex++)
											{
												FString DefaultValue;
												FRigElementKey::StaticStruct()->ExportText(DefaultValue, &DraggedKeys[ItemIndex], nullptr, nullptr, PPF_None, nullptr);
												Controller->SetPinDefaultValue(ItemPins[ItemIndex]->GetPinPath(), DefaultValue);
												Controller->SetPinExpansion(ItemPins[ItemIndex]->GetPinPath(), true);
											}
										}
									}

									Controller->CloseUndoBracket();
								}
							}
						)
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
	if (Blueprint->Controller == nullptr)
	{
		return;
	}

	Blueprint->Controller->OpenUndoBracket(TEXT("Adding Nodes from Hierarchy"));

	struct FNewNodeData
	{
		FName Name;
		FName ValuePinName;
		ERigControlType ValueType;
		FRigControlValue Value;
	};
	TArray<FNewNodeData> NewNodes;

	for (const FRigElementKey& Key : Keys)
	{
		UScriptStruct* StructTemplate = nullptr;

		FNewNodeData NewNode;
		NewNode.Name = NAME_None;
		NewNode.ValuePinName = NAME_None;

		TArray<FName> ItemPins;
		ItemPins.Add(TEXT("Item"));

		TArray<FName> NamePins;

		if (bIsGetter)
		{
			switch (Type)
			{
				case ERigElementGetterSetterType_Transform:
				{
					if (Key.Type == ERigElementType::Control)
					{
						const FRigControl& Control = Blueprint->HierarchyContainer.ControlHierarchy[Key.Name];
						switch (Control.ControlType)
						{
							case ERigControlType::Bool:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlBool::StaticStruct();
								break;
							}
							case ERigControlType::Float:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlFloat::StaticStruct();
								break;
							}
							case ERigControlType::Integer:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlInteger::StaticStruct();
								break;
							}
							case ERigControlType::Vector2D:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlVector2D::StaticStruct();
								break;
							}
							case ERigControlType::Position:
							case ERigControlType::Scale:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlVector::StaticStruct();
								break;
							}
							case ERigControlType::Rotator:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_GetControlRotator::StaticStruct();
								break;
							}
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							case ERigControlType::EulerTransform:
							{
								StructTemplate = FRigUnit_GetTransform::StaticStruct();
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
						StructTemplate = FRigUnit_GetTransform::StaticStruct();
					}
					break;
				}
				case ERigElementGetterSetterType_Initial:
				{
					StructTemplate = FRigUnit_GetTransform::StaticStruct();
					break;
				}
				case ERigElementGetterSetterType_Relative:
				{
					StructTemplate = FRigUnit_GetRelativeTransformForItem::StaticStruct();
					ItemPins.Reset();
					ItemPins.Add(TEXT("Child"));
					ItemPins.Add(TEXT("Parent"));
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
					if (Key.Type == ERigElementType::Control)
					{
						const FRigControl& Control = Blueprint->HierarchyContainer.ControlHierarchy[Key.Name];
						switch (Control.ControlType)
						{
							case ERigControlType::Bool:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlBool::StaticStruct();
								break;
							}
							case ERigControlType::Float:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlFloat::StaticStruct();
								break;
							}
							case ERigControlType::Integer:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlInteger::StaticStruct();
								break;
							}
							case ERigControlType::Vector2D:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlVector2D::StaticStruct();
								break;
							}
							case ERigControlType::Position:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlVector::StaticStruct();
								NewNode.ValuePinName = TEXT("Vector");
								NewNode.ValueType = ERigControlType::Position;
								NewNode.Value = FRigControlValue::Make<FVector>(Blueprint->HierarchyContainer.GetGlobalTransform(Key).GetLocation());
								break;
							}
							case ERigControlType::Scale:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlVector::StaticStruct();
								NewNode.ValuePinName = TEXT("Vector");
								NewNode.ValueType = ERigControlType::Scale;
								NewNode.Value = FRigControlValue::Make<FVector>(Blueprint->HierarchyContainer.GetGlobalTransform(Key).GetScale3D());
								break;
							}
							case ERigControlType::Rotator:
							{
								NamePins.Add(TEXT("Control"));
								StructTemplate = FRigUnit_SetControlRotator::StaticStruct();
								NewNode.ValuePinName = TEXT("Rotator");
								NewNode.ValueType = ERigControlType::Rotator;
								NewNode.Value = FRigControlValue::Make<FRotator>(Blueprint->HierarchyContainer.GetGlobalTransform(Key).Rotator());
								break;
							}
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							case ERigControlType::EulerTransform:
							{
								StructTemplate = FRigUnit_SetTransform::StaticStruct();
								NewNode.ValuePinName = TEXT("Transform");
								NewNode.ValueType = ERigControlType::Transform;
								NewNode.Value = FRigControlValue::Make<FTransform>(Blueprint->HierarchyContainer.GetGlobalTransform(Key));
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
						StructTemplate = FRigUnit_SetTransform::StaticStruct();
						NewNode.ValuePinName = TEXT("Transform");
						NewNode.ValueType = ERigControlType::Transform;
						NewNode.Value = FRigControlValue::Make<FTransform>(Blueprint->HierarchyContainer.GetGlobalTransform(Key));
					}
					break;
				}
				case ERigElementGetterSetterType_Relative:
				{
					StructTemplate = FRigUnit_SetRelativeTransformForItem::StaticStruct();
					ItemPins.Reset();
					ItemPins.Add(TEXT("Child"));
					ItemPins.Add(TEXT("Parent"));
					break;
				}
				case ERigElementGetterSetterType_Rotation:
				{
					StructTemplate = FRigUnit_SetRotation::StaticStruct();
					NewNode.ValuePinName = TEXT("Rotation");
					NewNode.ValueType = ERigControlType::Rotator;
					NewNode.Value = FRigControlValue::Make<FRotator>(Blueprint->HierarchyContainer.GetGlobalTransform(Key).Rotator());
					break;
				}
				case ERigElementGetterSetterType_Translation:
				{
					StructTemplate = FRigUnit_SetTranslation::StaticStruct();
					NewNode.ValuePinName = TEXT("Translation");
					NewNode.ValueType = ERigControlType::Position;
					NewNode.Value = FRigControlValue::Make<FVector>(Blueprint->HierarchyContainer.GetGlobalTransform(Key).GetLocation());
					break;
				}
				case ERigElementGetterSetterType_Offset:
				{
					StructTemplate = FRigUnit_OffsetTransformForItem::StaticStruct();
					break;
				}
				default:
				{
					break;
				}
			}
		}

		if (StructTemplate == nullptr)
		{
			return;
		}

		FVector2D NodePositionIncrement(0.f, 120.f);
		if (!bIsGetter)
		{
			NodePositionIncrement = FVector2D(380.f, 0.f);
		}

		FName Name = FControlRigBlueprintUtils::ValidateName(Blueprint, StructTemplate->GetName());
		if (URigVMStructNode* ModelNode = Blueprint->Controller->AddStructNode(StructTemplate, TEXT("Execute"), NodePosition))
		{
			FString ItemTypeStr = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Key.Type).ToString();
			NewNode.Name = ModelNode->GetFName();
			NewNodes.Add(NewNode);
			
			for (const FName& ItemPin : ItemPins)
			{
				Blueprint->Controller->SetPinDefaultValue(FString::Printf(TEXT("%s.%s.Name"), *ModelNode->GetName(), *ItemPin.ToString()), Key.Name.ToString());
				Blueprint->Controller->SetPinDefaultValue(FString::Printf(TEXT("%s.%s.Type"), *ModelNode->GetName(), *ItemPin.ToString()), ItemTypeStr);			
			}

			for (const FName& NamePin : NamePins)
			{
				Blueprint->Controller->SetPinDefaultValue(FString::Printf(TEXT("%s.%s"), *ModelNode->GetName(), *NamePin.ToString()), Key.Name.ToString());
			}

			if (!NewNode.ValuePinName.IsNone())
			{
				FString DefaultValue;

				switch (NewNode.ValueType)
				{
					case ERigControlType::Position:
					case ERigControlType::Scale:
					{
						DefaultValue = NewNode.Value.ToString<FVector>();
						break;
					}
					case ERigControlType::Rotator:
					{
						DefaultValue = NewNode.Value.ToString<FRotator>();
						break;
					}
					case ERigControlType::Transform:
					{
						DefaultValue = NewNode.Value.ToString<FTransform>();
						break;
					}
					default:
					{
						break;
					}
				}
				if (!DefaultValue.IsEmpty())
				{
					Blueprint->Controller->SetPinDefaultValue(FString::Printf(TEXT("%s.%s"), *ModelNode->GetName(), *NewNode.ValuePinName.ToString()), DefaultValue);
				}
			}

			UControlRigUnitNodeSpawner::HookupMutableNode(ModelNode, Blueprint);
		}

		NodePosition += NodePositionIncrement;
	}

	if (NewNodes.Num() > 0)
	{
		TArray<FName> NewNodeNames;
		for (const FNewNodeData& NewNode : NewNodes)
		{
			NewNodeNames.Add(NewNode.Name);
		}
		Blueprint->Controller->SetNodeSelection(NewNodeNames);
		Blueprint->Controller->CloseUndoBracket();
	}
	else
	{
		Blueprint->Controller->CancelUndoBracket();
	}
}

void FControlRigEditor::HandleOnControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context)
{
	if (Subject != ControlRig)
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (Blueprint == nullptr)
	{
		return;
	}

	if (Control.bIsTransientControl)
	{
		if (URigVMPin* Pin = Blueprint->Model->FindPin(Control.Name.ToString()))
		{
			FString NewDefaultValue;
			switch (Control.ControlType)
			{
				case ERigControlType::Position:
				case ERigControlType::Scale:
				{
					NewDefaultValue = Control.Value.ToString<FVector>();
					break;
				}
				case ERigControlType::Rotator:
				{
					FRotator Rotator = Control.Value.Get<FRotator>();
					FRigControlValue QuatValue = FRigControlValue::Make<FQuat>(FQuat(Rotator));
					NewDefaultValue = QuatValue.ToString<FQuat>();
					break;
				}
				case ERigControlType::Transform:
				{
					NewDefaultValue = Control.Value.ToString<FTransform>();
					break;
				}
				case ERigControlType::TransformNoScale:
				{
					NewDefaultValue = Control.Value.ToString<FTransformNoScale>();
					break;
				}
				case ERigControlType::EulerTransform:
				{
					NewDefaultValue = Control.Value.ToString<FEulerTransform>();
					break;
				}
				default:
				{
					break;
				}
			}

			if (!NewDefaultValue.IsEmpty())
			{
				Blueprint->Controller->SetPinDefaultValue(Pin->GetPinPath(), NewDefaultValue, true, true, true);
			}
		}
		else
		{
			static FString ControlRigForElementBoneName;
			static FString ControlRigForElementSpaceName;

			if (ControlRigForElementBoneName.IsEmpty())
			{
				ControlRigForElementBoneName = FString::Printf(TEXT("ControlForRigElement_%s_"),
					*StaticEnum<ERigElementType>()->GetNameByValue((int64)ERigElementType::Bone).ToString());
				ControlRigForElementSpaceName = FString::Printf(TEXT("ControlForRigElement_%s_"),
					*StaticEnum<ERigElementType>()->GetNameByValue((int64)ERigElementType::Space).ToString());
			}

			if (Control.Name.ToString().StartsWith(ControlRigForElementBoneName))
			{
				FName BoneName = *Control.Name.ToString().RightChop(ControlRigForElementBoneName.Len());

				FTransform Transform = Control.Value.Get<FTransform>() * Control.OffsetTransform;
				Blueprint->HierarchyContainer.BoneHierarchy.SetLocalTransform(BoneName, Transform);

				if (bSetupModeEnabled)
				{
					FTransform InitialGlobalTransform = Blueprint->HierarchyContainer.BoneHierarchy.GetGlobalTransform(BoneName);
					Blueprint->HierarchyContainer.BoneHierarchy.SetInitialGlobalTransform(BoneName, InitialGlobalTransform);
				}

				Blueprint->PropagateHierarchyFromBPToInstances(false, false);

				if (PreviewInstance)
				{
					if (FAnimNode_ModifyBone* Modify = PreviewInstance->FindModifiedBone(BoneName))
					{
						Modify->Translation = Transform.GetTranslation();
						Modify->Rotation = Transform.GetRotation().Rotator();
						Modify->TranslationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
						Modify->RotationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
					}
				}
			}
			else if (Control.Name.ToString().StartsWith(ControlRigForElementSpaceName))
			{
				FName SpaceName = *Control.Name.ToString().RightChop(ControlRigForElementSpaceName.Len());

				FTransform GlobalTransform = ControlRig->GetControlGlobalTransform(Control.Name);
				Blueprint->HierarchyContainer.SpaceHierarchy.SetGlobalTransform(SpaceName, GlobalTransform);
				Blueprint->HierarchyContainer.SpaceHierarchy.SetInitialGlobalTransform(SpaceName, GlobalTransform);
				Blueprint->PropagateHierarchyFromBPToInstances(false, false);
			}
		}
	}
	else if (bSetupModeEnabled)
	{
		FRigControlHierarchy& ControlHierarchy = ControlRig->GetControlHierarchy();
		Blueprint->HierarchyContainer.ControlHierarchy[Control.Index] = ControlHierarchy[Control.Index];
	}
}

void FControlRigEditor::HandleRefreshEditorFromBlueprint(UControlRigBlueprint* InBlueprint)
{
	OnHierarchyChanged();
	Compile();
}

void FControlRigEditor::HandleVariableDroppedFromBlueprint(UObject* InSubject, FProperty* InVariableToDrop, const FVector2D& InDropPosition, const FVector2D& InScreenPosition)
{
	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetBlueprintObj());
	if (Blueprint == nullptr)
	{
		return;
	}

	URigVMController* Controller = Blueprint->Controller;
	check(Controller);

	FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(InVariableToDrop, nullptr);
	if (!ExternalVariable.IsValid(true /* allow null ptr */))
	{
		return;
	}

	FMenuBuilder MenuBuilder(true, NULL);
	const FText SectionText = FText::FromString(FString::Printf(TEXT("Variable %s"), *ExternalVariable.Name.ToString()));

	MenuBuilder.BeginSection("VariableDropped", SectionText);

	MenuBuilder.AddMenuEntry(
		FText::FromString(FString::Printf(TEXT("Get %s"), *ExternalVariable.Name.ToString())),
		FText::FromString(FString::Printf(TEXT("Adds a getter node for variable %s"), *ExternalVariable.Name.ToString())),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([ExternalVariable, Controller, InDropPosition] {

				Controller->AddVariableNode(ExternalVariable.Name, ExternalVariable.TypeName.ToString(), ExternalVariable.TypeObject, true, FString(), InDropPosition);

			}),
			FCanExecuteAction()
		)
	);

	MenuBuilder.AddMenuEntry(
		FText::FromString(FString::Printf(TEXT("Set %s"), *ExternalVariable.Name.ToString())),
		FText::FromString(FString::Printf(TEXT("Adds a setter node for variable %s"), *ExternalVariable.Name.ToString())),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([ExternalVariable, Controller, InDropPosition] {

				Controller->AddVariableNode(ExternalVariable.Name, ExternalVariable.TypeName.ToString(), ExternalVariable.TypeObject, false, FString(), InDropPosition);

			}),
			FCanExecuteAction()
		)
	);

	MenuBuilder.EndSection();

	TSharedRef<SWidget> GraphEditorPanel = FocusedGraphEdPtr.Pin().ToSharedRef();

	// Show dialog to choose getter vs setter
	FSlateApplication::Get().PushMenu(
		GraphEditorPanel,
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		InScreenPosition,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
	);
}

void FControlRigEditor::OnGraphNodeClicked(UControlRigGraphNode* InNode)
{
	if (InNode)
	{
		if (InNode->IsSelectedInEditor())
		{
			SetDetailObject(InNode->GetModelNode());
		}
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

		if (ControlRigLog.Entries.Num() == 0 && !bAnyErrorsLeft)
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
			TMap<int32, UEdGraphNode*> InstructionIndexToNode;
			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
				{
					bFoundError = bFoundError || ControlRigGraphNode->ErrorType <= (int32)EMessageSeverity::Error;
					bFoundWarning = bFoundWarning || ControlRigGraphNode->ErrorType <= (int32)EMessageSeverity::Warning;
					if (URigVMNode* ModelNode = ControlRigGraphNode->GetModelNode())
					{
						if (ModelNode->GetInstructionIndex() != INDEX_NONE)
						{
							InstructionIndexToNode.Add(ModelNode->GetInstructionIndex(), GraphNode);
						}
					}
				}
				GraphNode->ErrorType = int32(EMessageSeverity::Info) + 1;
			}

			// update the nodes' error messages
			bool bFoundErrorOrWarningInLog = false;
			for (const FControlRigLog::FLogEntry& Entry : ControlRigLog.Entries)
			{
				UEdGraphNode** GraphNodePtr = InstructionIndexToNode.Find(Entry.InstructionIndex);
				if (GraphNodePtr == nullptr)
				{
					continue;
				}
				UEdGraphNode* GraphNode = *GraphNodePtr;

				bFoundError = bFoundError || Entry.Severity <= EMessageSeverity::Error;
				bFoundWarning = bFoundWarning || Entry.Severity <= EMessageSeverity::Warning;
				bFoundErrorOrWarningInLog = bFoundErrorOrWarningInLog || Entry.Severity <= EMessageSeverity::Warning;

				int32 ErrorType = (int32)Entry.Severity;
				if (GraphNode->ErrorType < ErrorType)
				{
					continue;
				}
				else if (GraphNode->ErrorType == ErrorType)
				{
					GraphNode->ErrorMsg = FString::Printf(TEXT("%s\n%s"), *GraphNode->ErrorMsg, *Entry.Message);
				}
				else
				{
					GraphNode->ErrorMsg = Entry.Message;
					GraphNode->ErrorType = ErrorType;
				}
			}
			bAnyErrorsLeft = bFoundErrorOrWarningInLog;

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				GraphNode->bHasCompilerMessage = GraphNode->ErrorType <= int32(EMessageSeverity::Info);
			}

			if (bFoundError)
			{
				Blueprint->Status = BS_Error;
				Blueprint->MarkPackageDirty();

				/*
				if (bFoundErrorOrWarningInLog)
				{
					FNotificationInfo Info(LOCTEXT("ControlRigBlueprintCompilerUnitErrorMessage", "There has been a compiler error.\nCheck the Execution Stack view."));
					Info.bUseSuccessFailIcons = true;
					Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Error"));
					Info.bFireAndForget = true;
					Info.FadeOutDuration = 5.0f;
					Info.ExpireDuration = 0.0f;
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
					NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
				}
				*/
			}
			else if (bFoundWarning)
			{
				/*
				if (bFoundErrorOrWarningInLog)
				{
					FNotificationInfo Info(LOCTEXT("ControlRigBlueprintCompilerUnitWarningMessage", "There has been a compiler warning.\nCheck the Execution Stack view."));
					Info.bUseSuccessFailIcons = true;
					Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
					Info.bFireAndForget = true;
					Info.FadeOutDuration = 5.0f;
					Info.ExpireDuration = 0.0f;
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
					NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
				}
				*/
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
