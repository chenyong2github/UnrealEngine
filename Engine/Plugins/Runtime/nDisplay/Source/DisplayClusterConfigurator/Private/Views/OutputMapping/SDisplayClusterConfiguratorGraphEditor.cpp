// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/SDisplayClusterConfiguratorGraphEditor.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorActions.h"

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"


#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorGraphEditor"

void SDisplayClusterConfiguratorGraphEditor::SetViewportPreviewTexture(const FString& NodeId, const FString& ViewportId, UTexture* InTexture)
{
	if (RootCanvasNode.IsValid())
	{
		for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator WindowIt(RootCanvasNode->GetChildWindows()); WindowIt; ++WindowIt)
		{
			UDisplayClusterConfiguratorWindowNode* WindowNode = *WindowIt;
			if (WindowNode->GetNodeName().Equals(NodeId))
			{
				for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator ViewportIt(WindowNode->GetChildViewports()); ViewportIt; ++ViewportIt)
				{
					UDisplayClusterConfiguratorViewportNode* ViewportNode = *ViewportIt;
					if (ViewportNode->GetNodeName().Equals(ViewportId))
					{
						ViewportNode->SetPreviewTexture(InTexture);
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
			for (UDisplayClusterConfiguratorBaseNode* SelectedNode : NewSelectedNodes)
			{
				SelectedNode->OnSelection();
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

	ForEachGraphNode([this](UDisplayClusterConfiguratorBaseNode* Node)
	{
		if (Node->IsSelected())
		{
			SetNodeSelection(Node, true);
		}
	});
}

void SDisplayClusterConfiguratorGraphEditor::OnConfigReloaded()
{
	RebuildCanvasNode();
}

void SDisplayClusterConfiguratorGraphEditor::RebuildCanvasNode()
{
	UDisplayClusterConfiguratorGraph* ConfiguratorGraph = ClusterConfiguratorGraph.Get();
	check(ConfiguratorGraph != nullptr);

	// Remove all the EdNodes from the graph
	ForEachGraphNode([=](UDisplayClusterConfiguratorBaseNode* Node)
	{
		ConfiguratorGraph->RemoveNode(Node);
	});

	// Reset the root node pointer, which allows it and all its child nodes to be GCed
	RootCanvasNode.Reset();

	if (UDisplayClusterConfigurationData* Config = ToolkitPtr.Pin()->GetConfig())
	{
		// Add Canvas node
		UDisplayClusterConfiguratorCanvasNode* RootCanvasNodePtr = NewObject<UDisplayClusterConfiguratorCanvasNode>(ConfiguratorGraph, UDisplayClusterConfiguratorCanvasNode::StaticClass(), NAME_None, RF_Transactional);
		RootCanvasNode = TStrongObjectPtr<UDisplayClusterConfiguratorCanvasNode>(RootCanvasNodePtr);
		RootCanvasNodePtr->Initialize(FString(), Config->Cluster, ToolkitPtr.Pin().ToSharedRef());
		RootCanvasNodePtr->CreateNewGuid();
		RootCanvasNodePtr->PostPlacedNewNode();

		ConfiguratorGraph->AddNode(RootCanvasNodePtr);

		int32 WindowIndex = 0;
		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& ClusterNodePair : Config->Cluster->Nodes)
		{
			UDisplayClusterConfiguratorWindowNode* WindowNode = NewObject<UDisplayClusterConfiguratorWindowNode>(ConfiguratorGraph, UDisplayClusterConfiguratorWindowNode::StaticClass(), NAME_None, RF_Transactional);
			WindowNode->Initialize(ClusterNodePair.Key, ClusterNodePair.Value, WindowIndex, ToolkitPtr.Pin().ToSharedRef());
			WindowNode->CreateNewGuid();
			WindowNode->PostPlacedNewNode();

			RootCanvasNodePtr->AddWindowNode(WindowNode);
			ConfiguratorGraph->AddNode(WindowNode);

			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportPair : ClusterNodePair.Value->Viewports)
			{
				UDisplayClusterConfiguratorViewportNode* ViewportNode = NewObject<UDisplayClusterConfiguratorViewportNode>(ConfiguratorGraph, UDisplayClusterConfiguratorViewportNode::StaticClass(), NAME_None, RF_Transactional);
				ViewportNode->Initialize(ViewportPair.Key, ViewportPair.Value, WindowNode, ToolkitPtr.Pin().ToSharedRef());
				ViewportNode->CreateNewGuid();
				ViewportNode->PostPlacedNewNode();

				WindowNode->AddViewportNode(ViewportNode);
				ConfiguratorGraph->AddNode(ViewportNode);
			}
			
			WindowIndex++;
		}

		TSharedPtr<IDisplayClusterConfiguratorViewOutputMapping> OutputMappingView = ToolkitPtr.Pin()->GetViewOutputMapping();
		if (OutputMappingView)
		{
			OutputMappingView->GetOnOutputMappingBuiltDelegate().Broadcast();
		}
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

		MenuBuilder->BeginSection("EdGraphSchemaOrganization", LOCTEXT("OrganizationHeader", "Organization"));
		{
			MenuBuilder->AddSubMenu(LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewMenuDelegate::CreateLambda([](FMenuBuilder& InSubMenuBuilder)
			{
				InSubMenuBuilder.BeginSection("EdGraphSchemaAlignment", LOCTEXT("AlignHeader", "Align"));
				{
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
					InSubMenuBuilder.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
				}
				InSubMenuBuilder.EndSection();
			}));
		}
		MenuBuilder->EndSection();

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

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesTop,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Top),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
		);

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesMiddle,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Middle),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
		);

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesBottom,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Bottom),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
		);

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesLeft,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Left),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
		);

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesCenter,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Center),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
		);

	CommandList->MapAction(
		FGraphEditorCommands::Get().AlignNodesRight,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::AlignNodes, ENodeAlignment::Right),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorGraphEditor::CanAlignNodes)
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

