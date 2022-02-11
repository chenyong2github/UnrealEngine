// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRewindDebugger.h"
#include "ActorPickerMode.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "RewindDebuggerStyle.h"
#include "RewindDebuggerCommands.h"
#include "SSimpleTimeSlider.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "Selection.h"
#include "Styling/SlateIconFinder.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "IRewindDebuggerViewCreator.h"
#include "RewindDebuggerViewCreators.h"
#include "IRewindDebuggerDoubleClickHandler.h"
#include "Interfaces/IMainFrameModule.h"

#define LOCTEXT_NAMESPACE "SRewindDebugger"

SRewindDebugger::SRewindDebugger() 
	: SCompoundWidget()
	, ViewRange(0,10)
	, DebugComponents(nullptr)
{ 
}

SRewindDebugger::~SRewindDebugger() 
{
}

void SRewindDebugger::TrackCursor(bool bReverse)
{
	float ScrubTime = ScrubTimeAttribute.Get();
	TRange<double> CurrentViewRange = ViewRange;
	float ViewSize = CurrentViewRange.GetUpperBoundValue() - CurrentViewRange.GetLowerBoundValue();

	static const double LeadingEdgeSize = 0.05;
	static const double TrailingEdgeThreshold = 0.01;

	if(bReverse)
	{
		// playing in reverse (cursor moving left)
		if (ScrubTime < CurrentViewRange.GetLowerBoundValue() + ViewSize * LeadingEdgeSize)
		{
			CurrentViewRange.SetLowerBound(ScrubTime - ViewSize * LeadingEdgeSize);
			CurrentViewRange.SetUpperBound(CurrentViewRange.GetLowerBoundValue() + ViewSize);
		}
		if (ScrubTime > CurrentViewRange.GetUpperBoundValue() + ViewSize * TrailingEdgeThreshold)
		{
			CurrentViewRange.SetUpperBound(ScrubTime);
			CurrentViewRange.SetLowerBound(CurrentViewRange.GetUpperBoundValue() - ViewSize);
		}
	}
	else
	{
		// playing normally or recording (cursor moving right)
		if (ScrubTime > CurrentViewRange.GetUpperBoundValue() - ViewSize * LeadingEdgeSize)
		{
			CurrentViewRange.SetUpperBound(ScrubTime + ViewSize * LeadingEdgeSize);
			CurrentViewRange.SetLowerBound(CurrentViewRange.GetUpperBoundValue() - ViewSize);
		}
		if (ScrubTime < CurrentViewRange.GetLowerBoundValue() - ViewSize * TrailingEdgeThreshold)
		{
			CurrentViewRange.SetLowerBound(ScrubTime);
			CurrentViewRange.SetUpperBound(CurrentViewRange.GetLowerBoundValue() + ViewSize);
		}
	}

	ViewRange = CurrentViewRange;
}

void SRewindDebugger::SetDebugTargetActor(AActor* Actor)
{
	DebugTargetActor.Set(Actor->GetName());
}

