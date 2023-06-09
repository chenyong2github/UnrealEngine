// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPluginReferenceViewer.h"

#include "EdGraphNode_PluginReference.h"
#include "EdGraph_PluginReferenceViewer.h"
#include "Features/EditorFeatures.h"
#include "Features/IModularFeatures.h"
#include "Features/IPluginsEditorFeature.h"
#include "Interfaces/IPluginManager.h"
#include "PluginReferenceViewerSchema.h"
#include "PluginReferenceViewerCommands.h"
#include "ToolMenus.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "PluginReferenceViewer"

SPluginReferenceViewer::~SPluginReferenceViewer()
{
	if (!GExitPurge)
	{
		if (ensure(GraphObj))
		{
			GraphObj->RemoveFromRoot();
		}
	}
}

void SPluginReferenceViewer::Construct(const FArguments& InArgs)
{
	/** Used to delay graph rebuilding during spinbox slider interaction */
	bNeedsGraphRebuild = false;

	RegisterActions();

	// Create the graph
	GraphObj = NewObject<UEdGraph_PluginReferenceViewer>();
	GraphObj->Schema = UPluginReferenceViewerSchema::StaticClass();
	GraphObj->AddToRoot();
	GraphObj->SetPluginReferenceViewer(StaticCastSharedRef<SPluginReferenceViewer>(AsShared()));

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SPluginReferenceViewer::OnNodeDoubleClicked);

	// Create the graph editor
	GraphEditorPtr = SNew(SGraphEditor)
		.AdditionalCommands(PluginReferenceViewerActions)
		.GraphToEdit(GraphObj)
		.GraphEvents(GraphEvents)
		.ShowGraphStateOverlay(false);

	GraphObj->CachePluginDependencies(IPluginManager::Get().GetDiscoveredPlugins());

	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					MakeToolBar()
				]
			]
		]

		// Graph
		+SVerticalBox::Slot()
		.FillHeight(0.90f)
		.HAlign(HAlign_Fill)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				GraphEditorPtr.ToSharedRef()
			]

			+SOverlay::Slot()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			.Padding(8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.Padding(2.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SearchDepthReferencersLabelText", "Search Referencers Depth"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2.f)
							[
								SNew(SBox)
								.WidthOverride(100)
								[
									SAssignNew(ReferencerCountBox, SSpinBox<int32>)
									.Value(this, &SPluginReferenceViewer::GetSearchReferencerDepthCount)
									.OnValueChanged_Lambda([this] (int32 NewValue)
										{	
											if (NewValue != Settings.MaxSearchReferencersDepth)
											{
												Settings.MaxSearchReferencersDepth = NewValue;
												bNeedsGraphRebuild = true;
											}
										}
									)
									.OnValueCommitted_Lambda([this] (int32 NewValue, ETextCommit::Type CommitType) 
										{ 
											FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly); 

											if (NewValue != Settings.MaxSearchReferencersDepth || bNeedsGraphRebuild)
											{
												Settings.MaxSearchReferencersDepth = NewValue;
												bNeedsGraphRebuild = false;
												RebuildGraph();
											}
										}
									)
									.MinValue(0)
									.MaxValue(50)
									.MaxSliderValue(10)
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.Padding(2.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SearchDepthDependenciesLabelText", "Search Dependencies Depth"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2.f)
							[
								SNew(SBox)
								.WidthOverride(100)
								[
									SAssignNew(DependencyCountBox, SSpinBox<int32>)
									.Value(this, &SPluginReferenceViewer::GetSearchDependencyDepthCount)
									.OnValueChanged_Lambda([this] (int32 NewValue)
										{	
											if (NewValue != Settings.MaxSearchDependencyDepth)
											{
												Settings.MaxSearchDependencyDepth = NewValue;
												bNeedsGraphRebuild = true;
											}
										}
									)
									.OnValueCommitted_Lambda([this] (int32 NewValue, ETextCommit::Type CommitType) 
										{ 
											FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly); 

											if (NewValue != Settings.MaxSearchDependencyDepth || bNeedsGraphRebuild)
											{
												Settings.MaxSearchDependencyDepth = NewValue;
												bNeedsGraphRebuild = false;
												RebuildGraph();
											}
										}
									)
									.MinValue(0)
									.MaxValue(50)
									.MaxSliderValue(10)
								]
							]
						]
					]
				]
			]
		]
	];
}

void SPluginReferenceViewer::SetGraphRoot(const TArray<FPluginIdentifier>& GraphRootIdentifiers)
{
	GraphObj->SetGraphRoot(GraphRootIdentifiers);

	GraphObj->RebuildGraph();

	GraphEditorPtr->ZoomToFit(false);
}

void SPluginReferenceViewer::OnOpenPluginProperties()
{
	TSharedPtr<const IPlugin> Plugin;

	const FGraphPanelSelectionSet& SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	if (!SelectedNodes.IsEmpty())
	{
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_PluginReference* PluginReferenceNode = Cast<UEdGraphNode_PluginReference>(Node);
			if (PluginReferenceNode)
			{
				Plugin = PluginReferenceNode->GetPlugin();
				break;
			}
		}
	}

	if (Plugin != nullptr)
	{
		OpenPluginProperties(Plugin->GetName());
	}
}

bool SPluginReferenceViewer::HasAtLeastOneRealNodeSelected()
{
	return true;
}

void SPluginReferenceViewer::OpenPluginProperties(const FString& PluginName)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	if (Plugin != nullptr)
	{
		IPluginsEditorFeature& PluginEditor = IModularFeatures::Get().GetModularFeature<IPluginsEditorFeature>(EditorFeatures::PluginsEditor);
		PluginEditor.OpenPluginEditor(Plugin.ToSharedRef(), nullptr, FSimpleDelegate());
	}
}

