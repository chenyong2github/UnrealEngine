// Copyright Epic Games, Inc. All Rights Reserved.

#include "SReferenceViewer.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "Dialogs/Dialogs.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "SSimpleButton.h"
#include "SSimpleComboButton.h"

#include "Styling/AppStyle.h"
#include "Engine/Selection.h"
#include "ReferenceViewer/EdGraph_ReferenceViewer.h"
#include "ReferenceViewer/EdGraphNode_Reference.h"
#include "ReferenceViewer/ReferenceViewerSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "Editor.h"
#include "AssetManagerEditorCommands.h"
#include "EditorWidgetsModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Engine/AssetManager.h"
#include "Widgets/Input/SComboBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "AssetManagerEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "ReferenceViewer"

static bool GShowToggleDeprecatedReferenceViewerLayout = false;
static FAutoConsoleCommand CVarShowToggleDeprecatedReferenceViewerLayout(
	TEXT("ReferenceViewer.ShowToggleDeprecatedLayout"),
	TEXT("Displays the toggle allowing the user to toggle back to the former layout algorithm."),
	FConsoleCommandDelegate::CreateLambda([] { GShowToggleDeprecatedReferenceViewerLayout = !GShowToggleDeprecatedReferenceViewerLayout; }));

bool IsPackageNamePassingFilter(FName InPackageName, const TArray<FString>& InSearchWords)
{
	// package name must match all words
	for (const FString& Word : InSearchWords)
	{
		if (!InPackageName.ToString().Contains(Word))
		{
			return false;
		}
	}

	return true;
}

bool IsReferenceNodePassingFilter(const UEdGraphNode_Reference& InNode, const TArray<FString>& InSearchWords)
{
	if (InSearchWords.Num() == 0)
	{
		return true;
	}

	TArray<FName> NodePackageNames;
	InNode.GetAllPackageNames(NodePackageNames);

	for (const FName& PackageName : NodePackageNames)
	{
		if (!IsPackageNamePassingFilter(PackageName, InSearchWords))
		{
			return false;
		}
	}

	return true;
}

SReferenceViewer::~SReferenceViewer()
{
	if (!GExitPurge)
	{
		if ( ensure(GraphObj) )
		{
			GraphObj->RemoveFromRoot();
		}		
	}
}