TSharedRef<SWidget> SRewindDebugger::MakeSelectActorMenu()
{
	// this menu is partially duplicated from LevelSequenceEditorActorBinding which has a similar workflow for adding actors to sequencer

	FMenuBuilder MenuBuilder(true, nullptr);

	// Set up a menu entry to choosing the selected actor(s) to the sequencer (maybe move this to a submenu and put each selected actor there)
	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects(SelectedActors);

	if (SelectedActors.Num() >= 1)
	{
		MenuBuilder.BeginSection("From Selection Section", LOCTEXT("FromSelection", "From Selection"));
		if (SelectedActors.Num() == 1)
		{
			AActor* SelectedActor = SelectedActors[0];

			FText SelectedLabel = FText::FromString(SelectedActor->GetActorLabel());
			FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(SelectedActors[0]->GetClass());

			MenuBuilder.AddMenuEntry(SelectedLabel, FText(), ActorIcon, FExecuteAction::CreateLambda([this, SelectedActor]{
				FSlateApplication::Get().DismissAllMenus();
				SetDebugTargetActor(SelectedActor);
			}));
		}
		else if (SelectedActors.Num() >= 1)
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("FromSelection", "From Selection"),
				LOCTEXT("FromSelection_Tooltip", "Select an Actor from the list of selected Actors"),
				FNewMenuDelegate::CreateLambda([this, SelectedActors](FMenuBuilder& SubMenuBuilder)
				{
					for(AActor* SelectedActor : SelectedActors)
					{
						FText SelectedLabel = FText::FromString(SelectedActor->GetActorLabel());
						FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(SelectedActors[0]->GetClass());

						SubMenuBuilder.AddMenuEntry(SelectedLabel, FText(), ActorIcon, FExecuteAction::CreateLambda([this, SelectedActor]{
							FSlateApplication::Get().DismissAllMenus();
							SetDebugTargetActor(SelectedActor);
						}));
					}

				})
			);

		}
		MenuBuilder.EndSection();
	}

	// todo: add special menu item for player controlled character

	MenuBuilder.BeginSection("ChooseActorSection", LOCTEXT("ChooseActor", "Choose Actor:"));

	// Set up a menu entry to select any arbitrary actor
	FSceneOutlinerInitializationOptions InitOptions;
	{
		// We hide the header row to keep the UI compact.
		InitOptions.bShowHeaderRow = false;
		InitOptions.bShowSearchBox = true;
		InitOptions.bShowCreateNewFolder = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;

		// Only want the actor label column
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));

		// todo: optionally filter for only actors that have debug data
		//InitOptions.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateLambda(IsActorValidForPossession, ExistingPossessedObjects));
	}

	// actor selector to allow the user to choose an actor
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	TSharedRef< SWidget > MiniSceneOutliner =
		SNew(SBox)
		.MaxDesiredHeight(400.0f)
		.WidthOverride(300.0f)
		[
			SceneOutlinerModule.CreateActorPicker(
				InitOptions,
				FOnActorPicked::CreateLambda([this](AActor* Actor){
					// Create a new binding for this actor
					FSlateApplication::Get().DismissAllMenus();
					SetDebugTargetActor(Actor);
				})
			)
		];

	MenuBuilder.AddWidget(MiniSceneOutliner, FText::GetEmpty(), true);
	MenuBuilder.EndSection();



	return MenuBuilder.MakeWidget();
}

void SRewindDebugger::CloseAllTabs()
{
	 for(FName& TabName : TabNames)
	 {
		CloseTab(TabName);
	 }
}

void SRewindDebugger::MainFrameCreationFinished(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow)
{
	IMainFrameModule::Get().OnMainFrameCreationFinished().RemoveAll(this);
	
	 // close all tabs that may be open from restoring the saved layout config
	CloseAllTabs();
}

void SRewindDebugger::Construct(const FArguments& InArgs, TSharedRef<FUICommandList> CommandList, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	bInitializing = true;
	
	OnScrubPositionChanged = InArgs._OnScrubPositionChanged;
	OnComponentSelectionChanged = InArgs._OnComponentSelectionChanged;
	BuildComponentContextMenu = InArgs._BuildComponentContextMenu;
	ScrubTimeAttribute = InArgs._ScrubTime;
	DebugComponents = InArgs._DebugComponents;
	TraceTime.Initialize(InArgs._TraceTime);
	RecordingDuration.Initialize(InArgs._RecordingDuration);
	DebugTargetActor.Initialize(InArgs._DebugTargetActor);
	
	FSlimHorizontalToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None, nullptr, true);

	const FRewindDebuggerCommands& Commands = FRewindDebuggerCommands::Get();

	ToolBarBuilder.SetStyle(&FAppStyle::Get(), "PaletteToolBar");
	ToolBarBuilder.BeginSection("Debugger");
	{
		ToolBarBuilder.AddToolBarButton(Commands.FirstFrame);
		ToolBarBuilder.AddToolBarButton(Commands.PreviousFrame);
		ToolBarBuilder.AddToolBarButton(Commands.ReversePlay);
		ToolBarBuilder.AddToolBarButton(Commands.Pause);
		ToolBarBuilder.AddToolBarButton(Commands.Play);
		ToolBarBuilder.AddToolBarButton(Commands.NextFrame);
		ToolBarBuilder.AddToolBarButton(Commands.LastFrame);
		ToolBarBuilder.AddToolBarButton(Commands.StartRecording);
		ToolBarBuilder.AddToolBarButton(Commands.StopRecording);
	}
	ToolBarBuilder.EndSection();

	ComponentTreeView =	SNew(SRewindDebuggerComponentTree)
				.DebugComponents(InArgs._DebugComponents)
				.OnMouseButtonDoubleClick(InArgs._OnComponentDoubleClicked)
				.OnContextMenuOpening(this, &SRewindDebugger::OnContextMenuOpening)
				.OnSelectionChanged(this, &SRewindDebugger::ComponentSelectionChanged);


	TraceTime.OnPropertyChanged = TraceTime.OnPropertyChanged.CreateRaw(this, &SRewindDebugger::TraceTimeChanged);

	// Tab Manager 
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);

	TabManager->SetOnPersistLayout(
		FTabManager::FOnPersistLayout::CreateStatic(
			[](const TSharedRef<FTabManager::FLayout>& InLayout)
			{
				if (InLayout->GetPrimaryArea().Pin().IsValid())
				{
					FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, InLayout);
				}
			}
		)
	);

	// Default Layout: all tabs in one stack, inside the rewind debugger tab

	TSharedPtr<FTabManager::FStack> MainTabStack = FTabManager::NewStack();

	FRewindDebuggerViewCreators::EnumerateCreators([this, MainTabStack](const IRewindDebuggerViewCreator* Creator)
		{
			FName TabName = Creator->GetName();
			TabNames.Add(TabName);
			// Add a closed tabs to the main tab stack in the default layout, so that the first time they won't pop up in their own window
			TabManager->RegisterTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &SRewindDebugger::SpawnTab, TabName),
													FCanSpawnTab::CreateRaw(this, &SRewindDebugger::CanSpawnTab, TabName))
				.SetDisplayName(Creator->GetTitle())
				.SetIcon(Creator->GetIcon());

			MainTabStack->AddTab(TabName, ETabState::ClosedTab);
		}
	);

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("RewindDebuggerLayout1.0") 
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				MainTabStack.ToSharedRef()
			)
		);

	// load saved layout if it exists
	Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);

	UToolMenu* Menu = UToolMenus::Get()->FindMenu("RewindDebugger.MainMenu");

	FToolMenuSection& Section = Menu->AddSection("ViewsSection", LOCTEXT("Views", "Views"));

	Section.AddDynamicEntry("ViewsSection", FNewToolMenuDelegateLegacy::CreateLambda([this](FMenuBuilder& InMenuBuilder, UToolMenu* InMenu)
	{
		MakeViewsMenu(InMenuBuilder);
	}));


	ChildSlot
	[
		SNew(SSplitter)
		+SSplitter::Slot().MinSize(280).Value(0)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SComboButton)
						.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
						.OnGetMenuContent(this, &SRewindDebugger::MakeMainMenu)
						.ButtonContent()
					[
						SNew(SImage)
						.Image(FRewindDebuggerStyle::Get().GetBrush("RewindDebugger.MenuIcon"))
					]
				]
				+SHorizontalBox::Slot().FillWidth(1.0)
				[
					ToolBarBuilder.MakeWidget()
				]
			]
			+SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot() .FillWidth(1.0f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &SRewindDebugger::MakeSelectActorMenu)
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot().AutoWidth().Padding(3)
						[
							SNew(SImage)
							.Image_Lambda([this]
								{
									FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(AActor::StaticClass());
									if (DebugComponents != nullptr && DebugComponents->Num()>0)
									{
										if (UObject* Object = FObjectTrace::GetObjectFromId((*DebugComponents)[0]->ObjectId))
										{
											ActorIcon = FSlateIconFinder::FindIconForClass(Object->GetClass());
										}
									}

									return ActorIcon.GetIcon();
								}
							)
						]
						+SHorizontalBox::Slot().Padding(3)
						[
							SNew(STextBlock)
							.Text_Lambda([this](){
								if (DebugComponents == nullptr || DebugComponents->Num()==0)
								{
									return LOCTEXT("Select Actor", "Debug Target Actor");
								}

								FString ReadableName = (*DebugComponents)[0]->ObjectName;

								if (UObject* Object = FObjectTrace::GetObjectFromId((*DebugComponents)[0]->ObjectId))
								{
									if (AActor* Actor = Cast<AActor>(Object))
									{
										ReadableName = Actor->GetActorLabel();
									}
								}

								return FText::FromString(ReadableName);
							} )
						]
					]
				]
				+SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked(this, &SRewindDebugger::OnSelectActorClicked)
					[
						SNew(SImage)
						.Image(FRewindDebuggerStyle::Get().GetBrush("RewindDebugger.SelectActor"))
					]
				]
			]
			+SVerticalBox::Slot().FillHeight(1.0f)
			[
				ComponentTreeView.ToSharedRef()
			]
		]
		+SSplitter::Slot() 
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSimpleTimeSlider)
					.ClampRangeHighlightSize(0.15f)
					.ClampRangeHighlightColor(FLinearColor::Red.CopyWithNewOpacity(0.5f))
					.ScrubPosition(ScrubTimeAttribute)
					.ViewRange_Lambda([this](){ return ViewRange; })
					.OnViewRangeChanged_Lambda([this](TRange<double> NewRange) { ViewRange = NewRange; })
					.ClampRange_Lambda(
							[this]()
							{ 
								return TRange<double>(0.0f,RecordingDuration.Get());
							})	
					.OnScrubPositionChanged_Lambda(
						[this](double NewScrubTime, bool bIsScrubbing)
								{
									if (bIsScrubbing)
									{
										OnScrubPositionChanged.ExecuteIfBound( NewScrubTime, bIsScrubbing );
									}
								})
			]
			+SVerticalBox::Slot().FillHeight(1.0f)
			[
				TabManager->RestoreFrom(Layout,TSharedPtr<SWindow>()).ToSharedRef()
			]
		]
	];

	if (IMainFrameModule::Get().IsWindowInitialized())
	{
		// close all tabs that may be open from restoring the saved layout config
		CloseAllTabs();
	}
	else
	{
		// close them later if we are initalizing the layout, to avoid issues with empty windows and crashes
		IMainFrameModule::Get().OnMainFrameCreationFinished().AddRaw(this, &SRewindDebugger::MainFrameCreationFinished);
	}
	
	bInitializing = false;
}

void SRewindDebugger::RefreshDebugComponents()
{
	ComponentTreeView->Refresh();
}

void SRewindDebugger::TraceTimeChanged(double Time)
{
	for(TSharedPtr<IRewindDebuggerView>& DebugView : DebugViews)
	{
		DebugView->SetTimeMarker(Time);
	}
	for(TSharedPtr<IRewindDebuggerView>& DebugView : PinnedDebugViews)
	{
		DebugView->SetTimeMarker(Time);
	}
}

TSharedRef<SDockTab> SRewindDebugger::SpawnTab(const FSpawnTabArgs& Args, FName ViewName)
{
	TSharedPtr<IRewindDebuggerView>* View = DebugViews.FindByPredicate([ViewName](TSharedPtr<IRewindDebuggerView>& View) { return View->GetName() == ViewName; } );
	if (View)
	{
		HiddenTabs.Remove(ViewName);

		return SNew(SDockTab)
		[
			View->ToSharedRef()
		]
		.OnExtendContextMenu(this, &SRewindDebugger::ExtendTabMenu, *View)
		.OnTabClosed_Lambda([this,ViewName](TSharedRef<SDockTab>)
		{
			if(!bInternalClosingTab) // skip this if the tab is being closed by our own code
			{
				HiddenTabs.Add(ViewName);
			}
		});
	}

	return SNew(SDockTab);
}

// returns true if DebugViews contains a view for the ViewName, but there is no matching Pinned view already open
bool SRewindDebugger::CanSpawnTab(const FSpawnTabArgs& Args, FName ViewName)
{
	if (bInitializing)
	{
		return true;
	}
	
	TSharedPtr<IRewindDebuggerView>* View = DebugViews.FindByPredicate([ViewName](TSharedPtr<IRewindDebuggerView>& View) { return View->GetName() == ViewName; } );
	
	if (View!=nullptr)
	{
		bool bPinned = nullptr != PinnedDebugViews.FindByPredicate(
			[View](TSharedPtr<IRewindDebuggerView>& PinnedView)
			{
				return PinnedView->GetName() == (*View)->GetName() && PinnedView->GetObjectId() == (*View)->GetObjectId();
			}
		);
	
		return !bPinned;
	}
	return false;
}

void SRewindDebugger::OnPinnedTabClosed(TSharedRef<SDockTab> Tab)
{
	// remove view from list of pinned views
	TSharedRef<IRewindDebuggerView> View = StaticCastSharedRef<IRewindDebuggerView>(Tab->GetContent());
	PinnedDebugViews.Remove(View);

	// recreate non-pinned tabs, so when closing a pinned tab for the currently selected component, the non-pinned one will appear
	CreateDebugTabs();
}

 void SRewindDebugger::PinTab(TSharedPtr<IRewindDebuggerView> View)
 {
	if (PinnedDebugViews.Find(View) != INDEX_NONE)
	{
		return;
	}

	FName TabName = View->GetName();

	CloseTab(TabName);

	FSlateIcon TabIcon;
	FText TabLabel;

	if (const IRewindDebuggerViewCreator* Creator = FRewindDebuggerViewCreators::GetCreator(TabName))
	{
		TabIcon = Creator->GetIcon();
		TabLabel = Creator->GetTitle();
	}

	TSharedPtr<SDockTab> NewTab = SNew(SDockTab)
	[
		// add a wrapper widget here, that says the name of the object/component for pinned tabs 
		View.ToSharedRef()
	]
	.OnExtendContextMenu(this, &SRewindDebugger::ExtendTabMenu, View)
	.OnTabClosed(this, &SRewindDebugger::OnPinnedTabClosed)
	.Label(TabLabel)
	.LabelSuffix(LOCTEXT(" (Locked)", " \xD83D\xDD12")); // unicode lock image

	NewTab->SetTabIcon(TabIcon.GetIcon());


	static const FName RewindDebuggerPinnedTab = "RewindDebuggerPinnedTabName";
	TabManager->InsertNewDocumentTab(TabName, RewindDebuggerPinnedTab, FTabManager::FRequireClosedTab(), NewTab.ToSharedRef());

	PinnedDebugViews.Add(View);
 }

TSharedRef<SWidget> SRewindDebugger::MakeMainMenu()
{
	return UToolMenus::Get()->GenerateWidget("RewindDebugger.MainMenu", FToolMenuContext());
}

void SRewindDebugger::MakeViewsMenu(FMenuBuilder& MenuBuilder)
{

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);

	MenuBuilder.AddMenuEntry(LOCTEXT("Show All Views", "Show All Views"),
							 LOCTEXT("Show All tooltip", "Show all debug views that are relevant to the selected object type"),
							 FSlateIcon(),
							 FExecuteAction::CreateLambda([this]() { ShowAllViews(); }));
}

void SRewindDebugger::ExtendTabMenu(FMenuBuilder& MenuBuilder, TSharedPtr<IRewindDebuggerView> View)
{
	MenuBuilder.BeginSection("RewindDebugger", LOCTEXT("Rewind Debugger", "Rewind Debugger"));

	MenuBuilder.AddMenuEntry(LOCTEXT("Keep View Open", "Keep View Open"),
							 LOCTEXT("Keep View Open tooltip", "Keep this debug view open even while selected component/actor changes"),
							 FSlateIcon(),
							 FUIAction(
								FExecuteAction::CreateRaw(this, &SRewindDebugger::PinTab, View),
								FCanExecuteAction::CreateLambda([this,View](){ return PinnedDebugViews.Find(View) == INDEX_NONE; })
							 )); 


	MenuBuilder.EndSection();
}

void SRewindDebugger::ShowAllViews()
{
	HiddenTabs.Empty();
	CreateDebugTabs();
}

void SRewindDebugger::CreateDebugViews()
{
	DebugViews.Empty();

	if (SelectedComponent.IsValid())
	{
		IUnrealInsightsModule &UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
		TSharedPtr<const TraceServices::IAnalysisSession> Session = UnrealInsightsModule.GetAnalysisSession();

		FRewindDebuggerViewCreators::CreateDebugViews(SelectedComponent->ObjectId, TraceTime.Get(), *Session, DebugViews);
	}
}

void SRewindDebugger::CloseTab(FName TabName)
{
	// using bInternalClosingTab to distinguish between procedural, and user initiated tab closing in the OnTabClosed callback
	bInternalClosingTab = true;
	TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(TabName);
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
	bInternalClosingTab = false;
}

void SRewindDebugger::CreateDebugTabs()
{
	for(FName& TabName : TabNames)
	{
		CloseTab(TabName);
	}

	for(TSharedPtr<IRewindDebuggerView> DebugView : DebugViews)
	{
		FName ViewName = DebugView->GetName();
		uint64 ObjectId = DebugView->GetObjectId();

	    bool bPinned = nullptr != PinnedDebugViews.FindByPredicate([ViewName, ObjectId](TSharedPtr<IRewindDebuggerView>& View) { return View->GetName() == ViewName && View->GetObjectId() == ObjectId; } );
	    bool bHidden = HiddenTabs.Find(ViewName) != INDEX_NONE;

		if(!bPinned && !bHidden)
		{
			TabManager->TryInvokeTab(ViewName);
		}
	}
}

void SRewindDebugger::ComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo)
{
	SelectedComponent = SelectedItem;

	OnComponentSelectionChanged.ExecuteIfBound(SelectedItem);

	CreateDebugViews();
	CreateDebugTabs();
}

TSharedPtr<SWidget> SRewindDebugger::OnContextMenuOpening()
{
	return BuildComponentContextMenu.Execute();
}

FReply SRewindDebugger::OnSelectActorClicked()
{
		FActorPickerModeModule& ActorPickerMode = FModuleManager::Get().GetModuleChecked<FActorPickerModeModule>("ActorPickerMode");
		
		// todo: force eject (from within BeginActorPickingMode?)

		ActorPickerMode.BeginActorPickingMode(
			FOnGetAllowedClasses(), 
			FOnShouldFilterActor(),
			FOnActorSelected::CreateRaw(this, &SRewindDebugger::SetDebugTargetActor));



	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