TSharedRef<SWidget> SPluginReferenceViewer::MakeToolBar()
{
	FToolBarBuilder ToolBarBuilder(PluginReferenceViewerActions, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);

	ToolBarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &SPluginReferenceViewer::GetShowMenuContent),
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility"),
		/*bInSimpleComboBox*/ false);
	
	return ToolBarBuilder.MakeWidget();
}

TSharedRef<SWidget> SPluginReferenceViewer::GetShowMenuContent()
{
	FMenuBuilder MenuBuilder(true, PluginReferenceViewerActions);

	MenuBuilder.BeginSection("ViewOptions", LOCTEXT("ViewOptions", "View Options"));
	MenuBuilder.AddMenuEntry(FPluginReferenceViewerCommands::Get().CompactMode);
	MenuBuilder.AddMenuEntry(FPluginReferenceViewerCommands::Get().ShowEnginePlugins);
	MenuBuilder.AddMenuEntry(FPluginReferenceViewerCommands::Get().ShowOptionalPlugins);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SPluginReferenceViewer::RebuildGraph()
{
	GraphObj->RebuildGraph();
}

void SPluginReferenceViewer::ZoomToFit()
{
	GraphEditorPtr->ZoomToFit(true);
}

void SPluginReferenceViewer::ReCenterGraph()
{
	ReCenterGraphOnNodes(GraphEditorPtr->GetSelectedNodes());
}

void SPluginReferenceViewer::ReCenterGraphOnNodes(const TSet<UObject*>& Nodes)
{
	TArray<FPluginIdentifier> NewGraphRootNames;
	FIntPoint TotalNodePos(ForceInitToZero);
	for (auto NodeIt = Nodes.CreateConstIterator(); NodeIt; ++NodeIt)
	{
		UEdGraphNode_PluginReference* PluginReferenceNode = Cast<UEdGraphNode_PluginReference>(*NodeIt);
		if (PluginReferenceNode)
		{
			NewGraphRootNames.Add(PluginReferenceNode->GetIdentifier());
			TotalNodePos.X += PluginReferenceNode->NodePosX;
			TotalNodePos.Y += PluginReferenceNode->NodePosY;
		}
	}

	if (NewGraphRootNames.Num() > 0)
	{
		const FIntPoint AverageNodePos = TotalNodePos / NewGraphRootNames.Num();
		GraphObj->SetGraphRoot(NewGraphRootNames, AverageNodePos);
		UEdGraphNode_PluginReference* NewRootNode = GraphObj->RebuildGraph();

		if (NewRootNode != nullptr)
		{
			GraphEditorPtr->ClearSelectionSet();
			GraphEditorPtr->SetNodeSelection(NewRootNode, true);
		}
	}
}

void SPluginReferenceViewer::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	if (UEdGraphNode_PluginReference* PlugingReferenceNode = Cast<UEdGraphNode_PluginReference>(Node))
	{
		TSet<UObject*> Nodes;
		Nodes.Add(Node);
		ReCenterGraphOnNodes(Nodes);
	}
}

void SPluginReferenceViewer::RegisterActions()
{
	PluginReferenceViewerActions = MakeShareable(new FUICommandList);
	FPluginReferenceViewerCommands::Register();

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().CompactMode,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::OnCompactModeChanged),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPluginReferenceViewer::IsCompactModeChecked));

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().ShowEnginePlugins,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::OnShowEnginePluginsChanged),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPluginReferenceViewer::IsShowEnginePluginsChecked));

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().ShowOptionalPlugins,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::OnShowOptionalPluginsChanged),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPluginReferenceViewer::IsShowOptionalPluginsChecked));

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().OpenPluginProperties,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::OnOpenPluginProperties),
		FCanExecuteAction::CreateSP(this, &SPluginReferenceViewer::HasAtLeastOneRealNodeSelected));

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().ZoomToFit,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::ZoomToFit),
		FCanExecuteAction());

	PluginReferenceViewerActions->MapAction(
		FPluginReferenceViewerCommands::Get().ReCenterGraph,
		FExecuteAction::CreateSP(this, &SPluginReferenceViewer::ReCenterGraph),
		FCanExecuteAction());
}

bool SPluginReferenceViewer::IsCompactModeChecked()
{
	return Settings.bIsCompactMode;
}

void SPluginReferenceViewer::OnCompactModeChanged()
{
	Settings.bIsCompactMode = !Settings.bIsCompactMode;
	GraphObj->RebuildGraph();
}

bool SPluginReferenceViewer::IsShowEnginePluginsChecked() const
{
	return Settings.bShowEnginePlugins;
}

void SPluginReferenceViewer::OnShowEnginePluginsChanged()
{
	Settings.bShowEnginePlugins = !Settings.bShowEnginePlugins;
	GraphObj->RebuildGraph();
}

bool SPluginReferenceViewer::IsShowOptionalPluginsChecked() const
{
	return Settings.bShowOptionalPlugins;
}

void SPluginReferenceViewer::OnShowOptionalPluginsChanged()
{
	Settings.bShowOptionalPlugins = !Settings.bShowOptionalPlugins;
	GraphObj->RebuildGraph();
}

int32 SPluginReferenceViewer::GetSearchReferencerDepthCount() const
{
	return Settings.MaxSearchReferencersDepth;
}

int32 SPluginReferenceViewer::GetSearchDependencyDepthCount() const
{
	return Settings.MaxSearchDependencyDepth;
}

#undef LOCTEXT_NAMESPACE