void SReferenceViewer::Construct(const FArguments& InArgs)
{
	// Create an action list and register commands
	RegisterActions();

	// Set up the history manager
	HistoryManager.SetOnApplyHistoryData(FOnApplyHistoryData::CreateSP(this, &SReferenceViewer::OnApplyHistoryData));
	HistoryManager.SetOnUpdateHistoryData(FOnUpdateHistoryData::CreateSP(this, &SReferenceViewer::OnUpdateHistoryData));

	// Create the graph
	GraphObj = NewObject<UEdGraph_ReferenceViewer>();
	GraphObj->Schema = UReferenceViewerSchema::StaticClass();
	GraphObj->AddToRoot();
	GraphObj->SetReferenceViewer(StaticCastSharedRef<SReferenceViewer>(AsShared()));

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SReferenceViewer::OnNodeDoubleClicked);
	GraphEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SReferenceViewer::OnCreateGraphActionMenu);

	// Create the graph editor
	GraphEditorPtr = SNew(SGraphEditor)
		.AdditionalCommands(ReferenceViewerActions)
		.GraphToEdit(GraphObj)
		.GraphEvents(GraphEvents)
		.ShowGraphStateOverlay(false)
		.OnNavigateHistoryBack(FSimpleDelegate::CreateSP(this, &SReferenceViewer::GraphNavigateHistoryBack))
		.OnNavigateHistoryForward(FSimpleDelegate::CreateSP(this, &SReferenceViewer::GraphNavigateHistoryForward));

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_None, FMargin(16, 8), false);

	const FAssetManagerEditorCommands& UICommands	= FAssetManagerEditorCommands::Get();

	static const FName DefaultForegroundName("DefaultForeground");

	// Visual options visibility
	FixAndHideSearchDepthLimit = 0;
	FixAndHideSearchBreadthLimit = 0;
	bShowCollectionFilter = true;
	bShowShowReferencesOptions = true;
	bShowShowSearchableNames = true;
	bShowShowNativePackages = true;
	bShowShowFilteredPackagesOnly = true;
	bShowCompactMode = true;
	bDirtyResults = false;

	ChildSlot
	[

		SNew(SVerticalBox)

		// Path and history
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(FMargin(12, 6, 12, 6))
			[
				SNew(SHorizontalBox)


				// Refresh Button
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SSimpleButton)
					.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh current view"))
					.OnClicked(this, &SReferenceViewer::RefreshClicked)
					.Icon(FAppStyle::GetBrush("Icons.Refresh"))
				]


				// History Back Button
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.ToolTipText( this, &SReferenceViewer::GetHistoryBackTooltip )
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ForegroundColor(FSlateColor::UseStyle())
					.OnClicked(this, &SReferenceViewer::BackClicked)
					.IsEnabled(this, &SReferenceViewer::IsBackEnabled)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FAppStyle::GetBrush("Icons.ArrowLeft"))
						.DesiredSizeOverride(FVector2D(20.0f, 20.0f))
					]
				]

				// History Forward Button
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.ToolTipText( this, &SReferenceViewer::GetHistoryForwardTooltip )
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ForegroundColor(FSlateColor::UseStyle())
					.OnClicked(this, &SReferenceViewer::ForwardClicked)
					.IsEnabled(this, &SReferenceViewer::IsForwardEnabled)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FAppStyle::GetBrush("Icons.ArrowRight"))
						.DesiredSizeOverride(FVector2D(20.0f, 20.0f))
					]
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SSimpleComboButton)
					.OnGetMenuContent(this, &SReferenceViewer::GetShowMenuContent)
					.Icon(FAppStyle::GetBrush("Icons.Visibility"))
					.HasDownArrow(true)
				]

				// Path
				+SHorizontalBox::Slot()
				.Padding(4, 0)
				.FillWidth(1.f)
				[
					SNew(SBorder)
					.BorderImage( FAppStyle::GetBrush("ToolPanel.GroupBorder") )
					[
						SNew(SEditableTextBox)
						.Text(this, &SReferenceViewer::GetAddressBarText)
						.OnTextCommitted(this, &SReferenceViewer::OnAddressBarTextCommitted)
						.OnTextChanged(this, &SReferenceViewer::OnAddressBarTextChanged)
						.SelectAllTextWhenFocused(true)
						.SelectAllTextOnCommit(true)
						.Style(FAppStyle::Get(), "ReferenceViewer.PathText")
					]
				]

			
			]
		]

		// Graph
		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SOverlay)

			+SOverlay::Slot()
			[
				GraphEditorPtr.ToSharedRef()
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(8)
			[
				SNew(SBorder)
				.BorderImage( FAppStyle::GetBrush("ToolPanel.GroupBorder") )
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding(2.f)
					.AutoHeight()
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("Search", "Search..."))
						.ToolTipText(LOCTEXT("SearchTooltip", "Type here to search (pressing Enter zooms to the results)"))
						.OnTextChanged(this, &SReferenceViewer::HandleOnSearchTextChanged)
						.OnTextCommitted(this, &SReferenceViewer::HandleOnSearchTextCommitted)
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (FixAndHideSearchDepthLimit > 0 ? EVisibility::Collapsed : EVisibility::Visible); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SearchDepthReferencersLabelText", "Search Referencers Depth"))
							.ToolTipText(FText::Format( LOCTEXT("ReferenceDepthToolTip", "Adjust Referencer Search Depth (+/-):  {0} / {1}\nSet Referencer Search Depth:                        {2}"),
															UICommands.IncreaseReferencerSearchDepth->GetInputText().ToUpper(),
															UICommands.DecreaseReferencerSearchDepth->GetInputText().ToUpper(),
															UICommands.SetReferencerSearchDepth->GetInputText().ToUpper()))

						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SAssignNew(ReferencerCountBox, SSpinBox<int32>)
								.Value(this, &SReferenceViewer::GetSearchReferencerDepthCount)
								.OnValueChanged(this, &SReferenceViewer::OnSearchReferencerDepthCommitted)
								.OnValueCommitted_Lambda([this] (int32 NewValue, ETextCommit::Type CommitType) { FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly); } )
								.MinValue(0)
								.MaxValue(50)
								.MaxSliderValue(12)
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (FixAndHideSearchDepthLimit > 0 ? EVisibility::Collapsed : EVisibility::Visible); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SearchDepthDependenciesLabelText", "Search Dependencies Depth"))
							.ToolTipText(FText::Format( LOCTEXT("DependencyDepthToolTip", "Adjust Dependency Search Depth (+/-):  {0} / {1}\nSet Dependency Search Depth:                        {2}"),
															UICommands.IncreaseDependencySearchDepth->GetInputText().ToUpper(),
															UICommands.DecreaseDependencySearchDepth->GetInputText().ToUpper(),
															UICommands.SetDependencySearchDepth->GetInputText().ToUpper()))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SAssignNew(DependencyCountBox, SSpinBox<int32>)
								.Value(this, &SReferenceViewer::GetSearchDependencyDepthCount)
								.OnValueChanged(this, &SReferenceViewer::OnSearchDependencyDepthCommitted)
								.OnValueCommitted_Lambda([this] (int32 NewValue, ETextCommit::Type CommitType) { FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly); } )
								.MinValue(0)
								.MaxValue(50)
								.MaxSliderValue(12)
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (FixAndHideSearchBreadthLimit > 0 ? EVisibility::Collapsed : EVisibility::Visible); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SearchBreadthLabelText", "Search Breadth Limit"))
							.ToolTipText(FText::Format( LOCTEXT("BreadthLimitToolTip", "Adjust Breadth Limit (+/-):  {0} / {1}\nSet Breadth Limit:                        {2}"),
															UICommands.IncreaseBreadth->GetInputText().ToUpper(),
															UICommands.DecreaseBreadth->GetInputText().ToUpper(),
															UICommands.SetBreadth->GetInputText().ToUpper()))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnSearchBreadthEnabledChanged )
							.IsChecked( this, &SReferenceViewer::IsSearchBreadthEnabledChecked )
						]
					
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SAssignNew(BreadthLimitBox, SSpinBox<int32>)
								.Value(this, &SReferenceViewer::GetSearchBreadthCount)
								.OnValueChanged(this, &SReferenceViewer::OnSearchBreadthCommitted)
								.OnValueCommitted_Lambda([this] (int32 NewValue, ETextCommit::Type CommitType) { FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly); } )
								.MinValue(1)
								.MaxValue(1000)
								.MaxSliderValue(50)
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowCollectionFilter ? EVisibility::Visible : EVisibility::Collapsed); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CollectionFilter", "Collection Filter"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnEnableCollectionFilterChanged )
							.IsChecked( this, &SReferenceViewer::IsEnableCollectionFilterChecked )
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SAssignNew(CollectionsCombo, SComboBox<TSharedPtr<FName>>)
								.OptionsSource(&CollectionsComboList)
								.OnComboBoxOpening(this, &SReferenceViewer::UpdateCollectionsComboList)
								.OnGenerateWidget(this, &SReferenceViewer::GenerateCollectionFilterItem)
								.OnSelectionChanged(this, &SReferenceViewer::HandleCollectionFilterChanged)
								.ToolTipText(this, &SReferenceViewer::GetCollectionFilterText)
								[
									SNew(STextBlock)
									.Text(this, &SReferenceViewer::GetCollectionFilterText)
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([]() { return (GShowToggleDeprecatedReferenceViewerLayout ? EVisibility::Visible : EVisibility::Collapsed); })

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("UseOldLayoutMechanism", "Use Deprecated Layout"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged_Lambda( [this] (ECheckBoxState NewState) { 
								if (GraphObj) 
								{ 	
									GraphObj->SetUseNodeInfos(NewState != ECheckBoxState::Checked); 
									RebuildGraph();} 
								} 
							)
							.IsChecked_Lambda([this] () 
							{
								return (GraphObj && GraphObj->GetUseNodeInfos()) ? ECheckBoxState::Unchecked: ECheckBoxState::Checked;
							})
						]
					]
				]
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(24, 0, 24, 0))
			[
				AssetDiscoveryIndicator
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(0, 0, 0, 16))
			[
				SNew(STextBlock)
				.Text(this, &SReferenceViewer::GetStatusText)
			]
		]
	];

	UpdateCollectionsComboList();
}

