// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditor.h"

#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"
#include "OptimusEditorGraphSchema.h"
#include "OptimusEditorCommands.h"
#include "OptimusEditorViewport.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "SOptimusGraphTitleBar.h"

#include "OptimusDeformer.h"
#include "OptimusActionStack.h"
#include "OptimusCoreNotify.h"

#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "IPersonaViewport.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "IAssetFamily.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "OptimusEditorMode.h"
#include "PersonaModule.h"
#include "PersonaTabs.h"


#define LOCTEXT_NAMESPACE "OptimusEditor"

const FName OptimusEditorAppName(TEXT("OptimusEditorApp"));


FOptimusEditor::FOptimusEditor()
{

}


FOptimusEditor::~FOptimusEditor()
{
	if (DeformerObject)
	{
		DeformerObject->GetNotifyDelegate().RemoveAll(this);
	}
}


void FOptimusEditor::Construct(
	const EToolkitMode::Type InMode,
	const TSharedPtr< class IToolkitHost >& InToolkitHost,
	UOptimusDeformer* InDeformerObject)
{
	DeformerObject = InDeformerObject;

	FPersonaModule& PersonaModule = FModuleManager::GetModuleChecked<FPersonaModule>("Persona");
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	// PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FOptimusEditor::HandlePreviewSceneCreated);
	PersonaToolkit = PersonaModule.CreatePersonaToolkit(InDeformerObject, PersonaToolkitArgs);

	PersonaToolkit->GetPreviewScene()->SetDefaultAnimationMode(EPreviewSceneDefaultAnimationMode::Animation);

	// TODO: Do we need this?
	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(InDeformerObject);
	AssetFamily->RecordAssetOpened(FAssetData(InDeformerObject));
	
	// Construct a new graph with a default name
	// TODO: Use a document manager like blueprints.
	// FIXME: The deformer asset shouldn't really be the owner.
	EditorGraph = NewObject<UOptimusEditorGraph>(
		DeformerObject, UOptimusEditorGraph::StaticClass(), NAME_None, 
		RF_Transactional|RF_Transient);
	EditorGraph->Schema = UOptimusEditorGraphSchema::StaticClass();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	// This call relies on virtual functions, so cannot be called from the constructor, hence
	// the dual-construction style.
	InitAssetEditor(InMode, InToolkitHost, OptimusEditorAppName, FTabManager::FLayout::NullLayout,
		bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InDeformerObject);

	BindCommands();

	// Set the default editor mode. This creates the editor layout and tabs.
	AddApplicationMode(FOptimusEditorMode::ModeId,MakeShareable(new FOptimusEditorMode(SharedThis(this))));

	CreateWidgets();

	SetCurrentMode(FOptimusEditorMode::ModeId);
	
	RegisterToolbar();

	
	// Find the update graph and set that as the startup graph.
	for (UOptimusNodeGraph* Graph: DeformerObject->GetGraphs())
	{
		if (Graph->GetGraphType() == EOptimusNodeGraphType::Update)
		{
			PreviousEditedNodeGraph = UpdateGraph = Graph;
			break;
		}
	}
	SetEditGraph(UpdateGraph);


	// Ensure that the action stack creates undoable transactions when actions are run.
	DeformerObject->GetActionStack()->SetTransactionScopeFunctions(
		[](UObject* InTransactObject, const FString& Title)->int32 {
			int32 TransactionId = INDEX_NONE;
			if (GEditor && GEditor->Trans)
			{
				InTransactObject->SetFlags(RF_Transactional);
				TransactionId = GEditor->BeginTransaction(TEXT(""), FText::FromString(Title), InTransactObject);
				InTransactObject->Modify();
			}
			return TransactionId;
		},
		[](int32 InTransactionId) {
			if (GEditor && GEditor->Trans && InTransactionId >= 0)
			{
				// For reasons I cannot fathom, EndTransaction returns the active index upon
				// entry, rather than the active index on exit. Which makes it one higher than
				// the index we got from BeginTransaction ¯\_(ツ)_/¯
				int32 TransactionId = GEditor->EndTransaction();
				check(InTransactionId == (TransactionId - 1));
			}
		}
		);

	// Make sure we get told when the deformer changes.
	DeformerObject->GetNotifyDelegate().AddRaw(this, &FOptimusEditor::OnDeformerModified);

	DeformerObject->GetCompileBeginDelegate().AddRaw(this, &FOptimusEditor::CompileBegin);
	DeformerObject->GetCompileEndDelegate().AddRaw(this, &FOptimusEditor::CompileEnd);
}


