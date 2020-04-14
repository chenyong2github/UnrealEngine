// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorModule.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintEditorModule.h"
#include "BlueprintNodeSpawner.h"
#include "PropertyEditorModule.h"
#include "ControlRig.h"
#include "ControlRigComponent.h"
#include "ControlRigConnectionDrawingPolicy.h"
#include "ControlRigVariableDetailsCustomization.h"
#include "GraphEditorActions.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ISequencerModule.h"
#include "IAssetTools.h"
#include "ControlRigEditorStyle.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditor.h"
#include "MovieSceneToolsProjectSettings.h"
#include "Styling/SlateStyle.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "EditorModeRegistry.h"
#include "ControlRigEditMode.h"
#include "ILevelSequenceModule.h"
#include "EditorModeManager.h"
#include "ControlRigEditMode.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "MovieSceneControlRigSectionDetailsCustomization.h"
#include "ControlRigEditModeCommands.h"
#include "Materials/Material.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "ControlRigEditModeSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRigSequenceExporter.h"
#include "ControlRigSequenceExporterSettingsDetailsCustomization.h"
#include "ControlRigSequenceExporterSettings.h"
#include "ContentBrowserModule.h"
#include "AssetRegistryModule.h"
#include "Editor/ControlRigEditor.h"
#include "ControlRigBlueprintActions.h"
#include "ControlRigGizmoLibraryActions.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/NodeSpawners/ControlRigUnitNodeSpawner.h"
#include "Graph/NodeSpawners/ControlRigVariableNodeSpawner.h"
#include "Graph/NodeSpawners/ControlRigParameterNodeSpawner.h"
#include "Graph/NodeSpawners/ControlRigRerouteNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Graph/ControlRigGraphNode.h"
#include "EdGraphUtilities.h"
#include "ControlRigGraphPanelNodeFactory.h"
#include "ControlRigGraphPanelPinFactory.h"
#include "ControlRigBlueprintUtils.h"
#include "ControlRigBlueprintCommands.h"
#include "ControlRigHierarchyCommands.h"
#include "ControlRigStackCommands.h"
#include "Animation/AnimSequence.h"
#include "ControlRigEditorEditMode.h"
#include "ControlRigElementDetails.h"
#include "ControlRigCompilerDetails.h"
#include "ControlRigDrawingDetails.h"
#include "Animation/AnimSequence.h"
#include "Editor/SControlRigProfilingView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ControlRigParameterTrackEditor.h"
#include "ActorFactories/ActorFactorySkeletalMesh.h"
#include "ControlRigThumbnailRenderer.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprint.h"
#include "ControlRig/Private/Units/Simulation/RigUnit_AlphaInterp.h"
#include "ControlRig/Private/Units/Debug/RigUnit_VisualDebug.h"

#define LOCTEXT_NAMESPACE "ControlRigEditorModule"

DEFINE_LOG_CATEGORY(LogControlRigEditor);

TSharedRef<SDockTab> SpawnRigProfiler( const FSpawnTabArgs& Args )
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SControlRigProfilingView)
		];
}