FReply SReferenceViewer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return ReferenceViewerActions->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

void SReferenceViewer::SetGraphRootIdentifiers(const TArray<FAssetIdentifier>& NewGraphRootIdentifiers, const FReferenceViewerParams& ReferenceViewerParams)
{
	GraphObj->SetGraphRoot(NewGraphRootIdentifiers);
	// Set properties
	GraphObj->SetShowReferencers(ReferenceViewerParams.bShowReferencers);
	GraphObj->SetShowDependencies(ReferenceViewerParams.bShowDependencies);
	// Set user-interactive properties
	FixAndHideSearchDepthLimit = ReferenceViewerParams.FixAndHideSearchDepthLimit;
	if (FixAndHideSearchDepthLimit > 0)
	{
		GraphObj->SetSearchDependencyDepthLimit(FixAndHideSearchDepthLimit);
		GraphObj->SetSearchReferencerDepthLimit(FixAndHideSearchDepthLimit);
		GraphObj->SetSearchDepthLimitEnabled(true);
	}
	FixAndHideSearchBreadthLimit = ReferenceViewerParams.FixAndHideSearchBreadthLimit;
	if (FixAndHideSearchBreadthLimit > 0)
	{
		GraphObj->SetSearchBreadthLimit(FixAndHideSearchBreadthLimit);
		GraphObj->SetSearchBreadthLimitEnabled(true);
	}
	bShowCollectionFilter = ReferenceViewerParams.bShowCollectionFilter;
	bShowShowReferencesOptions = ReferenceViewerParams.bShowShowReferencesOptions;
	bShowShowSearchableNames = ReferenceViewerParams.bShowShowSearchableNames;
	bShowShowNativePackages = ReferenceViewerParams.bShowShowNativePackages;

	bShowShowFilteredPackagesOnly = ReferenceViewerParams.bShowShowFilteredPackagesOnly;
	if (ReferenceViewerParams.bShowFilteredPackagesOnly.IsSet())
	{
		GraphObj->SetShowFilteredPackagesOnlyEnabled(ReferenceViewerParams.bShowFilteredPackagesOnly.GetValue());
	}
	UpdateIsPassingFilterPackageCallback();

	bShowCompactMode = ReferenceViewerParams.bShowCompactMode;
	if (ReferenceViewerParams.bCompactMode.IsSet())
	{
		GraphObj->SetCompactModeEnabled(ReferenceViewerParams.bCompactMode.GetValue());
	}

	RebuildGraph();

	// Zoom once this frame to make sure widgets are visible, then zoom again so size is correct
	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceViewer::TriggerZoomToFit));

	// Set the initial history data
	HistoryManager.AddHistoryData();
}

EActiveTimerReturnType SReferenceViewer::TriggerZoomToFit(double InCurrentTime, float InDeltaTime)
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(false);
	}
	return EActiveTimerReturnType::Stop;
}

void SReferenceViewer::SetCurrentRegistrySource(const FAssetManagerEditorRegistrySource* RegistrySource)
{
	RebuildGraph();
}

void SReferenceViewer::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	TSet<UObject*> Nodes;
	Nodes.Add(Node);
	ReCenterGraphOnNodes( Nodes );
}

void SReferenceViewer::RebuildGraph()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		// We are still discovering assets, listen for the completion delegate before building the graph
		if (!AssetRegistryModule.Get().OnFilesLoaded().IsBoundToObject(this))
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddSP(this, &SReferenceViewer::OnInitialAssetRegistrySearchComplete);
		}
	}
	else
	{
		// All assets are already discovered, build the graph now, if we have one
		if (GraphObj)
		{
			GraphObj->RebuildGraph();
		}

		bDirtyResults = false;
		if (!AssetRefreshHandle.IsValid())
		{
			// Listen for updates
			AssetRefreshHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
		}
	}
}

FActionMenuContent SReferenceViewer::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	// no context menu when not over a node
	return FActionMenuContent();
}

bool SReferenceViewer::IsBackEnabled() const
{
	return HistoryManager.CanGoBack();
}

bool SReferenceViewer::IsForwardEnabled() const
{
	return HistoryManager.CanGoForward();
}

FReply SReferenceViewer::BackClicked()
{
	HistoryManager.GoBack();

	return FReply::Handled();
}

FReply SReferenceViewer::ForwardClicked()
{
	HistoryManager.GoForward();

	return FReply::Handled();
}

FReply SReferenceViewer::RefreshClicked()
{
	RebuildGraph();
	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceViewer::TriggerZoomToFit));

	return FReply::Handled();
}

void SReferenceViewer::GraphNavigateHistoryBack()
{
	BackClicked();
}

void SReferenceViewer::GraphNavigateHistoryForward()
{
	ForwardClicked();
}

