// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/SDisplayClusterConfiguratorGraphEditor.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingSlot.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"


#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorOutputMappingWindowSlot"
FDisplayClusterConfiguratorWindowNodeFactory::FDisplayClusterConfiguratorWindowNodeFactory(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<SDisplayClusterConfiguratorGraphEditor>& InGraphEditor)
	: ToolkitPtr(InToolkit)
	, GraphEditorPtr(InGraphEditor)
{
}

TSharedPtr<SGraphNode> FDisplayClusterConfiguratorWindowNodeFactory::CreateNodeWidget(UEdGraphNode* InNode)
{
	if (UDisplayClusterConfiguratorCanvasNode* CanvasNode = Cast<UDisplayClusterConfiguratorCanvasNode>(InNode))
	{
		TSharedPtr<SDisplayClusterConfiguratorCanvasNode> GraphCanvasNode = SNew(SDisplayClusterConfiguratorCanvasNode, CanvasNode, ToolkitPtr.Pin().ToSharedRef());
		GraphEditorPtr.Pin()->SetRootNode(GraphCanvasNode.ToSharedRef());

		TSharedPtr<IDisplayClusterConfiguratorViewOutputMapping> OutputMappingView = ToolkitPtr.Pin()->GetViewOutputMapping();
		if (OutputMappingView)
		{
			OutputMappingView->GetOnOutputMappingBuiltDelegate().Broadcast();
		}

		return GraphCanvasNode;
	}

	return FGraphNodeFactory::CreateNodeWidget(InNode);
}

void SDisplayClusterConfiguratorGraphEditor::SetViewportPreviewTexture(const FString& NodeId, const FString& ViewportId, UTexture* InTexture)
{
	if (TSharedPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNode = CanvasNodePtr.Pin())
	{
		for (const TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>& Slot : CanvasNode->GetAllSlots())
		{
			if (Slot->GetType() == FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport)
			{
				if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> ParentSlot = Slot->GetParentSlot())
				{
					if (ParentSlot->GetName().Equals(NodeId) && Slot->GetName().Equals(ViewportId))
					{
						Slot->SetPreviewTexture(InTexture);
						
						// Found, we can break
						break;
					}
				}
			}
		}
	}
}

void SDisplayClusterConfiguratorGraphEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	SelectedNodes = NewSelection;

	if (bClearSelection)
	{
		return;
	}

	if (NewSelection.Num() > 0)
	{
		TArray<UDisplayClusterConfiguratorBaseNode*> NewSelectedNodes;
		const TArray<UObject*>& SelectedObjects =  ToolkitPtr.Pin()->GetSelectedObjects();

		TArray<UObject*> Selection;
		for (UObject* Obj : NewSelection)
		{
			if (UDisplayClusterConfiguratorBaseNode* GraphNode = Cast<UDisplayClusterConfiguratorBaseNode>(Obj))
			{
				if (!SelectedObjects.Contains(GraphNode->GetObject()) && !GraphNode->IsSelected())
				{
					NewSelectedNodes.Add(GraphNode);
				}
			}
		}

		if (NewSelectedNodes.Num())
		{
			// Reset before set active slot
			if (TSharedPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNode = CanvasNodePtr.Pin())
			{
				for (const TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>& Slot : CanvasNode->GetAllSlots())
				{
					Slot->SetActive(false);
				}
			}

			for (UDisplayClusterConfiguratorBaseNode* SelectedNode : NewSelectedNodes)
			{
				SelectedNode->OnSelection();

				if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Slot = SelectedNode->GetSlot())
				{
					Slot->SetActive(true);
				}
			}
		}
	}
	else
	{
		TArray<UObject*> SelectedObjects;
		ToolkitPtr.Pin()->SelectObjects(SelectedObjects);
	}
}