void FControlRigEditorModule::StartupModule()
{
	FControlRigEditModeCommands::Register();
	FControlRigBlueprintCommands::Register();
	FControlRigHierarchyCommands::Register();
	FControlRigStackCommands::Register();
	FControlRigEditorStyle::Get();

	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	// Register Blueprint editor variable customization
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	BlueprintEditorModule.RegisterVariableCustomization(FProperty::StaticClass(), FOnGetVariableCustomizationInstance::CreateStatic(&FControlRigVariableDetailsCustomization::MakeInstance));

	// Register to fixup newly created BPs
	FKismetEditorUtilities::RegisterOnBlueprintCreatedCallback(this, UControlRig::StaticClass(), FKismetEditorUtilities::FOnBlueprintCreated::CreateRaw(this, &FControlRigEditorModule::HandleNewBlueprintCreated));

	// Register details customizations for animation controller nodes
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	ClassesToUnregisterOnShutdown.Reset();

	ClassesToUnregisterOnShutdown.Add(UMovieSceneControlRigParameterSection::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneControlRigSectionDetailsCustomization::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(UControlRigSequenceExporterSettings::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigSequenceExporterSettingsDetailsCustomization::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(FRigBone::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigBoneDetails::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(FRigControl::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigControlDetails::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(FRigSpace::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigSpaceDetails::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(UControlRig::StaticClass()->GetFName());

	// same as ClassesToUnregisterOnShutdown but for properties, there is none right now
	PropertiesToUnregisterOnShutdown.Reset();

	PropertiesToUnregisterOnShutdown.Add(FRigVMCompileSettings::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigVMCompileSettingsDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FControlRigDrawContainer::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FControlRigDrawContainerDetails::MakeInstance));

	// Register asset tools
	auto RegisterAssetTypeAction = [this](const TSharedRef<IAssetTypeActions>& InAssetTypeAction)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		RegisteredAssetTypeActions.Add(InAssetTypeAction);
		AssetTools.RegisterAssetTypeActions(InAssetTypeAction);
	};

	RegisterAssetTypeAction(MakeShareable(new FControlRigBlueprintActions()));
	RegisterAssetTypeAction(MakeShareable(new FControlRigGizmoLibraryActions()));
	
	// Register sequencer track editor
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	ControlRigParameterTrackCreateEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FControlRigParameterTrackEditor::CreateTrackEditor));

	FEditorModeRegistry::Get().RegisterMode<FControlRigEditMode>(
		FControlRigEditMode::ModeName,
		NSLOCTEXT("AnimationModeToolkit", "DisplayName", "Animation"),
		FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRigEditMode", "ControlRigEditMode.Small"),
		true);

	FEditorModeRegistry::Get().RegisterMode<FControlRigEditorEditMode>(
		FControlRigEditorEditMode::ModeName,
		NSLOCTEXT("RiggingModeToolkit", "DisplayName", "Rigging"),
		FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRigEditMode", "ControlRigEditMode.Small"),
		false);


	ControlRigGraphPanelNodeFactory = MakeShared<FControlRigGraphPanelNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(ControlRigGraphPanelNodeFactory);

	ControlRigGraphPanelPinFactory = MakeShared<FControlRigGraphPanelPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(ControlRigGraphPanelPinFactory);

	ReconstructAllNodesDelegateHandle = FBlueprintEditorUtils::OnReconstructAllNodesEvent.AddStatic(&FControlRigBlueprintUtils::HandleReconstructAllNodes);
	RefreshAllNodesDelegateHandle = FBlueprintEditorUtils::OnRefreshAllNodesEvent.AddStatic(&FControlRigBlueprintUtils::HandleRefreshAllNodes);

#if WITH_EDITOR
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner("HierarchicalProfiler", FOnSpawnTab::CreateStatic(&SpawnRigProfiler))
			.SetDisplayName(NSLOCTEXT("UnrealEditor", "HierarchicalProfilerTab", "Hierarchical Profiler"))
			.SetTooltipText(NSLOCTEXT("UnrealEditor", "HierarchicalProfilerTooltip", "Open the Hierarchical Profiler tab."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
			.SetIcon(FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.RigUnit")));
	};
#endif

	FControlRigBlueprintActions::ExtendSketalMeshToolMenu();
	UActorFactorySkeletalMesh::RegisterDelegatesForAssetClass(
		UControlRigBlueprint::StaticClass(),
		FGetSkeletalMeshFromAssetDelegate::CreateStatic(&FControlRigBlueprintActions::GetSkeletalMeshFromControlRigBlueprint),
		FPostSkeletalMeshActorSpawnedDelegate::CreateStatic(&FControlRigBlueprintActions::PostSpawningSkeletalMeshActor)
	);

	UThumbnailManager::Get().RegisterCustomRenderer(UControlRigBlueprint::StaticClass(), UControlRigThumbnailRenderer::StaticClass());
}

void FControlRigEditorModule::ShutdownModule()
{
#if WITH_EDITOR
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("ControlRigProfiler");
	}
#endif

	//UThumbnailManager::Get().UnregisterCustomRenderer(UControlRigBlueprint::StaticClass());
	//UActorFactorySkeletalMesh::UnregisterDelegatesForAssetClass(UControlRigBlueprint::StaticClass());

	FBlueprintEditorUtils::OnRefreshAllNodesEvent.Remove(RefreshAllNodesDelegateHandle);
	FBlueprintEditorUtils::OnReconstructAllNodesEvent.Remove(ReconstructAllNodesDelegateHandle);

	FEdGraphUtilities::UnregisterVisualPinFactory(ControlRigGraphPanelPinFactory);
	FEdGraphUtilities::UnregisterVisualNodeFactory(ControlRigGraphPanelNodeFactory);

	FEditorModeRegistry::Get().UnregisterMode(FControlRigEditorEditMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FControlRigEditMode::ModeName);


	ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnRegisterTrackEditor(ControlRigParameterTrackCreateEditorHandle);
	}

	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		for (TSharedRef<IAssetTypeActions> RegisteredAssetTypeAction : RegisteredAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(RegisteredAssetTypeAction);
		}
	}

	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	FBlueprintEditorModule* BlueprintEditorModule = FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet");
	if (BlueprintEditorModule)
	{
		BlueprintEditorModule->UnregisterVariableCustomization(FProperty::StaticClass());
	}

	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		for (int32 Index = 0; Index < ClassesToUnregisterOnShutdown.Num(); ++Index)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ClassesToUnregisterOnShutdown[Index]);
		}

		for (int32 Index = 0; Index < PropertiesToUnregisterOnShutdown.Num(); ++Index)
		{
			PropertyEditorModule->UnregisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown[Index]);
		}
	}
}