FText SReferenceViewer::GetHistoryBackTooltip() const
{
	if ( HistoryManager.CanGoBack() )
	{
		return FText::Format( LOCTEXT("HistoryBackTooltip", "Back to {0}"), HistoryManager.GetBackDesc() );
	}
	return FText::GetEmpty();
}

FText SReferenceViewer::GetHistoryForwardTooltip() const
{
	if ( HistoryManager.CanGoForward() )
	{
		return FText::Format( LOCTEXT("HistoryForwardTooltip", "Forward to {0}"), HistoryManager.GetForwardDesc() );
	}
	return FText::GetEmpty();
}

FText SReferenceViewer::GetAddressBarText() const
{
	if ( GraphObj )
	{
		if (TemporaryPathBeingEdited.IsEmpty())
		{
			const TArray<FAssetIdentifier>& CurrentGraphRootPackageNames = GraphObj->GetCurrentGraphRootIdentifiers();
			if (CurrentGraphRootPackageNames.Num() == 1)
			{
				return FText::FromString(CurrentGraphRootPackageNames[0].ToString());
			}
			else if (CurrentGraphRootPackageNames.Num() > 1)
			{
				return FText::Format(LOCTEXT("AddressBarMultiplePackagesText", "{0} and {1} others"), FText::FromString(CurrentGraphRootPackageNames[0].ToString()), FText::AsNumber(CurrentGraphRootPackageNames.Num()));
			}
		}
		else
		{
			return TemporaryPathBeingEdited;
		}
	}

	return FText();
}

FText SReferenceViewer::GetStatusText() const
{
	FString DirtyPackages;
	if (GraphObj)
	{
		const TArray<FAssetIdentifier>& CurrentGraphRootPackageNames = GraphObj->GetCurrentGraphRootIdentifiers();
		
		for (const FAssetIdentifier& CurrentAsset : CurrentGraphRootPackageNames)
		{
			if (CurrentAsset.IsPackage())
			{
				FString PackageString = CurrentAsset.PackageName.ToString();
				UPackage* InMemoryPackage = FindPackage(nullptr, *PackageString);
				if (InMemoryPackage && InMemoryPackage->IsDirty())
				{
					DirtyPackages += FPackageName::GetShortName(*PackageString);

					// Break on first modified asset to avoid string going too long, the multi select case is fairly rare
					break;
				}
			}
		}
	}

	if (DirtyPackages.Len() > 0)
	{
		return FText::Format(LOCTEXT("ModifiedWarning", "Showing old saved references for edited asset {0}"), FText::FromString(DirtyPackages));
	}

	if (bDirtyResults)
	{
		return LOCTEXT("DirtyWarning", "Saved references changed, refresh for update");
	}

	return FText();
}

void SReferenceViewer::OnAddressBarTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		TArray<FAssetIdentifier> NewPaths;
		NewPaths.Add(FAssetIdentifier::FromString(NewText.ToString()));

		SetGraphRootIdentifiers(NewPaths);
	}

	TemporaryPathBeingEdited = FText();
}

void SReferenceViewer::OnAddressBarTextChanged(const FText& NewText)
{
	TemporaryPathBeingEdited = NewText;
}

void SReferenceViewer::OnApplyHistoryData(const FReferenceViewerHistoryData& History)
{
	if ( GraphObj )
	{
		GraphObj->SetGraphRoot(History.Identifiers);
		UEdGraphNode_Reference* NewRootNode = GraphObj->RebuildGraph();
		
		if ( NewRootNode && ensure(GraphEditorPtr.IsValid()) )
		{
			GraphEditorPtr->SetNodeSelection(NewRootNode, true);
		}
	}
}

void SReferenceViewer::OnUpdateHistoryData(FReferenceViewerHistoryData& HistoryData) const
{
	if ( GraphObj )
	{
		const TArray<FAssetIdentifier>& CurrentGraphRootIdentifiers = GraphObj->GetCurrentGraphRootIdentifiers();
		HistoryData.HistoryDesc = GetAddressBarText();
		HistoryData.Identifiers = CurrentGraphRootIdentifiers;
	}
	else
	{
		HistoryData.HistoryDesc = FText::GetEmpty();
		HistoryData.Identifiers.Empty();
	}
}