IOptimusNodeGraphCollectionOwner* FOptimusEditor::GetGraphCollectionRoot() const
{
	return DeformerObject;
}


UOptimusDeformer* FOptimusEditor::GetDeformer() const
{
	return DeformerObject;
}


FText FOptimusEditor::GetGraphCollectionRootName() const
{
	return FText::FromName(DeformerObject->GetFName());
}


UOptimusActionStack* FOptimusEditor::GetActionStack() const
{
	return DeformerObject->GetActionStack();
}


void FOptimusEditor::InspectObject(UObject* InObject)
{
	PropertyDetailsWidget->SetObject(InObject, /*bForceRefresh=*/true);

	// Bring the node details tab into the open.
	GetTabManager()->TryInvokeTab(FPersonaTabs::DetailsID);
}


void FOptimusEditor::InspectObjects(const TArray<UObject*>& InObjects)
{
	PropertyDetailsWidget->SetObjects(InObjects, /*bForceRefresh=*/true);

	// Bring the node details tab into the open.
	GetTabManager()->TryInvokeTab(FPersonaTabs::DetailsID);
}


FName FOptimusEditor::GetToolkitFName() const
{
	return FName("OptimusEditor");
}


FText FOptimusEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Optimus Deformer Editor");
}


FString FOptimusEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Optimus Deformer Editor ").ToString();
}


FLinearColor FOptimusEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.2f, 0.2f, 0.6f, 0.5f);
}



bool FOptimusEditor::SetEditGraph(UOptimusNodeGraph* InNodeGraph)
{
	if (ensure(InNodeGraph))
	{
		PreviousEditedNodeGraph = EditorGraph->NodeGraph;

		GraphEditorWidget->ClearSelectionSet();

		EditorGraph->Reset();
		EditorGraph->InitFromNodeGraph(InNodeGraph);

		// FIXME: Store pan/zoom

		RefreshEvent.Broadcast();
		return true;
	}
	else
	{
		return false;
	}
}


void FOptimusEditor::Compile()
{
	if (DeformerObject->Compile())
	{
		// Ensure we do a redraw.
		// FIXMNE: 
		// EditorViewportWidget->GetViewportClient()->Invalidate();
	}
}


bool FOptimusEditor::CanCompile() const
{
	return true;
}


void FOptimusEditor::CompileBegin(UOptimusDeformer* InDeformer)
{
	// FIXMNE
	// EditorViewportWidget->RemoveDataProviders();
}


void FOptimusEditor::CompileEnd(UOptimusDeformer* InDeformer)
{
	// FIXME
	// EditorViewportWidget->InstallDataProviders(InDeformer);
}


void FOptimusEditor::SelectAllNodes()
{
	GraphEditorWidget->SelectAllNodes();
}


bool FOptimusEditor::CanSelectAllNodes() const
{
	return GraphEditorWidget.IsValid();
}


void FOptimusEditor::DeleteSelectedNodes()
{
	TArray<UOptimusNode*> NodesToDelete;
	for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
	{
		UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(Object);
		if (GraphNode && GraphNode->CanUserDeleteNode())
		{
			NodesToDelete.Add(GraphNode->ModelNode);
		}
	}

	if (NodesToDelete.IsEmpty())
	{
		return;
	}

	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphEditorWidget->GetCurrentGraph());
	if (Graph) 
	{
		Graph->GetModelGraph()->RemoveNodes(NodesToDelete);
	}

	GraphEditorWidget->ClearSelectionSet();
}