void FControlRigEditorModule::HandleNewBlueprintCreated(UBlueprint* InBlueprint)
{
	// add an initial graph for us to work in
	const UControlRigGraphSchema* ControlRigGraphSchema = GetDefault<UControlRigGraphSchema>();

	UEdGraph* ControlRigGraph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, ControlRigGraphSchema->GraphName_ControlRig, UControlRigGraph::StaticClass(), UControlRigGraphSchema::StaticClass());
	ControlRigGraph->bAllowDeletion = false;
	FBlueprintEditorUtils::AddUbergraphPage(InBlueprint, ControlRigGraph);
	InBlueprint->LastEditedDocuments.AddUnique(ControlRigGraph);
	InBlueprint->PostLoad();
}


TSharedRef<IControlRigEditor> FControlRigEditorModule::CreateControlRigEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, class UControlRigBlueprint* InBlueprint)
{
	TSharedRef< FControlRigEditor > NewControlRigEditor(new FControlRigEditor());
	NewControlRigEditor->InitControlRigEditor(Mode, InitToolkitHost, InBlueprint);
	return NewControlRigEditor;
}

void FControlRigEditorModule::GetTypeActions(const UControlRigBlueprint* CRB, FBlueprintActionDatabaseRegistrar& ActionRegistrar)
{
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the class (so if the class 
	// type disappears, then the action should go with it)
	UClass* ActionKey = CRB->GetClass();
	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first   
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the 
	// registrar would only accept actions corresponding to that asset)
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	// Add all rig units
	FControlRigBlueprintUtils::ForAllRigUnits([&](UScriptStruct* InStruct)
	{
		FString CategoryMetadata, DisplayNameMetadata, MenuDescSuffixMetadata;
		InStruct->GetStringMetaDataHierarchical(UControlRig::CategoryMetaName, &CategoryMetadata);
		InStruct->GetStringMetaDataHierarchical(UControlRig::DisplayNameMetaName, &DisplayNameMetadata);
		InStruct->GetStringMetaDataHierarchical(UControlRig::MenuDescSuffixMetaName, &MenuDescSuffixMetadata);
		if (!MenuDescSuffixMetadata.IsEmpty())
		{
			MenuDescSuffixMetadata = TEXT(" ") + MenuDescSuffixMetadata;
		}
		FText NodeCategory = FText::FromString(CategoryMetadata);
		FText MenuDesc = FText::FromString(DisplayNameMetadata + MenuDescSuffixMetadata);
		FText ToolTip = InStruct->GetToolTipText();

		UBlueprintNodeSpawner* NodeSpawner = UControlRigUnitNodeSpawner::CreateFromStruct(InStruct, MenuDesc, NodeCategory, ToolTip);
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	});

	// Add 'new properties'
	TArray<FEdGraphPinType> PinTypes;
	GetDefault<UControlRigGraphSchema>()->GetVariablePinTypes(PinTypes);

	struct Local
	{
		static void AddVariableActions(UClass* InActionKey, FBlueprintActionDatabaseRegistrar& InActionRegistrar, const FEdGraphPinType& PinType, const FString& InCategory)
		{
			static const FString CategoryDelimiter(TEXT("|"));

			FText NodeCategory = FText::FromString(InCategory);
			FText MenuDesc;
			FText ToolTip;
			if(PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
				{
					MenuDesc = FText::FromString(Struct->GetName());
					ToolTip = MenuDesc;
				}

			}
			else
			{
				MenuDesc = UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, true);
				ToolTip = UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, false);
			}


			UBlueprintNodeSpawner* NodeSpawnerGetter = UControlRigVariableNodeSpawner::CreateFromPinType(PinType, true, MenuDesc, NodeCategory, ToolTip);
			check(NodeSpawnerGetter != nullptr);
			InActionRegistrar.AddBlueprintAction(InActionKey, NodeSpawnerGetter);

			UBlueprintNodeSpawner* NodeSpawnerSetter = UControlRigVariableNodeSpawner::CreateFromPinType(PinType, false, MenuDesc, NodeCategory, ToolTip);
			check(NodeSpawnerSetter != nullptr);
			InActionRegistrar.AddBlueprintAction(InActionKey, NodeSpawnerSetter);
		}

		static void AddParameterActions(UClass* InActionKey, FBlueprintActionDatabaseRegistrar& InActionRegistrar, const FEdGraphPinType& PinType, const FString& InCategory)
		{
			static const FString CategoryDelimiter(TEXT("|"));

			FText NodeCategory = FText::FromString(InCategory);
			FText MenuDesc;
			FText ToolTip;
			if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
				{
					MenuDesc = FText::FromString(Struct->GetName());
					ToolTip = MenuDesc;
				}
			}
			else
			{
				MenuDesc = UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, true);
				ToolTip = UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, false);
			}


			UBlueprintNodeSpawner* NodeSpawnerGetter = UControlRigParameterNodeSpawner::CreateFromPinType(PinType, true, MenuDesc, NodeCategory, ToolTip);
			check(NodeSpawnerGetter != nullptr);
			InActionRegistrar.AddBlueprintAction(InActionKey, NodeSpawnerGetter);

			// let's disable setters for now
			//UBlueprintNodeSpawner* NodeSpawnerSetter = UControlRigParameterNodeSpawner::CreateFromPinType(PinType, false, MenuDesc, NodeCategory, ToolTip);
			//check(NodeSpawnerSetter != nullptr);
			//InActionRegistrar.AddBlueprintAction(InActionKey, NodeSpawnerSetter);
		}
	};

	FString VariableCategory = LOCTEXT("NewVariable", "New Variable").ToString();
	FString ParameterCategory = LOCTEXT("NewParameter", "New Parameter").ToString();
	for (const FEdGraphPinType& PinType: PinTypes)
	{
		Local::AddVariableActions(ActionKey, ActionRegistrar, PinType, VariableCategory);
		// let's disable parameters in the UI for now.
		Local::AddParameterActions(ActionKey, ActionRegistrar, PinType, ParameterCategory);
	}

	// add support for names as parameters
	Local::AddParameterActions(ActionKey, ActionRegistrar, FEdGraphPinType(UEdGraphSchema_K2::PC_Name, FName(NAME_None), nullptr, EPinContainerType::None, false, FEdGraphTerminalType()), ParameterCategory);

	UBlueprintNodeSpawner* RerouteNodeSpawner = UControlRigRerouteNodeSpawner::CreateGeneric(
		LOCTEXT("RerouteSpawnerDesc", "New Reroute Node"),
		LOCTEXT("RerouteSpawnerCategory", "Organization"), 
		LOCTEXT("RerouteSpawnerTooltip", "Adds a new reroute node to the graph"));
	ActionRegistrar.AddBlueprintAction(ActionKey, RerouteNodeSpawner);
}