void SReferenceViewer::OnSearchDepthEnabledChanged( ECheckBoxState NewState )
{
	if ( GraphObj )
	{
		GraphObj->SetSearchDepthLimitEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsSearchDepthEnabledChecked() const
{
	if ( GraphObj )
	{
		return GraphObj->IsSearchDepthLimited() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

int32 SReferenceViewer::GetSearchDependencyDepthCount() const
{
	if ( GraphObj )
	{
		return GraphObj->GetSearchDependencyDepthLimit();
	}
	else
	{
		return 0;
	}
}

int32 SReferenceViewer::GetSearchReferencerDepthCount() const
{
	if ( GraphObj )
	{
		return GraphObj->GetSearchReferencerDepthLimit();
	}
	else
	{
		return 0;
	}
}

void SReferenceViewer::OnSearchDependencyDepthCommitted(int32 NewValue)
{
	if ( GraphObj )
	{
		GraphObj->SetSearchDependencyDepthLimit(NewValue);
		RebuildGraph();
	}
}

void SReferenceViewer::OnSearchReferencerDepthCommitted(int32 NewValue)
{
	if ( GraphObj )
	{
		GraphObj->SetSearchReferencerDepthLimit(NewValue);
		RebuildGraph();
	}
}

void SReferenceViewer::OnSearchBreadthEnabledChanged( ECheckBoxState NewState )
{
	if ( GraphObj )
	{
		GraphObj->SetSearchBreadthLimitEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsSearchBreadthEnabledChecked() const
{
	if ( GraphObj )
	{
		return GraphObj->IsSearchBreadthLimited() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

TSharedRef<SWidget> SReferenceViewer::GenerateCollectionFilterItem(TSharedPtr<FName> InItem)
{
	FText ItemAsText = FText::FromName(*InItem);
	return
		SNew(SBox)
		.WidthOverride(300)
		[
			SNew(STextBlock)
			.Text(ItemAsText)
			.ToolTipText(ItemAsText)
		];
}

void SReferenceViewer::OnEnableCollectionFilterChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		const bool bNewValue = NewState == ECheckBoxState::Checked;
		const bool bCurrentValue = GraphObj->GetEnableCollectionFilter();
		if (bCurrentValue != bNewValue)
		{
			GraphObj->SetEnableCollectionFilter(NewState == ECheckBoxState::Checked);
			RebuildGraph();
		}
	}
}

ECheckBoxState SReferenceViewer::IsEnableCollectionFilterChecked() const
{
	if (GraphObj)
	{
		return GraphObj->GetEnableCollectionFilter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::UpdateCollectionsComboList()
{
	TArray<FName> CollectionNames;
	{
		FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

		TArray<FCollectionNameType> AllCollections;
		CollectionManagerModule.Get().GetCollections(AllCollections);

		for (const FCollectionNameType& Collection : AllCollections)
		{
			ECollectionStorageMode::Type StorageMode = ECollectionStorageMode::Static;
			CollectionManagerModule.Get().GetCollectionStorageMode(Collection.Name, Collection.Type, StorageMode);

			if (StorageMode == ECollectionStorageMode::Static)
			{
				CollectionNames.AddUnique(Collection.Name);
			}
		}
	}
	CollectionNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	CollectionsComboList.Reset();
	CollectionsComboList.Add(MakeShared<FName>(NAME_None));
	for (FName CollectionName : CollectionNames)
	{
		CollectionsComboList.Add(MakeShared<FName>(CollectionName));
	}

	if (CollectionsCombo)
	{
		CollectionsCombo->ClearSelection();
		CollectionsCombo->RefreshOptions();

		if (GraphObj)
		{
			const FName CurrentFilter = GraphObj->GetCurrentCollectionFilter();

			const int32 SelectedItemIndex = CollectionsComboList.IndexOfByPredicate([CurrentFilter](const TSharedPtr<FName>& InItem)
			{
				return CurrentFilter == *InItem;
			});

			if (SelectedItemIndex != INDEX_NONE)
			{
				CollectionsCombo->SetSelectedItem(CollectionsComboList[SelectedItemIndex]);
			}
		}
	}
}

void SReferenceViewer::HandleCollectionFilterChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	if (GraphObj && Item)
	{
		const FName NewFilter = *Item;
		const FName CurrentFilter = GraphObj->GetCurrentCollectionFilter();
		if (CurrentFilter != NewFilter)
		{
			if (CurrentFilter == NAME_None)
			{
				// Automatically check the box to enable the filter if the previous filter was None
				GraphObj->SetEnableCollectionFilter(true);
			}

			GraphObj->SetCurrentCollectionFilter(NewFilter);
			RebuildGraph();
		}
	}
}

FText SReferenceViewer::GetCollectionFilterText() const
{
	return FText::FromName(GraphObj->GetCurrentCollectionFilter());
}

void SReferenceViewer::OnShowSoftReferencesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowSoftReferencesEnabled( !GraphObj->IsShowSoftReferences() );
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowSoftReferencesChecked() const
{
	return GraphObj ? GraphObj->IsShowSoftReferences() : false;
}

void SReferenceViewer::OnShowHardReferencesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowHardReferencesEnabled(!GraphObj->IsShowHardReferences());
		RebuildGraph();
	}
}


bool SReferenceViewer::IsShowHardReferencesChecked() const
{
	return GraphObj ? GraphObj->IsShowHardReferences() : false;
}

void SReferenceViewer::OnShowFilteredPackagesOnlyChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowFilteredPackagesOnlyEnabled(!GraphObj->IsShowFilteredPackagesOnly());
	}
	UpdateIsPassingFilterPackageCallback();
}


bool SReferenceViewer::IsShowFilteredPackagesOnlyChecked() const
{
	return GraphObj ? GraphObj->IsShowFilteredPackagesOnly() : false;
}

void SReferenceViewer::UpdateIsPassingFilterPackageCallback()
{
	if (GraphObj)
	{
		TOptional<UEdGraph_ReferenceViewer::FIsPackageNamePassingFilterCallback> IsAssetPassingFilterCallback;
		FString SearchString = SearchBox->GetText().ToString();
		TArray<FString> SearchWords;
		SearchString.ParseIntoArrayWS(SearchWords);
		{
			if (GraphObj->IsShowFilteredPackagesOnly())
			{
				if (SearchWords.Num() > 0)
				{
					IsAssetPassingFilterCallback = [=](FName InName) { return IsPackageNamePassingFilter(InName, SearchWords); };
				}
			}

			GraphObj->SetIsPackageNamePassingFilterCallback(IsAssetPassingFilterCallback);
		}
		RebuildGraph();
	}
}

void SReferenceViewer::OnCompactModeChanged()
{
	if (GraphObj)
	{
		GraphObj->SetCompactModeEnabled(!GraphObj->IsCompactMode());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsCompactModeChecked() const
{
	return GraphObj ? GraphObj->IsCompactMode() : false;
}

void SReferenceViewer::OnShowDuplicatesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowDuplicatesEnabled(!GraphObj->IsShowDuplicates());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowDuplicatesChecked() const
{
	return GraphObj ? GraphObj->IsShowDuplicates() : false;
}

void SReferenceViewer::OnShowEditorOnlyReferencesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowEditorOnlyReferencesEnabled(!GraphObj->IsShowEditorOnlyReferences());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowEditorOnlyReferencesChecked() const
{
	return GraphObj ? GraphObj->IsShowEditorOnlyReferences() : false;
}


bool SReferenceViewer::GetManagementReferencesVisibility() const
{
	return bShowShowReferencesOptions && UAssetManager::IsValid();
}

void SReferenceViewer::OnShowManagementReferencesChanged()
{
	if (GraphObj)
	{
		// This can take a few seconds if it isn't ready
		UAssetManager::Get().UpdateManagementDatabase();

		GraphObj->SetShowManagementReferencesEnabled(!GraphObj->IsShowManagementReferences());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowManagementReferencesChecked() const
{
	return GraphObj ? GraphObj->IsShowManagementReferences() : false;
}

void SReferenceViewer::OnShowSearchableNamesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowSearchableNames(!GraphObj->IsShowSearchableNames());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowSearchableNamesChecked() const
{
	return GraphObj ? GraphObj->IsShowSearchableNames() : false;
}

void SReferenceViewer::OnShowNativePackagesChanged()
{
	if (GraphObj)
	{
		GraphObj->SetShowNativePackages(!GraphObj->IsShowNativePackages());
		RebuildGraph();
	}
}

bool SReferenceViewer::IsShowNativePackagesChecked() const
{
	return GraphObj ? GraphObj->IsShowNativePackages() : false;
}

int32 SReferenceViewer::GetSearchBreadthCount() const
{
	return GraphObj ? GraphObj->GetSearchBreadthLimit() : 0;
}

void SReferenceViewer::OnSearchBreadthCommitted(int32 NewValue)
{
	if ( GraphObj )
	{
		GraphObj->SetSearchBreadthLimit(NewValue);
		RebuildGraph();
	}
}

void SReferenceViewer::RegisterActions()
{
	ReferenceViewerActions = MakeShareable(new FUICommandList);
	FAssetManagerEditorCommands::Register();

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ZoomToFit,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ZoomToFit),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::CanZoomToFit));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().Find,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnFind));

	ReferenceViewerActions->MapAction(
		FGlobalEditorCommonCommands::Get().FindInContentBrowser,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowSelectionInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().OpenSelectedInAssetEditor,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OpenSelectedInAssetEditor),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ReCenterGraph,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ReCenterGraph),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().IncreaseReferencerSearchDepth,
		FExecuteAction::CreateLambda( [this] { OnSearchReferencerDepthCommitted( GetSearchReferencerDepthCount() + 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().DecreaseReferencerSearchDepth,
		FExecuteAction::CreateLambda( [this] { OnSearchReferencerDepthCommitted( GetSearchReferencerDepthCount() - 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().SetReferencerSearchDepth,
		FExecuteAction::CreateLambda( [this] { FSlateApplication::Get().SetKeyboardFocus(ReferencerCountBox, EFocusCause::SetDirectly); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().IncreaseDependencySearchDepth,
		FExecuteAction::CreateLambda( [this] { OnSearchDependencyDepthCommitted( GetSearchDependencyDepthCount() + 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().DecreaseDependencySearchDepth,
		FExecuteAction::CreateLambda( [this] { OnSearchDependencyDepthCommitted( GetSearchDependencyDepthCount() - 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().SetDependencySearchDepth,
		FExecuteAction::CreateLambda( [this] { FSlateApplication::Get().SetKeyboardFocus(DependencyCountBox, EFocusCause::SetDirectly); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().IncreaseBreadth,
		FExecuteAction::CreateLambda( [this] { OnSearchBreadthCommitted( GetSearchBreadthCount() + 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().DecreaseBreadth,
		FExecuteAction::CreateLambda( [this] { OnSearchBreadthCommitted( GetSearchBreadthCount() - 1); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().SetBreadth,
		FExecuteAction::CreateLambda( [this] { FSlateApplication::Get().SetKeyboardFocus(BreadthLimitBox, EFocusCause::SetDirectly); } ),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowSoftReferences,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowSoftReferencesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowSoftReferencesChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowReferencesOptions; }));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowHardReferences,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowHardReferencesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowHardReferencesChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowReferencesOptions; }));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowEditorOnlyReferences,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowEditorOnlyReferencesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowEditorOnlyReferencesChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowReferencesOptions; }));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowManagementReferences,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowManagementReferencesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowManagementReferencesChecked),
		FIsActionButtonVisible::CreateSP(this, &SReferenceViewer::GetManagementReferencesVisibility));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowGamePlayTags,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowSearchableNamesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowSearchableNamesChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowSearchableNames; }));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowNativePackages,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowNativePackagesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowNativePackagesChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowNativePackages; }));


	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowDuplicates,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowDuplicatesChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowDuplicatesChecked));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().CompactMode,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnCompactModeChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsCompactModeChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowCompactMode; }));


	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().FilterSearch,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnShowFilteredPackagesOnlyChanged),
		FCanExecuteAction(),	
		FIsActionChecked::CreateSP(this, &SReferenceViewer::IsShowFilteredPackagesOnlyChecked),
		FIsActionButtonVisible::CreateLambda([this] { return bShowShowFilteredPackagesOnly; }) );

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().CopyReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::CopyReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().CopyReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::CopyReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeLocalCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Local, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakePrivateCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Private, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeSharedCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Shared, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeLocalCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Local, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakePrivateCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Private, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeSharedCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Shared, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferenceTree,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferenceTree),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ViewSizeMap,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ViewSizeMap),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ViewAssetAudit,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ViewAssetAudit),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));
}