bool SDisplayClusterConfiguratorGraphEditor::CanAlignNodes() const
{
	// We want nodes to be alignable only when all nodes are children of the same parent. Viewport nodes should only be aligned with
	// sibling viewport nodes or their parent window node, and window nodes should only be aligned with their sibling windows.
	const FGraphPanelSelectionSet& CurrentlySelectedNodes = GetSelectedNodes();

	TSet<UDisplayClusterConfiguratorWindowNode*> WindowNodes;
	TSet<UDisplayClusterConfiguratorViewportNode*> ViewportNodes;
	TSet<UDisplayClusterConfiguratorWindowNode*> ParentNodes;

	for (auto NodeIt = CurrentlySelectedNodes.CreateConstIterator(); NodeIt; ++NodeIt)
	{
		UObject* Node = *NodeIt;
		if (UDisplayClusterConfiguratorCanvasNode* CanvasNode = Cast<UDisplayClusterConfiguratorCanvasNode>(Node))
		{
			// Canvas node cannot be aligned, as it is meant to be a stationary node.
			// Disable alignment if canvas node is among the selected nodes.
			return false;
		}
		else if (UDisplayClusterConfiguratorViewportNode* ViewportNode = Cast<UDisplayClusterConfiguratorViewportNode>(Node))
		{
			ViewportNodes.Add(ViewportNode);

			// Need to make sure all selected viewports are children of the same window node,
			// so add viewport's parent to set.
			ParentNodes.Add(ViewportNode->GetParentWindow());
		}
		else if (UDisplayClusterConfiguratorWindowNode* WindowNode = Cast<UDisplayClusterConfiguratorWindowNode>(Node))
		{
			WindowNodes.Add(WindowNode);
		}
	}

	if (ViewportNodes.Num() > 0)
	{
		if (WindowNodes.Num() > 0)
		{
			// We can only allow viewport nodes to be aligned with their parent window nodes, so only allow
			// alignment if a single window is selected, and that window is the parent to all selected viewports
			return WindowNodes.Union(ParentNodes).Num() == 1;
		}
		else
		{
			// If only viewports are selected, only allow alignment if all viewports share a parent window.
			return ParentNodes.Num() == 1;
		}
	}
	else
	{
		return true;
	}
}

void SDisplayClusterConfiguratorGraphEditor::AlignNodes(ENodeAlignment Alignment)
{
	// We need to store the node positions before the alignment so that we can propagate node position changes to the nodes.
	TArray<TTuple<UDisplayClusterConfiguratorBaseNode*, FVector2D>> NodePositions;
	const FGraphPanelSelectionSet& CurrentlySelectedNodes = GetSelectedNodes();

	// Window nodes should also update their children if they are aligned except in the case that the window node is being aligned _with_ its children.
	bool bCanUpdateChildren = true;

	for (UObject* Node : CurrentlySelectedNodes)
	{
		if (UDisplayClusterConfiguratorBaseNode* BaseNode = Cast<UDisplayClusterConfiguratorBaseNode>(Node))
		{
			NodePositions.Add(TTuple<UDisplayClusterConfiguratorBaseNode*, FVector2D>(BaseNode, BaseNode->GetNodePosition()));

			// If any of the nodes being aligned are viewport nodes, do not allow window nodes to update their children, as that can cause the viewport children to
			// be shifted out of alignment when the window nodes propagate the position change to their children.
			if (BaseNode->IsA<UDisplayClusterConfiguratorViewportNode>())
			{
				bCanUpdateChildren = false;
			}
		}
	}

	switch (Alignment)
	{
	case ENodeAlignment::Top:
		OnAlignTop();
		break;

	case ENodeAlignment::Middle:
		OnAlignMiddle();
		break;

	case ENodeAlignment::Bottom:
		OnAlignBottom();
		break;

	case ENodeAlignment::Left:
		OnAlignLeft();
		break;

	case ENodeAlignment::Center:
		OnAlignCenter();
		break;

	case ENodeAlignment::Right:
		OnAlignRight();
		break;
	}

	for (const TTuple<UDisplayClusterConfiguratorBaseNode*, FVector2D>& NodePair : NodePositions)
	{
		UDisplayClusterConfiguratorBaseNode* Node = NodePair.Key;
		FVector2D OldPosition = NodePair.Value;

		FVector2D PositionChange = Node->GetNodePosition() - OldPosition;
		Node->OnNodeAligned(PositionChange, bCanUpdateChildren);
	}
}

void SDisplayClusterConfiguratorGraphEditor::ForEachGraphNode(TFunction<void(UDisplayClusterConfiguratorBaseNode* Node)> Predicate)
{
	if (RootCanvasNode.IsValid())
	{
		Predicate(RootCanvasNode.Get());

		for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator WindowIt(RootCanvasNode->GetChildWindows()); WindowIt; ++WindowIt)
		{
			UDisplayClusterConfiguratorWindowNode* WindowNode = *WindowIt;
			Predicate(WindowNode);

			for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator ViewportIt(WindowNode->GetChildViewports()); ViewportIt; ++ViewportIt)
			{
				UDisplayClusterConfiguratorViewportNode* ViewportNode = *ViewportIt;
				Predicate(ViewportNode);
			}
		}
	}
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
	Arguments._AdditionalCommands = CommandList;
	Arguments._IsEditable = true;
	Arguments._GraphEvents = GraphEvents;

	SGraphEditor::Construct(Arguments);

	SetCanTick(true);
}

#undef LOCTEXT_NAMESPACE