FConnectionDrawingPolicy* FControlRigEditorModule::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj)
{
	return new FControlRigConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

void FControlRigEditorModule::GetNodeContextMenuActions(const UControlRigGraphNode* Node, UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	{
		if (Context->Pin != nullptr)
		{
			// Add array operations for array pins
			if (Context->Pin->PinType.IsArray())
			{
				FToolMenuSection& Section = Menu->AddSection(TEXT("ArrayOperations"), LOCTEXT("ArrayOperations", "Array Operations"));

				// Array operations
				Section.AddMenuEntry(
					"ClearArray",
					LOCTEXT("ClearArray", "Clear"),
					LOCTEXT("ClearArray_Tooltip", "Clear this array of all of its entries"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(const_cast<UControlRigGraphNode*>(Node), &UControlRigGraphNode::HandleClearArray, Context->Pin->PinName.ToString())));
			}
			else if (Context->Pin->ParentPin != nullptr && Context->Pin->ParentPin->PinType.IsArray())
			{
				FToolMenuSection& Section = Menu->AddSection(TEXT("ArrayElementOperations"), LOCTEXT("ArrayElementOperations", "Array Element Operations"));

				// Array element operations
				Section.AddMenuEntry(
					"RemoveArrayElement",
					LOCTEXT("RemoveArrayElement", "Remove"),
					LOCTEXT("RemoveArrayElement_Tooltip", "Remove this array element"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(const_cast<UControlRigGraphNode*>(Node), &UControlRigGraphNode::HandleRemoveArrayElement, Context->Pin->PinName.ToString())));

				Section.AddMenuEntry(
					"InsertArrayElement",
					LOCTEXT("InsertArrayElement", "Insert"),
					LOCTEXT("InsertArrayElement_Tooltip", "Insert an array element after this one"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(const_cast<UControlRigGraphNode*>(Node), &UControlRigGraphNode::HandleInsertArrayElement, Context->Pin->PinName.ToString())));
			}
		}
	}
}

void FControlRigEditorModule::GetContextMenuActions(const UControlRigGraphSchema* Schema, UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (Menu && Context)
	{
		Schema->UEdGraphSchema::GetContextMenuActions(Menu, Context);

		if (UEdGraphPin* InGraphPin = (UEdGraphPin* )Context->Pin)
		{
			{
				FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaPinActions", LOCTEXT("PinActionsMenuHeader", "Pin Actions"));
				// Break pin links
				if (InGraphPin->LinkedTo.Num() > 0)
				{
					Section.AddMenuEntry(FGraphEditorCommands::Get().BreakPinLinks);
				}
			}

			// Add the watch pin / unwatch pin menu items
			{
				FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaWatches", LOCTEXT("WatchesHeader", "Watches"));
				UBlueprint* OwnerBlueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(Context->Graph);
				{
					if (FKismetDebugUtilities::IsPinBeingWatched(OwnerBlueprint, InGraphPin))
					{
						Section.AddMenuEntry(FGraphEditorCommands::Get().StopWatchingPin);
					}
					else
					{
						Section.AddMenuEntry(FGraphEditorCommands::Get().StartWatchingPin);
					}
				}
			}

			// Add alphainterp menu entries
			if(UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>((UBlueprint*)Context->Blueprint))
			{
				if (URigVMPin* ModelPin = RigBlueprint->Model->FindPin(InGraphPin->GetName()))
				{

					if (ModelPin->IsArray())
					{
						FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaPinArrays", LOCTEXT("PinArrays", "Arrays"));
						Section.AddMenuEntry(
							"ClearPinArray",
							LOCTEXT("ClearPinArray", "Clear Array"),
							LOCTEXT("ClearPinArray_Tooltip", "Removes all elements of the array."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
								RigBlueprint->Controller->ClearArrayPin(ModelPin->GetPinPath());
							})
						));
		}
					if(ModelPin->IsArrayElement())
					{
						FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaPinArrays", LOCTEXT("PinArrays", "Arrays"));
						Section.AddMenuEntry(
							"RemoveArrayPin",
							LOCTEXT("RemoveArrayPin", "Remove Array Element"),
							LOCTEXT("RemoveArrayPin_Tooltip", "Removes the selected element from the array"),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
								RigBlueprint->Controller->RemoveArrayPin(ModelPin->GetPinPath());
							})
						));
						Section.AddMenuEntry(
							"DuplicateArrayPin",
							LOCTEXT("DuplicateArrayPin", "Duplicate Array Element"),
							LOCTEXT("DuplicateArrayPin_Tooltip", "Duplicates the selected element"),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
								RigBlueprint->Controller->DuplicateArrayPin(ModelPin->GetPinPath());
							})
						));
					}

					if(Cast<URigVMStructNode>(ModelPin->GetNode()))
					{
						FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaPinDefaults", LOCTEXT("PinDefaults", "Pin Defaults"));
						Section.AddMenuEntry(
							"ResetPinDefaultValue",
							LOCTEXT("ResetPinDefaultValue", "Reset Pin Value"),
							LOCTEXT("ResetPinDefaultValue_Tooltip", "Resets the pin's value to its default."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
								RigBlueprint->Controller->ResetPinDefaultValue(ModelPin->GetPinPath());
							})
						));
	}

					if ((ModelPin->GetCPPType() == TEXT("FVector") ||
						 ModelPin->GetCPPType() == TEXT("FQuat") ||
						 ModelPin->GetCPPType() == TEXT("FTransform")) &&
						(ModelPin->GetDirection() == ERigVMPinDirection::Input ||
						 ModelPin->GetDirection() == ERigVMPinDirection::IO) &&
						 ModelPin->GetPinForLink()->GetRootPin()->GetSourceLinks(true).Num() == 0)
					{
						FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaControlPin", LOCTEXT("ControlPin", "Direct Manipulation"));
						Section.AddMenuEntry(
							"DirectManipControlPin",
							LOCTEXT("DirectManipControlPin", "Control Pin Value"),
							LOCTEXT("DirectManipControlPin_Tooltip", "Configures the pin for direct interaction in the viewport"),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
								RigBlueprint->AddTransientControl(ModelPin);
							})
						));
					}

					if (ModelPin->GetRootPin() == ModelPin && Cast<URigVMStructNode>(ModelPin->GetNode()) != nullptr)
					{
						if (ModelPin->HasInjectedNodes())
						{
							FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaNodeEjectionInterp", LOCTEXT("NodeEjectionInterp", "Eject"));

							Section.AddMenuEntry(
								"EjectLastNode",
								LOCTEXT("EjectLastNode", "Eject Last Node"),
								LOCTEXT("EjectLastNode_Tooltip", "Eject the last injected node"),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([RigBlueprint, ModelPin]() {
									RigBlueprint->Controller->EjectNodeFromPin(ModelPin->GetPinPath());
								})
							));
						}

						if (ModelPin->GetCPPType() == TEXT("float") ||
							ModelPin->GetCPPType() == TEXT("FVector"))
						{
							FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaNodeInjectionInterp", LOCTEXT("NodeInjectionInterp", "Interpolate"));
							URigVMNode* InterpNode = nullptr;
							for (URigVMInjectionInfo* Injection : ModelPin->GetInjectedNodes())
							{
								FString PrototypeName;
								if (Injection->StructNode->GetScriptStruct()->GetStringMetaDataHierarchical(TEXT("PrototypeName"), &PrototypeName))
								{
									if (PrototypeName == TEXT("AlphaInterp"))
									{
										InterpNode = Injection->StructNode;
										break;
									}
								}
							}

							if (InterpNode == nullptr)
							{
								UScriptStruct* ScriptStruct = nullptr;

								if (ModelPin->GetCPPType() == TEXT("float"))
								{
									ScriptStruct = FRigUnit_AlphaInterp::StaticStruct();
								}
								else if (ModelPin->GetCPPType() == TEXT("FVector"))
								{
									ScriptStruct = FRigUnit_AlphaInterpVector::StaticStruct();
								}
								else
								{
									checkNoEntry();
								}

								Section.AddMenuEntry(
									"AddAlphaInterp",
									LOCTEXT("AddAlphaInterp", "Add Interpolate"),
									LOCTEXT("AddAlphaInterp_Tooltip", "Injects an interpolate node"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, InGraphPin, ModelPin, ScriptStruct]() {
										URigVMInjectionInfo* Injection = RigBlueprint->Controller->AddInjectedNode(ModelPin->GetPinPath(), ModelPin->GetDirection() != ERigVMPinDirection::Output, ScriptStruct, TEXT("Execute"), TEXT("Value"), TEXT("Result"));
										if (Injection)
										{
											TArray<FName> NodeNames;
											NodeNames.Add(Injection->StructNode->GetFName());
											RigBlueprint->Controller->SetNodeSelection(NodeNames);
										}
									})
								));
							}
							else
							{
								Section.AddMenuEntry(
									"EditAlphaInterp",
									LOCTEXT("EditAlphaInterp", "Edit Interpolate"),
									LOCTEXT("EditAlphaInterp_Tooltip", "Edit the interpolate node"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, InterpNode]() {
									TArray<FName> NodeNames;
									NodeNames.Add(InterpNode->GetFName());
									RigBlueprint->Controller->SetNodeSelection(NodeNames);
								})
									));
								Section.AddMenuEntry(
									"RemoveAlphaInterp",
									LOCTEXT("RemoveAlphaInterp", "Remove Interpolate"),
									LOCTEXT("RemoveAlphaInterp_Tooltip", "Removes the interpolate node"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, InGraphPin, ModelPin, InterpNode]() {
										RigBlueprint->Controller->RemoveNodeByName(InterpNode->GetFName());
									})
								));
							}
						}

						if (ModelPin->GetCPPType() == TEXT("FVector") ||
							ModelPin->GetCPPType() == TEXT("FQuat") ||
							ModelPin->GetCPPType() == TEXT("FTransform"))
						{
							FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaNodeInjectionVisualDebug", LOCTEXT("NodeInjectionVisualDebug", "Visual Debug"));

							URigVMNode* VisualDebugNode = nullptr;
							for (URigVMInjectionInfo* Injection : ModelPin->GetInjectedNodes())
							{
								FString PrototypeName;
								if (Injection->StructNode->GetScriptStruct()->GetStringMetaDataHierarchical(TEXT("PrototypeName"), &PrototypeName))
								{
									if (PrototypeName == TEXT("VisualDebug"))
									{
										VisualDebugNode = Injection->StructNode;
										break;
									}
								}
							}

							if (VisualDebugNode == nullptr)
							{
								UScriptStruct* ScriptStruct = nullptr;

								if (ModelPin->GetCPPType() == TEXT("FVector"))
								{
									ScriptStruct = FRigUnit_VisualDebugVector::StaticStruct();
								}
								else if (ModelPin->GetCPPType() == TEXT("FQuat"))
								{
									ScriptStruct = FRigUnit_VisualDebugQuat::StaticStruct();
								}
								else if (ModelPin->GetCPPType() == TEXT("FTransform"))
								{
									ScriptStruct = FRigUnit_VisualDebugTransform::StaticStruct();
								}
								else
								{
									checkNoEntry();
								}

								Section.AddMenuEntry(
									"AddVisualDebug",
									LOCTEXT("AddVisualDebug", "Add Visual Debug"),
									LOCTEXT("AddVisualDebug_Tooltip", "Injects a visual debugging node"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, InGraphPin, ModelPin, ScriptStruct]() {
										URigVMInjectionInfo* Injection = RigBlueprint->Controller->AddInjectedNode(ModelPin->GetPinPath(), ModelPin->GetDirection() != ERigVMPinDirection::Output, ScriptStruct, TEXT("Execute"), TEXT("Value"), TEXT("Value"));
										if (Injection)
										{
											TArray<FName> NodeNames;
											NodeNames.Add(Injection->StructNode->GetFName());
											RigBlueprint->Controller->SetNodeSelection(NodeNames);

											if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(ModelPin->GetNode()))
											{
												if (TSharedPtr<FStructOnScope> DefaultStructScope = StructNode->ConstructStructInstance())
												{
													FRigVMStruct* DefaultStruct = (FRigVMStruct*)DefaultStructScope->GetStructMemory();

													FString PinPath = ModelPin->GetPinPath();
													FString Left, Right;

													FName SpaceName = NAME_None;
													if (URigVMPin::SplitPinPathAtStart(PinPath, Left, Right))
													{
														SpaceName = DefaultStruct->DetermineSpaceForPin(Right, &RigBlueprint->HierarchyContainer);
													}

													if (!SpaceName.IsNone())
													{
														if (URigVMPin* BoneSpacePin = Injection->StructNode->FindPin(TEXT("BoneSpace")))
														{
															RigBlueprint->Controller->SetPinDefaultValue(BoneSpacePin->GetPinPath(), SpaceName.ToString());
														}
													}
												}
											}
										}
									})
								));
							}
							else
							{
								Section.AddMenuEntry(
									"EditVisualDebug",
									LOCTEXT("EditVisualDebug", "Edit Visual Debug"),
									LOCTEXT("EditVisualDebug_Tooltip", "Edit the visual debugging node"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, VisualDebugNode]() {
										TArray<FName> NodeNames;
										NodeNames.Add(VisualDebugNode->GetFName());
										RigBlueprint->Controller->SetNodeSelection(NodeNames);
									})
								));
								Section.AddMenuEntry(
									"ToggleVisualDebug",
									LOCTEXT("ToggleVisualDebug", "Toggle Visual Debug"),
									LOCTEXT("ToggleVisualDebug_Tooltip", "Toggle the visibility the visual debugging"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([RigBlueprint, VisualDebugNode]() {
										URigVMPin* EnabledPin = VisualDebugNode->FindPin(TEXT("bEnabled"));
										check(EnabledPin);
										RigBlueprint->Controller->SetPinDefaultValue(EnabledPin->GetPinPath(), EnabledPin->GetDefaultValue() == TEXT("True") ? TEXT("False") : TEXT("True"), false);
									})
								));
								Section.AddMenuEntry(
									"RemoveVisualDebug",
									LOCTEXT("RemoveVisualDebug", "Remove Visual Debug"),
									LOCTEXT("RemoveVisualDebug_Tooltip", "Removes the visual debugging node"),
									FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([RigBlueprint, InGraphPin, ModelPin, VisualDebugNode]() {
										RigBlueprint->Controller->RemoveNodeByName(VisualDebugNode->GetFName());
									})
								));
							}
						}
					}
				}
			}
		}
		else if(Context->Node) // right clicked on the node
		{
			if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>((UBlueprint*)Context->Blueprint))
			{
				TArray<FRigElementKey> RigElementsToSelect;
				if (URigVMNode* ModelNode = RigBlueprint->Model->FindNodeByName(Context->Node->GetFName()))
				{
					for (const URigVMPin* Pin : ModelNode->GetPins())
					{
						if (Pin->GetCPPType() == TEXT("FName"))
						{
							if (Pin->GetCustomWidgetName() == TEXT("BoneName"))
							{
								RigElementsToSelect.Add(FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Bone));
							}
							else if (Pin->GetCustomWidgetName() == TEXT("ControlName"))
							{
								RigElementsToSelect.Add(FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Control));
							}
							else if (Pin->GetCustomWidgetName() == TEXT("SpaceName"))
							{
								RigElementsToSelect.Add(FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Space));
							}
							else if (Pin->GetCustomWidgetName() == TEXT("CurveName"))
							{
								RigElementsToSelect.Add(FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Curve));
							}
						}
					}
				}

				if (RigElementsToSelect.Num() > 0)
				{
					FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaHierarchy", LOCTEXT("HierarchyHeader", "Hierarchy"));
					Section.AddMenuEntry(
						"SelectRigElements",
						LOCTEXT("SelectRigElements", "Select Rig Elements"),
						LOCTEXT("SelectRigElements_Tooltip", "Selects the bone, controls or spaces associated with this node."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([RigBlueprint, RigElementsToSelect]() {

							RigBlueprint->HierarchyContainer.ClearSelection();
							for (const FRigElementKey& RigElementToSelect : RigElementsToSelect)
	{
								RigBlueprint->HierarchyContainer.Select(RigElementToSelect, true);
	}

						})
					));
				}
			}
		}
	}
}

IMPLEMENT_MODULE(FControlRigEditorModule, ControlRigEditor)

#undef LOCTEXT_NAMESPACE