void SReferenceViewer::ShowSelectionInContentBrowser()
{
	TArray<FAssetData> AssetList;

	// Build up a list of selected assets from the graph selection set
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*It))
		{
			if (ReferenceNode->GetAssetData().IsValid())
			{
				AssetList.Add(ReferenceNode->GetAssetData());
			}
		}
	}

	if (AssetList.Num() > 0)
	{
		GEditor->SyncBrowserToObjects(AssetList);
	}
}

void SReferenceViewer::OpenSelectedInAssetEditor()
{
	TArray<FAssetIdentifier> IdentifiersToEdit;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*It))
		{
			if (!ReferenceNode->IsCollapsed())
			{
				ReferenceNode->GetAllIdentifiers(IdentifiersToEdit);
			}
		}
	}

	// This will handle packages as well as searchable names if other systems register
	FEditorDelegates::OnEditAssetIdentifiers.Broadcast(IdentifiersToEdit);
}

void SReferenceViewer::ReCenterGraph()
{
	ReCenterGraphOnNodes( GraphEditorPtr->GetSelectedNodes() );
}

FString SReferenceViewer::GetReferencedObjectsList() const
{
	FString ReferencedObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
			
			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencedObjectsList += FString::Printf(TEXT("[%s - Dependencies]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencedObjectsList;
}

FString SReferenceViewer::GetReferencingObjectsList() const
{
	FString ReferencingObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencingObjectsList += FString::Printf(TEXT("[%s - Referencers]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencingObjectsList;
}

void SReferenceViewer::CopyReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencedObjectsList);
}

void SReferenceViewer::CopyReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencingObjectsList);
}