bool FOptimusEditor::CanDeleteSelectedNodes() const
{
	for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
	{
		UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object);
		if (GraphNode && GraphNode->CanUserDeleteNode())
		{
			return true;
		}
	}

	return false;
}


void FOptimusEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	TSet<UOptimusEditorGraphNode*> SelectedNodes;

	for (UObject* Object : NewSelection)
	{
		if (UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(Object))
		{
			SelectedObjects.Add(GraphNode->ModelNode);
			SelectedNodes.Add(GraphNode);
		}
		else
		{
			SelectedObjects.Add(Object);
		}
	}

	// Make sure the graph knows too.
	EditorGraph->SetSelectedNodes(SelectedNodes);

	if (SelectedObjects.IsEmpty())
	{
		// If nothing was selected, default to the deformer object.
		SelectedObjects.Add(DeformerObject);
	}

	PropertyDetailsWidget->SetObjects(SelectedObjects, /*bForceRefresh=*/true);

	// Bring the node details tab into the open.
	GetTabManager()->TryInvokeTab(FPersonaTabs::DetailsID);
}


void FOptimusEditor::OnNodeDoubleClicked(class UEdGraphNode* Node)
{

}


void FOptimusEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{

}


bool FOptimusEditor::OnVerifyNodeTextCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage)
{
	return false;
}


FReply FOptimusEditor::OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2D& InPosition, UEdGraph* InGraph)
{
	return FReply::Handled();
}


void FOptimusEditor::RegisterToolbar()
{
	FName ParentName;
	const FName MenuName = GetToolMenuToolbarName(ParentName);
	if (UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		return;
	}

	UToolMenu* ToolBar = UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);
	const FOptimusEditorCommands& Commands = FOptimusEditorCommands::Get();
	
	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& Section = ToolBar->AddSection("Compile", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.Compile,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Blueprint.CompileStatus.Background")));
	}

}


void FOptimusEditor::BindCommands()
{
	const FOptimusEditorCommands& Commands = FOptimusEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.Compile,
		FExecuteAction::CreateSP(this, &FOptimusEditor::Compile),
		FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanCompile));
}


void FOptimusEditor::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPreviewScene)
{
}


void FOptimusEditor::HandleDetailsCreated(
	const TSharedRef<IDetailsView>& InDetailsView
	)
{
	PropertyDetailsWidget = InDetailsView;
}


void FOptimusEditor::HandleViewportCreated(
	const TSharedRef<IPersonaViewport>& InPersonaViewport
	)
{
	// ViewportWidget = InPersonaViewport;

	// FIXME: Set up the compute component.
}


void FOptimusEditor::CreateWidgets()
{
	// -- Graph editor
	GraphEditorWidget = CreateGraphEditorWidget();
	GraphEditorWidget->SetViewLocation(FVector2D::ZeroVector, 1);
}


TSharedRef<SGraphEditor> FOptimusEditor::CreateGraphEditorWidget()
{
	GraphEditorCommands = MakeShareable(new FUICommandList);
	{
		// Editing commands
		GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &FOptimusEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanSelectAllNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FOptimusEditor::DeleteSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanDeleteSelectedNodes)
		);

#if 0
		GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &FOptimusEditor::CopySelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanCopyNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &FOptimusEditor::PasteNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanPasteNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &FOptimusEditor::CutSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanCutNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &FOptimusEditor::DuplicateNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanDuplicateNodes)
		);

		// Graph Editor Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
			FExecuteAction::CreateSP(this, &FOptimusEditor::OnCreateComment)
		);
#endif

#if 0

		// Alignment Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignTop)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignMiddle)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignBottom)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesLeft,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignLeft)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesCenter,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignCenter)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesRight,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignRight)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().StraightenConnections,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnStraightenConnections)
		);

		// Distribution Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnDistributeNodesH)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnDistributeNodesV)
		);