void SDisplayClusterConfiguratorGraphEditor::OnObjectSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();
	if (!SelectedObjects.Num())
	{
		bClearSelection = true;
		ClearSelectionSet();
		bClearSelection = false;

		return;
	}

	auto IsAlreadySelectedFunction = [this, &SelectedObjects]() -> bool
	{
		bool bAlreadySelected = false;

		for (UObject* Node : SelectedNodes)
		{
			if (UDisplayClusterConfiguratorBaseNode* GraphNode = Cast<UDisplayClusterConfiguratorBaseNode>(Node))
			{
				for (UObject* SelectedObject : SelectedObjects)
				{
					if (SelectedObject == GraphNode->GetObject())
					{
						return true;
					}
				}
			}
		}

		return false;
	};

	// Check if node already selected
	if (IsAlreadySelectedFunction())
	{
		return;
	}

	// Clear all selection
	bClearSelection = true;
	ClearSelectionSet();
	bClearSelection = false;

	// Select active node
	if (TSharedPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNode = CanvasNodePtr.Pin())
	{
		for (const TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>& Slot : CanvasNode->GetAllSlots())
		{
			// Reset all active slots
			Slot->SetActive(false);

			UDisplayClusterConfiguratorBaseNode* BaseNode = Cast<UDisplayClusterConfiguratorBaseNode>(Slot->GetGraphNode());
			if (BaseNode->IsSelected())
			{
				// Set active slot and active node
				Slot->SetActive(true);
				SetNodeSelection(BaseNode, true);
			}
		}
	}
}

void SDisplayClusterConfiguratorGraphEditor::SetRootNode(const TSharedRef<SDisplayClusterConfiguratorCanvasNode>& InCanvasNode)
{
	CanvasNodePtr = InCanvasNode;
}
void SDisplayClusterConfiguratorGraphEditor::OnConfigReloaded()
{
	RebuildCanvasNode();
}

void SDisplayClusterConfiguratorGraphEditor::RebuildCanvasNode()
{
	UDisplayClusterConfiguratorGraph* ConfiguratorGraph = ClusterConfiguratorGraph.Get();
	check(ConfiguratorGraph != nullptr);

	if (RootCanvasNode.IsValid())
	{
		ConfiguratorGraph->RemoveNode(RootCanvasNode.Get());
	}

	RootCanvasNode.Reset();

	FIntPoint CanvasSize = FIntPoint::ZeroValue;
	if (UDisplayClusterConfigurationData* Config = ToolkitPtr.Pin()->GetConfig())
	{
		// Add Canvas node
		UDisplayClusterConfiguratorCanvasNode* RootCanvasNodePtr = NewObject<UDisplayClusterConfiguratorCanvasNode>(ConfiguratorGraph, UDisplayClusterConfiguratorCanvasNode::StaticClass(), NAME_None, RF_Transactional);
		RootCanvasNode = TStrongObjectPtr<UDisplayClusterConfiguratorCanvasNode>(RootCanvasNodePtr);
		RootCanvasNodePtr->Initialize(Config->Cluster, FString(), ToolkitPtr.Pin().ToSharedRef());
		ConfiguratorGraph->AddNode(RootCanvasNodePtr);
	}
}

FActionMenuContent SDisplayClusterConfiguratorGraphEditor::OnCreateNodeOrPinMenu(UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, FMenuBuilder* MenuBuilder, bool bIsDebugging)
{
	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	if (InGraphNode->IsA(UDisplayClusterConfiguratorBaseNode::StaticClass()))
	{
		MenuBuilder->PushCommandList(CommandList.ToSharedRef());

		MenuBuilder->BeginSection(FName(TEXT("Documentation")), LOCTEXT("Documentation", "Documentation"));
		{
			MenuBuilder->AddMenuEntry(
				Commands.BrowseDocumentation,
				NAME_None,
				LOCTEXT("GoToDocsForActor", "View Documentation"),
				LOCTEXT("GoToDocsForActor_ToolTip", "Click to open documentation for nDisplay"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered")
				);
		}
		MenuBuilder->EndSection();

		MenuBuilder->BeginSection(FName(TEXT("CommonSection")), LOCTEXT("CommonSection", "Common"));
		{
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder->EndSection();

		MenuBuilder->PopCommandList();

		return FActionMenuContent(MenuBuilder->MakeWidget());
	}

	return FActionMenuContent();
}
void SDisplayClusterConfiguratorGraphEditor::BindCommands()
{
	CommandList = MakeShareable(new FUICommandList);

	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	CommandList->MapAction(
		Commands.BrowseDocumentation,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::BrowseDocumentation),
		FCanExecuteAction()
		);

	CommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanDeleteNodes)
		);

	CommandList->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanCopyNodes)
		);

	CommandList->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanCutNodes)
		);

	CommandList->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::PasteNodes),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanPasteNodes)
		);

	CommandList->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::DuplicateNodes),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanDuplicateNodes)
		);
}
void SDisplayClusterConfiguratorGraphEditor::BrowseDocumentation()
{
	const static FString OverrideUrlSection = TEXT("UnrealEd.URLOverrides");
	const static FString DocumentationURL = TEXT("DocumentationURL");
	const static FString NDisplayLink = TEXT("en-US/Engine/Rendering/nDisplay/");
	FString OutURL;

	if (GConfig->GetString(*OverrideUrlSection, *DocumentationURL, OutURL, GEditorIni))
	{
		FString URL = OutURL + NDisplayLink;
		FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
	}
}
void SDisplayClusterConfiguratorGraphEditor::DeleteSelectedNodes()
{
	// That will be implemented
}
bool SDisplayClusterConfiguratorGraphEditor::CanDeleteNodes() const
{
	// That will be implemented
	return false;
}
void SDisplayClusterConfiguratorGraphEditor::CopySelectedNodes()
{
	// That will be implemented
}
bool SDisplayClusterConfiguratorGraphEditor::CanCopyNodes() const
{
	// That will be implemented
	return false;
}
void SDisplayClusterConfiguratorGraphEditor::CutSelectedNodes()
{
	// That will be implemented
}
bool SDisplayClusterConfiguratorGraphEditor::CanCutNodes() const
{
	// That will be implemented
	return false;
}
void SDisplayClusterConfiguratorGraphEditor::PasteNodes()
{
	// That will be implemented
}
bool SDisplayClusterConfiguratorGraphEditor::CanPasteNodes() const
{
	// That will be implemented
	return false;
}
void SDisplayClusterConfiguratorGraphEditor::DuplicateNodes()
{
	// That will be implemented
}
bool SDisplayClusterConfiguratorGraphEditor::CanDuplicateNodes() const
{
	// That will be implemented
	return false;
}
void SDisplayClusterConfiguratorGraphEditor::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorViewOutputMapping>& InViewOutputMapping)
{
	ToolkitPtr = InToolkit;
	ViewOutputMappingPtr = InViewOutputMapping;
	bClearSelection = false;

	BindCommands();

	InToolkit->RegisterOnObjectSelected(IDisplayClusterConfiguratorToolkit::FOnObjectSelectedDelegate::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::OnObjectSelected));
	InToolkit->RegisterOnConfigReloaded(IDisplayClusterConfiguratorToolkit::FOnConfigReloadedDelegate::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::OnConfigReloaded));

	ClusterConfiguratorGraph = CastChecked<UDisplayClusterConfiguratorGraph>(InArgs._GraphToEdit);

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::OnSelectedNodesChanged);
	GraphEvents.OnCreateNodeOrPinMenu = SGraphEditor::FOnCreateNodeOrPinMenu::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::OnCreateNodeOrPinMenu);

	SGraphEditor::FArguments Arguments;
	Arguments._GraphToEdit = InArgs._GraphToEdit;
	Arguments._IsEditable = true;
	Arguments._GraphEvents = GraphEvents;

	SGraphEditor::Construct(Arguments);

	SetCanTick(true);

	SetNodeFactory(MakeShared<FDisplayClusterConfiguratorWindowNodeFactory>(InToolkit, SharedThis(this)));
}

void SDisplayClusterConfiguratorGraphEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphEditor::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

#undef LOCTEXT_NAMESPACE