void SReferenceViewer::ShowReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencedObjectsDlgTitle", "Referenced Objects"), SNew(STextBlock).Text(FText::FromString(ReferencedObjectsList)));
}

void SReferenceViewer::ShowReferencingObjects()
{	
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencingObjectsDlgTitle", "Referencing Objects"), SNew(STextBlock).Text(FText::FromString(ReferencingObjectsList)));
}

void SReferenceViewer::MakeCollectionWithReferencersOrDependencies(ECollectionShareType::Type ShareType, bool bReferencers)
{
	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		if (ensure(ShareType != ECollectionShareType::CST_All))
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			FText CollectionNameAsText;
			FString FirstAssetName = FPackageName::GetLongPackageAssetName(AllSelectedPackageNames.Array()[0].ToString());
			if (bReferencers)
			{
				if (AllSelectedPackageNames.Num() > 1)
				{
					CollectionNameAsText = FText::Format(LOCTEXT("ReferencersForMultipleAssetNames", "{0}AndOthers_Referencers"), FText::FromString(FirstAssetName));
				}
				else
				{
					CollectionNameAsText = FText::Format(LOCTEXT("ReferencersForSingleAsset", "{0}_Referencers"), FText::FromString(FirstAssetName));
				}
			}
			else
			{
				if (AllSelectedPackageNames.Num() > 1)
				{
					CollectionNameAsText = FText::Format(LOCTEXT("DependenciesForMultipleAssetNames", "{0}AndOthers_Dependencies"), FText::FromString(FirstAssetName));
				}
				else
				{
					CollectionNameAsText = FText::Format(LOCTEXT("DependenciesForSingleAsset", "{0}_Dependencies"), FText::FromString(FirstAssetName));
				}
			}

			FName CollectionName;
			CollectionManagerModule.Get().CreateUniqueCollectionName(*CollectionNameAsText.ToString(), ShareType, CollectionName);

			FText ResultsMessage;
			
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FName> PackageNamesToAddToCollection;
			if (bReferencers)
			{
				for (FName SelectedPackage : AllSelectedPackageNames)
				{
					AssetRegistryModule.Get().GetReferencers(SelectedPackage, PackageNamesToAddToCollection);
				}
			}
			else
			{
				for (FName SelectedPackage : AllSelectedPackageNames)
				{
					AssetRegistryModule.Get().GetDependencies(SelectedPackage, PackageNamesToAddToCollection);
				}
			}

			TSet<FName> PackageNameSet;
			for (FName PackageToAdd : PackageNamesToAddToCollection)
			{
				if (!AllSelectedPackageNames.Contains(PackageToAdd))
				{
					PackageNameSet.Add(PackageToAdd);
				}
			}

			IAssetManagerEditorModule::Get().WriteCollection(CollectionName, ShareType, PackageNameSet.Array(), true);
		}
	}
}

void SReferenceViewer::ShowReferenceTree()
{
	UObject* SelectedObject = GetObjectFromSingleSelectedNode();

	if ( SelectedObject )
	{
		bool bObjectWasSelected = false;
		for (FSelectionIterator It(*GEditor->GetSelectedObjects()) ; It; ++It)
		{
			if ( (*It) == SelectedObject )
			{
				GEditor->GetSelectedObjects()->Deselect( SelectedObject );
				bObjectWasSelected = true;
			}
		}

		ObjectTools::ShowReferenceGraph( SelectedObject );

		if ( bObjectWasSelected )
		{
			GEditor->GetSelectedObjects()->Select( SelectedObject );
		}
	}
}

void SReferenceViewer::ViewSizeMap()
{
	TArray<FAssetIdentifier> AssetIdentifiers;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (UObject* Node : SelectedNodes)
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
		if (ReferenceNode)
		{
			ReferenceNode->GetAllIdentifiers(AssetIdentifiers);
		}
	}

	if (AssetIdentifiers.Num() > 0)
	{
		IAssetManagerEditorModule::Get().OpenSizeMapUI(AssetIdentifiers);
	}
}

void SReferenceViewer::ViewAssetAudit()
{
	TSet<FName> SelectedAssetPackageNames;
	GetPackageNamesFromSelectedNodes(SelectedAssetPackageNames);

	if (SelectedAssetPackageNames.Num() > 0)
	{
		IAssetManagerEditorModule::Get().OpenAssetAuditUI(SelectedAssetPackageNames.Array());
	}
}