#endif
	}

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FOptimusEditor::OnSelectedNodesChanged);
	InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FOptimusEditor::OnNodeDoubleClicked);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FOptimusEditor::OnNodeTitleCommitted);
	InEvents.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FOptimusEditor::OnVerifyNodeTextCommit);
	InEvents.OnSpawnNodeByShortcut = 
		SGraphEditor::FOnSpawnNodeByShortcut::CreateSP(
			this, &FOptimusEditor::OnSpawnGraphNodeByShortcut, 
			static_cast<UEdGraph*>(EditorGraph));

	// Create the title bar widget
	TSharedPtr<SWidget> TitleBarWidget = SNew(SOptimusGraphTitleBar)
		.OptimusEditor(SharedThis(this));

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.TitleBar(TitleBarWidget)
		.Appearance(this, &FOptimusEditor::GetGraphAppearance)
		.GraphToEdit(EditorGraph)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}


FGraphAppearanceInfo FOptimusEditor::GetGraphAppearance() const
{
	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("AppearanceCornerText_OptimusDeformer", "OPTIMUS DEFORMER");
	return Appearance;
}


void FOptimusEditor::OnDeformerModified(
	EOptimusGlobalNotifyType InNotifyType, 
	UObject* InModifiedObject
	)
{
	switch (InNotifyType)
	{
	case EOptimusGlobalNotifyType::GraphAdded:
		SetEditGraph(Cast<UOptimusNodeGraph>(InModifiedObject));
		RefreshEvent.Broadcast();
		break;

	case EOptimusGlobalNotifyType::GraphIndexChanged:
	case EOptimusGlobalNotifyType::GraphRenamed:
		RefreshEvent.Broadcast();
		break;

	case EOptimusGlobalNotifyType::ResourceAdded:
	case EOptimusGlobalNotifyType::VariableAdded:
		InspectObject(InModifiedObject);
		RefreshEvent.Broadcast();
		break;

	case EOptimusGlobalNotifyType::ResourceRemoved:
	case EOptimusGlobalNotifyType::VariableRemoved:
		InspectObject(UpdateGraph);
		RefreshEvent.Broadcast();
		break;

	case EOptimusGlobalNotifyType::ResourceRenamed:
	case EOptimusGlobalNotifyType::ResourceIndexChanged:
	case EOptimusGlobalNotifyType::VariableRenamed:
	case EOptimusGlobalNotifyType::VariableIndexChanged:
		RefreshEvent.Broadcast();
		break;
		

	case EOptimusGlobalNotifyType::GraphRemoved: 
	{
		// If the currently editing graph is being removed, then switch to the previous graph
		// or the update graph if no previous graph.
		UOptimusNodeGraph* RemovedGraph = Cast<UOptimusNodeGraph>(InModifiedObject);
		if (EditorGraph->NodeGraph == RemovedGraph)
		{
			if (ensure(PreviousEditedNodeGraph))
			{
				SetEditGraph(PreviousEditedNodeGraph);
			}
			PreviousEditedNodeGraph = UpdateGraph;
		}
		else if (PreviousEditedNodeGraph == RemovedGraph)
		{
			PreviousEditedNodeGraph = UpdateGraph;
		}
		RefreshEvent.Broadcast();
		break;
	}
	}
}


void FOptimusEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		FProperty* Property = PropertyChangedEvent.Property;

		for (int32 Index = 0; Index < PropertyChangedEvent.GetNumObjectsBeingEdited(); Index++ )
		{
			if (const UOptimusNode* ModelNode = Cast<const UOptimusNode>(PropertyChangedEvent.GetObjectBeingEdited(Index)))
			{
				UOptimusNodeGraph* ModelGraph = ModelNode->GetOwningGraph();
				if (UpdateGraph && UpdateGraph == ModelGraph)
				{
					const UOptimusNodePin* ModelPin = ModelNode->FindPinFromProperty(
						PropertyChangedEvent.MemberProperty,
						PropertyChangedEvent.Property);

					if (ModelPin)
					{
						if (UOptimusEditorGraphNode* GraphNode = EditorGraph->FindGraphNodeFromModelNode(ModelNode))
						{
							GraphNode->SynchronizeGraphPinValueWithModelPin(ModelPin);
						}
					}
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
