// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRewindDebugger.h"
#include "ActorPickerMode.h"
#include "Async/Future.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Insights/IUnrealInsightsModule.h"
#include "IGameplayInsightsModule.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "RewindDebuggerStyle.h"
#include "RewindDebuggerCommands.h"
#include "SRewindDebuggerTimeSlider.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "Selection.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/SListView.h"


#define LOCTEXT_NAMESPACE "SAnimationInsights"

SRewindDebugger::SRewindDebugger() 
	: SCompoundWidget()
	, DebugComponents(nullptr)
{ 
}

SRewindDebugger::~SRewindDebugger() 
{

}

void SRewindDebugger::TrackCursor(bool bReverse)
{
	float ScrubTime = ScrubTimeAttribute.Get();
	TRange<float> CurrentViewRange = TimeSliderController->GetTimeSliderArgs().ViewRange.Get();
	float ViewSize = CurrentViewRange.GetUpperBoundValue() - CurrentViewRange.GetLowerBoundValue();

	static const float LeadingEdgeSize = 0.05f;
	static const float TrailingEdgeThreshold = 0.01f;

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


	TimeSliderController->GetTimeSliderArgs().ViewRange = CurrentViewRange;
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


void SRewindDebugger::Construct(const FArguments& InArgs, TSharedRef<FUICommandList> CommandList, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	OnScrubPositionChanged = InArgs._OnScrubPositionChanged;
	ScrubTimeAttribute = InArgs._ScrubTime;
	DebugComponents = InArgs._DebugComponents;
	TraceTime.Initialize(InArgs._TraceTime);
	RecordingDuration.Initialize(InArgs._RecordingDuration);
	DebugTargetActor.Initialize(InArgs._DebugTargetActor);
	
	FSlimHorizontalToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None, nullptr, true);

	const FRewindDebuggerCommands& Commands = FRewindDebuggerCommands::Get();

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

	FTimeSliderArgs InitArgs;
	InitArgs.CursorSize = 0.0f;
	InitArgs.OnScrubPositionChanged = FTimeSliderArgs::FOnScrubPositionChanged::CreateLambda(
		[this](float NewScrubTime, bool bIsScrubbing)
		{
			if (bIsScrubbing)
			{
				OnScrubPositionChanged.ExecuteIfBound( NewScrubTime, bIsScrubbing );
			}
		}
	);

	InitArgs.ScrubPosition = ScrubTimeAttribute;

	InitArgs.ClampRange = InitArgs.ClampRange.CreateLambda(
		[this]()
		{ 
			return TRange<float>(0.0f,RecordingDuration.Get());
		} );

	TimeSliderController = MakeShared<FTimeSliderController>(InitArgs);
    TSharedRef<STimeSlider> TimeSlider = SNew(STimeSlider, TimeSliderController.ToSharedRef());

	ComponentListView =	SNew(SListView<TSharedPtr<FDebugObjectInfo>>)
									.ItemHeight(16.0f)
									.ListItemsSource(DebugComponents)
									.OnGenerateRow(this, &SRewindDebugger::ComponentListViewGenerateRow)
									.SelectionMode(ESelectionMode::Single)
									.OnSelectionChanged(this, &SRewindDebugger::OnComponentSelectionChanged);


	// everything related to creating SAnimGraphSchematicView should be moved to a separate file
	IUnrealInsightsModule &UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	IGameplayInsightsModule& GameplayInsightsModule = FModuleManager::LoadModuleChecked<IGameplayInsightsModule>("GameplayInsights");
	TSharedPtr<const TraceServices::IAnalysisSession> Session = UnrealInsightsModule.GetAnalysisSession();

	AnimGraphView = GameplayInsightsModule.CreateAnimGraphSchematicView(0, TraceTime.Get(), *Session);
	TraceTime.OnPropertyChanged = TraceTime.OnPropertyChanged.CreateSP(AnimGraphView.Get(), &IAnimGraphSchematicView::SetTimeMarker);

	FSlateIcon ActorIcon = FSlateIconFinder::FindIconForClass(AActor::StaticClass());

	ChildSlot
	[
		SNew(SSplitter)
		+SSplitter::Slot().MinSize(270).Value(0)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot().AutoHeight()
			[
				ToolBarBuilder.MakeWidget()
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
						SNew(STextBlock)
						.Text_Lambda([this](){
							const FString& TargetName = DebugTargetActor.Get();
							return (TargetName.IsEmpty()) ? LOCTEXT("Select Actor", "Debug Target Actor") : FText::FromString(TargetName);
						 } )
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
			+SVerticalBox::Slot().VAlign(VAlign_Top) .FillHeight(1.0f)
			[
				ComponentListView.ToSharedRef()
			]
		]
		+SSplitter::Slot() 
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot() .VAlign(VAlign_Top) .AutoHeight()
			[
				TimeSlider
			]
			+SVerticalBox::Slot() .VAlign(VAlign_Top) .FillHeight(1.0f)
			[
				// todo: this should be a proxy slot that we swap out based on which component is selected in the treeview
				AnimGraphView.ToSharedRef()
			]
		]
	];
}

TSharedRef<ITableRow> SRewindDebugger::ComponentListViewGenerateRow(TSharedPtr<FDebugObjectInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return 
		SNew(STableRow<TSharedPtr<FDebugObjectInfo>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InItem->ObjectName))
		];
}

void SRewindDebugger::RefreshDebugComponents()
{
	ComponentListView->RebuildList();
}

void SRewindDebugger::OnComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectedItem.IsValid())
	{
		AnimGraphView->SetAnimInstanceId(SelectedItem->ObjectId);
	}
	else
	{
		AnimGraphView->SetAnimInstanceId(0);
	}
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