void SReferenceViewer::ReCenterGraphOnNodes(const TSet<UObject*>& Nodes)
{
	TArray<FAssetIdentifier> NewGraphRootNames;
	FIntPoint TotalNodePos(ForceInitToZero);
	for ( auto NodeIt = Nodes.CreateConstIterator(); NodeIt; ++NodeIt )
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*NodeIt);
		if ( ReferenceNode )
		{
			ReferenceNode->GetAllIdentifiers(NewGraphRootNames);
			TotalNodePos.X += ReferenceNode->NodePosX;
			TotalNodePos.Y += ReferenceNode->NodePosY;
		}
	}

	if ( NewGraphRootNames.Num() > 0 )
	{
		const FIntPoint AverageNodePos = TotalNodePos / NewGraphRootNames.Num();
		GraphObj->SetGraphRoot(NewGraphRootNames, AverageNodePos);
		UEdGraphNode_Reference* NewRootNode = GraphObj->RebuildGraph();

		if ( NewRootNode && ensure(GraphEditorPtr.IsValid()) )
		{
			GraphEditorPtr->ClearSelectionSet();
			GraphEditorPtr->SetNodeSelection(NewRootNode, true);
		}

		// Set the initial history data
		HistoryManager.AddHistoryData();
	}
}

UObject* SReferenceViewer::GetObjectFromSingleSelectedNode() const
{
	UObject* ReturnObject = nullptr;

	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	if ( ensure(SelectedNodes.Num()) == 1 )
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(SelectedNodes.Array()[0]);
		if ( ReferenceNode )
		{
			const FAssetData& AssetData = ReferenceNode->GetAssetData();
			if (AssetData.IsAssetLoaded())
			{
				ReturnObject = AssetData.GetAsset();
			}
			else
			{
				FScopedSlowTask SlowTask(0, LOCTEXT("LoadingSelectedObject", "Loading selection..."));
				SlowTask.MakeDialog();
				ReturnObject = AssetData.GetAsset();
			}
		}
	}

	return ReturnObject;
}

void SReferenceViewer::GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const
{
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (UObject* Node : SelectedNodes)
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
		if (ReferenceNode)
		{
			TArray<FName> NodePackageNames;
			ReferenceNode->GetAllPackageNames(NodePackageNames);
			OutNames.Append(NodePackageNames);
		}
	}
}

bool SReferenceViewer::HasExactlyOneNodeSelected() const
{
	if ( GraphEditorPtr.IsValid() )
	{
		return GraphEditorPtr->GetSelectedNodes().Num() == 1;
	}
	
	return false;
}

bool SReferenceViewer::HasExactlyOnePackageNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		if (GraphEditorPtr->GetSelectedNodes().Num() != 1)
		{
			return false;
		}

		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool SReferenceViewer::HasAtLeastOnePackageNodeSelected() const
{
	if ( GraphEditorPtr.IsValid() )
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
		}
	}
	
	return false;
}

bool SReferenceViewer::HasAtLeastOneRealNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (!ReferenceNode->IsCollapsed())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void SReferenceViewer::OnAssetRegistryChanged(const FAssetData& AssetData)
{
	// We don't do more specific checking because that data is not exposed, and it wouldn't handle newly added references anyway
	bDirtyResults = true;
}

void SReferenceViewer::OnInitialAssetRegistrySearchComplete()
{
	if ( GraphObj )
	{
		GraphObj->RebuildGraph();
	}
}

void SReferenceViewer::ZoomToFit()
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(true);
	}
}

bool SReferenceViewer::CanZoomToFit() const
{
	if (GraphEditorPtr.IsValid())
	{
		return true;
	}

	return false;
}

void SReferenceViewer::OnFind()
{
	FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
}

void SReferenceViewer::HandleOnSearchTextChanged(const FText& SearchText)
{
	if (GraphObj == nullptr || !GraphEditorPtr.IsValid())
	{
		return;
	}

	GraphEditorPtr->ClearSelectionSet();

	UpdateIsPassingFilterPackageCallback();

	if (SearchText.IsEmpty())
	{
		return;
	}

	FString SearchString = SearchText.ToString();
	TArray<FString> SearchWords;
	SearchString.ParseIntoArrayWS( SearchWords );

	TArray<UEdGraphNode_Reference*> AllNodes;
	GraphObj->GetNodesOfClass<UEdGraphNode_Reference>( AllNodes );

	for (UEdGraphNode_Reference* Node : AllNodes)
	{
		if (IsReferenceNodePassingFilter(*Node, SearchWords))
		{
			GraphEditorPtr->SetNodeSelection(Node, true);
		}
	}
}

void SReferenceViewer::HandleOnSearchTextCommitted(const FText& SearchText, ETextCommit::Type CommitType)
{
	if (!GraphEditorPtr.IsValid())
	{
		return;
	}

	if (CommitType == ETextCommit::OnCleared)
	{
		GraphEditorPtr->ClearSelectionSet();
	}
	else if (CommitType == ETextCommit::OnEnter)
	{
		HandleOnSearchTextChanged(SearchBox->GetText());
	}
	
	GraphEditorPtr->ZoomToFit(true);
	FSlateApplication::Get().SetKeyboardFocus(GraphEditorPtr, EFocusCause::SetDirectly);
}

TSharedRef<SWidget> SReferenceViewer::GetShowMenuContent()
{
 	FMenuBuilder MenuBuilder(true, ReferenceViewerActions);

	MenuBuilder.BeginSection("ReferenceTypes", LOCTEXT("ReferenceTypes", "Reference Types"));
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowSoftReferences);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowHardReferences);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowEditorOnlyReferences);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Assets", LOCTEXT("Assets", "Assets"));
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowManagementReferences);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowGamePlayTags);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowNativePackages);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("ViewOptions", LOCTEXT("ViewOptions", "View Options"));
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().ShowDuplicates);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().FilterSearch);
	MenuBuilder.AddMenuEntry(FAssetManagerEditorCommands::Get().CompactMode);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE