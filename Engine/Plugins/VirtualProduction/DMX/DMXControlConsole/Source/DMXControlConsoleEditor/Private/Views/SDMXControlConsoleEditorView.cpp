// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorView.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Application/ThrottleManager.h"
#include "Commands/DMXControlConsoleEditorCommands.h"
#include "Customizations/DMXControlConsoleDataDetails.h"
#include "Customizations/DMXControlConsoleFaderDetails.h"
#include "Customizations/DMXControlConsoleFaderGroupDetails.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXEditorStyle.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "Layout/Visibility.h"
#include "Library/DMXEntityReference.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Models/Filter/FilterModel.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "TimerManager.h"
#include "Views/SDMXControlConsoleEditorFaderGroupRowView.h"
#include "Views/SDMXControlConsoleEditorFaderGroupView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleEditorAddButton.h"
#include "Widgets/SDMXControlConsoleEditorAssetPicker.h"
#include "Widgets/SDMXControlConsoleEditorFixturePatchVerticalBox.h"
#include "Widgets/SDMXControlConsoleEditorPortSelector.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorView"

SDMXControlConsoleEditorView::~SDMXControlConsoleEditorView()
{
	FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(OnActiveTabChangedDelegateHandle);
}

void SDMXControlConsoleEditorView::Construct(const FArguments& InArgs)
{
	RegisterCommands();

	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.GetOnConsoleLoaded().AddSP(this, &SDMXControlConsoleEditorView::OnConsoleLoaded);
	EditorConsoleModel.GetOnConsoleSaved().AddSP(this, &SDMXControlConsoleEditorView::OnConsoleSaved);
	EditorConsoleModel.GetOnScrollFaderGroupIntoView().AddSP(this, &SDMXControlConsoleEditorView::OnScrollIntoView);
	EditorConsoleModel.GetOnControlConsoleForceRefresh().AddSP(this, &SDMXControlConsoleEditorView::OnFaderGroupRowAdded);
	EditorConsoleModel.GetOnControlConsoleForceRefresh().AddSP(this, &SDMXControlConsoleEditorView::OnFaderGroupRowRemoved);
	EditorConsoleModel.GetOnControlConsoleForceRefresh().AddSP(this, &SDMXControlConsoleEditorView::RequestUpdateDetailsViews);

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
	SelectionHandler->GetOnSelectionChanged().AddSP(this, &SDMXControlConsoleEditorView::RequestUpdateDetailsViews);

	OnActiveTabChangedDelegateHandle = FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &SDMXControlConsoleEditorView::OnActiveTabChanged));

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	
	ControlConsoleDataDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	FaderGroupsDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	FadersDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);

	FOnGetDetailCustomizationInstance ControlConsoleCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXControlConsoleDataDetails::MakeInstance);
	ControlConsoleDataDetailsView->RegisterInstancedCustomPropertyLayout(UDMXControlConsoleData::StaticClass(), ControlConsoleCustomizationInstance);
	ControlConsoleDataDetailsView->OnFinishedChangingProperties().AddSP(this, &SDMXControlConsoleEditorView::OnControlConsoleDataPropertyChanged);

	FOnGetDetailCustomizationInstance FaderGroupsCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXControlConsoleFaderGroupDetails::MakeInstance);
	FaderGroupsDetailsView->RegisterInstancedCustomPropertyLayout(UDMXControlConsoleFaderGroup::StaticClass(), FaderGroupsCustomizationInstance);

	FOnGetDetailCustomizationInstance FadersCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&UE::DMXControlConsole::FDMXControlConsoleFaderDetails::MakeInstance);
	FadersDetailsView->RegisterInstancedCustomPropertyLayout(UDMXControlConsoleFaderBase::StaticClass(), FadersCustomizationInstance);

	const TSharedRef<SScrollBar> VerticalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Vertical);

	const TSharedRef<SScrollBar> HorizontalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Horizontal);

	ChildSlot
		[
			SNew(SVerticalBox)
			// Toolbar Section
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				GenerateToolbar()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			// Panel Section
			+ SVerticalBox::Slot()
			[
				SNew(SBorder)
				.BorderImage(FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.DefaultBrush"))
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					.ResizeMode(ESplitterResizeMode::FixedSize)

					// Fixture Patches VerticalBox Section
					+ SSplitter::Slot()
					.Value(.2f)
					.MinSize(10.f)
					[
						SNew(SScrollBox)
						.Orientation(Orient_Vertical)

						+ SScrollBox::Slot()
						.AutoSize()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								ControlConsoleDataDetailsView.ToSharedRef()
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(FixturePatchVerticalBox, SDMXControlConsoleEditorFixturePatchVerticalBox)
							]
						]
					]

					// DMX Control Console Section
					+ SSplitter::Slot()
					.Value(.8f)
					.MinSize(10.f)
					[
						SNew(SSplitter)
						.Orientation(Orient_Horizontal)
						.ResizeMode(ESplitterResizeMode::FixedSize)

						+ SSplitter::Slot()
						.Value(.80)
						.MinSize(10.f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("NoBorder"))
							.Padding(10.f)
							[

								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								[
									SNew(SHorizontalBox)
									+SHorizontalBox::Slot()
									[
										SAssignNew(HorizontalScrollBox, SScrollBox)
										.ExternalScrollbar(HorizontalScrollBar)
										.Orientation(Orient_Horizontal)

										+ SScrollBox::Slot()
										[
											SAssignNew(VerticalScrollBox, SScrollBox)
											.ExternalScrollbar(VerticalScrollBar)
											.Orientation(Orient_Vertical)

											+ SScrollBox::Slot()
											.HAlign(HAlign_Left)
											.VAlign(VAlign_Center)
											[
												SNew(SBox)
												.WidthOverride(50.f)
												.HeightOverride(50.f)
												.HAlign(HAlign_Center)
												.VAlign(VAlign_Center)
												[
													SNew(SDMXControlConsoleEditorAddButton)
													.OnClicked(this, &SDMXControlConsoleEditorView::OnAddFirstFaderGroup)
													.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleEditorView::GetAddButtonVisibility))
												]
											]

											+ SScrollBox::Slot()
											[
												SAssignNew(FaderGroupRowsVerticalBox, SVerticalBox)
											]
										]
									]
						
									// Horizontal ScrollBar slot
									+SHorizontalBox::Slot()
									.AutoWidth()
									[
										VerticalScrollBar
									]
								]
					
								// Vertical Scrollbar slot
								+SVerticalBox::Slot()
								.AutoHeight()
								[
									HorizontalScrollBar
								]
							]
						]

						// Details View Section
						+ SSplitter::Slot()
						.Value(.2f)
						.MinSize(10.f)
						[
							SNew(SScrollBox)
							.Orientation(Orient_Vertical)
							.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleEditorView::GetDetailViewsSectionVisibility))

							+ SScrollBox::Slot()
							[
								SNew(SVerticalBox)

								+SVerticalBox::Slot()
								.AutoHeight()
								[
									FadersDetailsView.ToSharedRef()
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SSeparator)
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									FaderGroupsDetailsView.ToSharedRef()
								]
							]
						]
					]
				]
			]
		];
	
	ForceUpdateDetailsViews();
	RestoreGlobalFilter();
	OnSelectedPortsChanged();

	const TSharedRef<SWidget> SharedThis = AsShared();
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([SharedThis]()
		{
			FSlateApplication::Get().SetKeyboardFocus(SharedThis);
		}));
}

UDMXControlConsoleEditorModel& SDMXControlConsoleEditorView::GetEditorConsoleModel() const
{
	return *GetMutableDefault<UDMXControlConsoleEditorModel>();
}

UDMXControlConsoleData* SDMXControlConsoleEditorView::GetControlConsoleData() const
{ 
	const UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	return EditorConsoleModel.GetEditorConsoleData();
}

void SDMXControlConsoleEditorView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const TObjectPtr<UDMXControlConsoleData> ControlConsoleData = GetControlConsoleData();
	if (!ensureMsgf(ControlConsoleData, TEXT("Invalid DMX Control Console, can't update DMX Control Console state correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsoleData->GetFaderGroupRows();
	if (FaderGroupRows.Num() == FaderGroupRowViews.Num())
	{
		return;
	}

	if (FaderGroupRows.Num() > FaderGroupRowViews.Num())
	{
		OnFaderGroupRowAdded();
	}
	else
	{
		OnFaderGroupRowRemoved();
	}
}

FReply SDMXControlConsoleEditorView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXControlConsoleEditorView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
	SelectionHandler->ClearSelection();
	return FReply::Handled();
}

void SDMXControlConsoleEditorView::PostUndo(bool bSuccess)
{
	UpdateFixturePatchVerticalBox();
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.RequestRefresh();
}

void SDMXControlConsoleEditorView::PostRedo(bool bSuccess)
{
	UpdateFixturePatchVerticalBox();
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.RequestRefresh();
}

void SDMXControlConsoleEditorView::RegisterCommands()
{
	CommandList = MakeShared<FUICommandList>();

	UDMXControlConsoleEditorModel* EditorConsoleModel = &GetEditorConsoleModel();

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().CreateNewConsole,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::CreateNewConsole)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SaveConsole,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::SaveConsole)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SaveConsoleAs,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::SaveConsoleAs)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SendDMX,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::SendDMX),
		FCanExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::CanSendDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::CanSendDMX)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().StopDMX,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::StopDMX),
		FCanExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::CanStopDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::CanStopDMX)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().RemoveElements,
		FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::RemoveAllSelectedElements)
	);

	constexpr bool bSelectOnlyVisible = true;
	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorView::OnSelectAll, bSelectOnlyVisible)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().ClearAll,
		FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorView::OnClearAll)
	);
}

TSharedRef<SWidget> SDMXControlConsoleEditorView::GenerateToolbar()
{
	ensureMsgf(CommandList.IsValid(), TEXT("Invalid command list for control console editor view."));

	FSlimHorizontalToolBarBuilder ToolbarBuilder = FSlimHorizontalToolBarBuilder(CommandList, FMultiBoxCustomization::None);

	const auto GenerateButtonContentLambda = [](const FSlateColor& ImageColor, const FSlateBrush* ImageBrush, const FText& ButtonText)
		{
			return
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(2.f)
				.AutoWidth()
				[
					SNew(SImage)
					.ColorAndOpacity(ImageColor)
					.Image(ImageBrush)
				]

				+ SHorizontalBox::Slot()
				.Padding(8.f, 2.f, 2.f, 2.f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(ButtonText)
				];
		};

	ToolbarBuilder.BeginSection("AssetActions");
	{
		ToolbarBuilder.AddToolBarButton(
			FDMXControlConsoleEditorCommands::Get().CreateNewConsole,
			NAME_None,
			FText::GetEmpty(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle")
		);

		ToolbarBuilder.AddWidget(SNew(SDMXControlConsoleEditorAssetPicker));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("SendDMX");
	{
		ToolbarBuilder.AddToolBarButton(
			FDMXControlConsoleEditorCommands::Get().SendDMX,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.PlayDMX"),
			FName(TEXT("Send DMX"))
		);

		ToolbarBuilder.AddToolBarButton(
			FDMXControlConsoleEditorCommands::Get().StopDMX,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.StopPlayingDMX"),
			FName(TEXT("Stop Sending DMX"))
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Clear");
	{
		ToolbarBuilder.AddToolBarButton(
			FDMXControlConsoleEditorCommands::Get().ClearAll,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FName(TEXT("Clear All"))
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Modes");
	{
		// Input Mode
		const TSharedRef<SComboButton> ControlModeComboButton =
			SNew(SComboButton)
			.ContentPadding(0.f)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.OnGetMenuContent(this, &SDMXControlConsoleEditorView::GenerateControlModeMenuWidget)
			.HasDownArrow(true)
			.ButtonContent()
			[
				GenerateButtonContentLambda(
					FSlateColor::UseForeground(),
					FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.InputMode"),
					LOCTEXT("ControlModeToolbarButtonText", "Control Mode"))
			];

		ToolbarBuilder.AddWidget(ControlModeComboButton);
	
		const TSharedRef<SComboButton> ViewModeComboButton =
			SNew(SComboButton)
			.ContentPadding(0.f)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.OnGetMenuContent(this, &SDMXControlConsoleEditorView::GenerateViewModeMenuWidget)
			.HasDownArrow(true)
			.ButtonContent()
			[
				GenerateButtonContentLambda(
					FSlateColor::UseForeground(),
					FAppStyle::Get().GetBrush("Icons.Layout"),
					LOCTEXT("ViewModeToolbarButtonText", "View Mode"))
			];

		ToolbarBuilder.AddWidget(ViewModeComboButton);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Selection");
	{
		const TSharedRef<SComboButton> SelectionComboButton =
			SNew(SComboButton)
			.ContentPadding(0.f)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.OnGetMenuContent(this, &SDMXControlConsoleEditorView::GenerateSelectionMenuWidget)
			.HasDownArrow(true)
			.ButtonContent()
			[
				GenerateButtonContentLambda(
					FSlateColor::UseForeground(),
					FAppStyle::GetBrush("LevelEditor.Tabs.Viewports"),
					LOCTEXT("SelectionToolbarButtonText", "Selection"))
			];

		ToolbarBuilder.AddWidget(SelectionComboButton);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Search");
	{
		const TSharedRef<SWidget> SearchBarWidget =
			SNew(SHorizontalBox)

			// SearchBox section
			+ SHorizontalBox::Slot()
			[
				SAssignNew(GlobalFilterSearchBox, SSearchBox)
				.DelayChangeNotificationsWhileTyping(true)
				.DelayChangeNotificationsWhileTypingSeconds(.5f)
				.MinDesiredWidth(300.f)
				.OnTextChanged(this, &SDMXControlConsoleEditorView::OnSearchTextChanged)
				.ToolTipText(LOCTEXT("SearchBarTooltip", "Searches for Fader Name, Attributes, Fixture ID, Universe or Patch. Examples:\n\n* FaderName\n* Dimmer\n* Pan, Tilt\n* 1\n* 1.\n* 1.1\n* Universe 1\n* Uni 1-3\n* Uni 1, 3\n* Uni 1, 4-5'."))
			]

			// Autooselection CheckBox section
			+ SHorizontalBox::Slot()
			.Padding(4.f, 0.f)
			.AutoWidth()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked(this, &SDMXControlConsoleEditorView::IsFilteredElementsAutoSelectChecked)
					.OnCheckStateChanged(this, &SDMXControlConsoleEditorView::OnFilteredElementsAutoSelectStateChanged)
				]
				+ SHorizontalBox::Slot()
				.Padding(4.f, 0.f, 2.f, 0.f)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(LOCTEXT("SearchBarAutoselectText", "Auto-Select"))
				]
			];

		ToolbarBuilder.AddWidget(SearchBarWidget);
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SDMXControlConsoleEditorView::GenerateControlModeMenuWidget()
{
	constexpr bool bShouldCloseWindowAfterClosing = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

	MenuBuilder.BeginSection("Faders", LOCTEXT("FadersControlModeCategory", "Faders"));
	{
		UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		const auto AddMenuEntryLambda = [&MenuBuilder, EditorConsoleModel, this](const FText& Label, const FText& ToolTip, EDMXControlConsoleEditorControlMode ControlMode)
			{
				MenuBuilder.AddMenuEntry
				(
					Label,
					ToolTip,
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::SetInputMode, ControlMode),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this, EditorConsoleModel, ControlMode]() { return EditorConsoleModel->GetControlMode() == ControlMode; })
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			};

		AddMenuEntryLambda
		(
			LOCTEXT("RelativeControlModeRadioButtonLabel", "Relative"),
			LOCTEXT("RelativeControlModeRadioButton_ToolTip", "Values of all selected Faders are increased/decreased by the same percentage."),
			EDMXControlConsoleEditorControlMode::Relative
		);

		AddMenuEntryLambda(
			LOCTEXT("AbsoluteControlModeRadioButtonLabel", "Absolute"),
			LOCTEXT("AbsoluteControlModeRadioButton_ToolTip", "Values of all selected Faders are set to the same percentage."),
			EDMXControlConsoleEditorControlMode::Absolute
		);

		// Port Selector menu entry

		const TSharedRef<SWidget> PortSelectorWidget =
			SNew(SBox)
			.Padding(4.f, 0.f)
			[
				SAssignNew(PortSelector, SDMXControlConsoleEditorPortSelector)
				.OnPortsSelected(this, &SDMXControlConsoleEditorView::OnSelectedPortsChanged)
			];

		MenuBuilder.AddWidget(PortSelectorWidget, FText::GetEmpty());
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDMXControlConsoleEditorView::GenerateViewModeMenuWidget()
{
	constexpr bool bShouldCloseWindowAfterClosing = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

	MenuBuilder.BeginSection("Fader Groups", LOCTEXT("FaderGroupsViewModeCategory", "Fader Groups"));
	{
		const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, EDMXControlConsoleEditorViewMode ViewMode)
			{
				MenuBuilder.AddMenuEntry
				(
					Label,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorView::OnFaderGroupsViewModeSelected, ViewMode)
					),
					NAME_None,
					EUserInterfaceActionType::Button
				);
			};

		AddMenuEntryLambda(LOCTEXT("FaderGroupsViewModeCollapseAllButtonLabel", "Collapse All"), EDMXControlConsoleEditorViewMode::Collapsed);
		AddMenuEntryLambda(LOCTEXT("FaderGroupsViewModeExpandAllButtonLabel", "Expand All"), EDMXControlConsoleEditorViewMode::Expanded);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Faders", LOCTEXT("FadersViewModeCategory", "Faders"));
	{
		const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, EDMXControlConsoleEditorViewMode ViewMode)
			{
				MenuBuilder.AddMenuEntry
				(
					Label,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorView::OnFadersViewModeSelected, ViewMode),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this, ViewMode]() { return GetEditorConsoleModel().GetFadersViewMode() == ViewMode; })
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			};

		AddMenuEntryLambda(LOCTEXT("FadersViewModeCollapsedRadioButtonLabel", "Collapsed"), EDMXControlConsoleEditorViewMode::Collapsed);
		AddMenuEntryLambda(LOCTEXT("FadersViewModeExpandedRadioButtonLabel", "Expanded"), EDMXControlConsoleEditorViewMode::Expanded);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDMXControlConsoleEditorView::GenerateSelectionMenuWidget()
{
	constexpr bool bShouldCloseWindowAfterClosing = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

	MenuBuilder.BeginSection("Fader Groups", LOCTEXT("FaderGroupsSelectionCategory", "Fader Groups"));
	{
		// Selection buttons menu entries
		const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, bool bOnlyVisible = false)
			{
				MenuBuilder.AddMenuEntry
				(
					Label,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorView::OnSelectAll, bOnlyVisible)
					),
					NAME_None,
					EUserInterfaceActionType::Button
				);
			};

		AddMenuEntryLambda(LOCTEXT("EditorViewSelectAllButtonLabel", "Select All"));
		AddMenuEntryLambda(LOCTEXT("EditorViewSelectOnlyFilteredLabel", "Select Only Filtered"), true);

		// Selection toggle button menu entry
		UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("EditorViewAutoSelectLabel", "Auto-Select"),
			LOCTEXT("EditorViewAutoSelectToolTip", "Checked if activated Fader Groups must be automatically selected."),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::ToggleAutoSelectActivePatches),
				FCanExecuteAction(),
				FIsActionChecked::CreateUObject(EditorConsoleModel, &UDMXControlConsoleEditorModel::GetAutoSelectActivePatches)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SDMXControlConsoleEditorFaderGroupRowView> SDMXControlConsoleEditorView::FindFaderGroupRowView(const UDMXControlConsoleFaderGroupRow* FaderGroupRow)
{
	if (FaderGroupRow)
	{
		const TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView>* FaderGroupRowViewPtr = Algo::FindByPredicate(FaderGroupRowViews, [FaderGroupRow](const TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView>& WeakFaderGroupRowView)
			{
				return WeakFaderGroupRowView.IsValid() && WeakFaderGroupRowView.Pin()->GetFaderGroupRow() == FaderGroupRow;
			});

		if (FaderGroupRowViewPtr)
		{
			return FaderGroupRowViewPtr->Pin();
		}
	}

	return nullptr;
}

void SDMXControlConsoleEditorView::RestoreGlobalFilter()
{
	if (const UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData())
	{
		const FString& FilterString = ControlConsoleData->FilterString;
		const FText FilterText = FText::FromString(FilterString);
		GlobalFilterSearchBox->SetText(FilterText);
	}
}

void SDMXControlConsoleEditorView::RequestUpdateDetailsViews()
{
	if(!UpdateDetailsViewTimerHandle.IsValid())
	{
		UpdateDetailsViewTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXControlConsoleEditorView::ForceUpdateDetailsViews));
	}
}

void SDMXControlConsoleEditorView::ForceUpdateDetailsViews()
{
	UpdateDetailsViewTimerHandle.Invalidate();

	UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData();
	if (!ensureMsgf(ControlConsoleData, TEXT("Invalid DMX Control Console, can't update details view correctly.")))
	{
		return;
	}

	constexpr bool bForceRefresh = true;
	ControlConsoleDataDetailsView->SetObject(ControlConsoleData, bForceRefresh);

	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupObjects = SelectionHandler->GetSelectedFaderGroups();
	SelectedFaderGroupObjects.RemoveAll([](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			return SelectedFaderGroup && !SelectedFaderGroup->IsMatchingFilter();
		});
	FaderGroupsDetailsView->SetObjects(SelectedFaderGroupObjects, bForceRefresh);

	TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
	SelectedFaderObjects.RemoveAll([](const TWeakObjectPtr<UObject>& SelectedFaderObject)
		{
			const UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
			return SelectedFader && !SelectedFader->IsMatchingFilter();
		});
	FadersDetailsView->SetObjects(SelectedFaderObjects, bForceRefresh);
}

void SDMXControlConsoleEditorView::UpdateFixturePatchVerticalBox()
{
	if (FixturePatchVerticalBox.IsValid())
	{
		FixturePatchVerticalBox->ForceRefresh();
	}
}

void SDMXControlConsoleEditorView::OnFaderGroupRowAdded()
{
	const UDMXControlConsoleData* ControlConsole = GetControlConsoleData();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't add new fader group row correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsole->GetFaderGroupRows();
	for (UDMXControlConsoleFaderGroupRow* FaderGroupRow : FaderGroupRows)
	{
		if (!FaderGroupRow)
		{
			continue;
		}

		if (IsFaderGroupRowContained(FaderGroupRow))
		{
			continue;
		}

		AddFaderGroupRow(FaderGroupRow);
	}
}

void SDMXControlConsoleEditorView::AddFaderGroupRow(UDMXControlConsoleFaderGroupRow* FaderGroupRow)
{
	if (!ensureMsgf(FaderGroupRow, TEXT("Invalid fader group row, can't add new fader group row view correctly.")))
	{
		return;
	}

	if (!FaderGroupRowsVerticalBox.IsValid())
	{
		return;
	}

	const TSharedRef<SDMXControlConsoleEditorFaderGroupRowView> FaderGroupRowWidget = 
		SNew(SDMXControlConsoleEditorFaderGroupRowView, FaderGroupRow)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleEditorView::GetFaderGroupRowViewVisibility, FaderGroupRow));
	
	const int32 RowIndex = FaderGroupRow->GetRowIndex();
	if (ensureMsgf(RowIndex == 0 || FaderGroupRowViews.IsValidIndex(RowIndex - 1), TEXT("Unexpected, invalid row index when trying to add control console row.")))
	{
		FaderGroupRowViews.Insert(FaderGroupRowWidget, RowIndex);

		FaderGroupRowsVerticalBox->InsertSlot(RowIndex)
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(0.f, 8.f)
			[
				FaderGroupRowWidget
			];
	}
	else
	{
		FaderGroupRowViews.Add(FaderGroupRowWidget);

		FaderGroupRowsVerticalBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(0.f, 8.f)
			[
				FaderGroupRowWidget
			];
	}
}

void SDMXControlConsoleEditorView::OnFaderGroupRowRemoved()
{
	const UDMXControlConsoleData* ControlConsole = GetControlConsoleData();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't delete fader group row correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsole->GetFaderGroupRows();

	TArray<TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView>>FaderGroupRowViewsToRemove;
	for (TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView>& FaderGroupRowView : FaderGroupRowViews)
	{
		if (!FaderGroupRowView.IsValid())
		{
			continue;
		}

		const UDMXControlConsoleFaderGroupRow* FaderGroupRow = FaderGroupRowView.Pin()->GetFaderGroupRow();
		if (!FaderGroupRow || !FaderGroupRows.Contains(FaderGroupRow))
		{
			FaderGroupRowsVerticalBox->RemoveSlot(FaderGroupRowView.Pin().ToSharedRef());
			FaderGroupRowViewsToRemove.Add(FaderGroupRowView);
		}
	}

	FaderGroupRowViews.RemoveAll([&FaderGroupRowViewsToRemove](const TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView> FaderGroupRowView)
		{
			return !FaderGroupRowView.IsValid() || FaderGroupRowViewsToRemove.Contains(FaderGroupRowView);
		});
}

bool SDMXControlConsoleEditorView::IsFaderGroupRowContained(UDMXControlConsoleFaderGroupRow* FaderGroupRow)
{
	const TWeakObjectPtr<UDMXControlConsoleFaderGroupRow> FaderGroupRowWeakPtr = FaderGroupRow;

	auto IsContainedLambda = [FaderGroupRowWeakPtr](const TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView> FaderGroupRowView)
	{
		if (!FaderGroupRowView.IsValid())
		{
			return false;
		}

		const TWeakObjectPtr<UDMXControlConsoleFaderGroupRow> FaderGroupRow = FaderGroupRowView.Pin()->GetFaderGroupRow();
		if (!FaderGroupRow.IsValid())
		{
			return false;
		}

		return FaderGroupRow == FaderGroupRowWeakPtr;
	};

	return FaderGroupRowViews.ContainsByPredicate(IsContainedLambda);
}

void SDMXControlConsoleEditorView::OnSearchTextChanged(const FText& SearchText)
{
	const FString& SearchString = SearchText.ToString();

	using namespace UE::DMXControlConsoleEditor::FilterModel::Private;
	FFilterModel::Get().SetGlobalFilter(SearchString);

	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	if (!SearchString.IsEmpty() && EditorConsoleModel.GetAutoSelectFilteredElements())
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
		constexpr bool bNotifySelection = false;
		SelectionHandler->ClearSelection(bNotifySelection);

		constexpr bool bSelectOnlyFiltered = true;
		SelectionHandler->SelectAll(bSelectOnlyFiltered);
	}

	RequestUpdateDetailsViews();
}

ECheckBoxState SDMXControlConsoleEditorView::IsFilteredElementsAutoSelectChecked() const
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	const bool bAutoSelect = EditorConsoleModel.GetAutoSelectFilteredElements();
	return bAutoSelect ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SDMXControlConsoleEditorView::OnFilteredElementsAutoSelectStateChanged(ECheckBoxState CheckBoxState)
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.ToggleAutoSelectFilteredElements();
}

FReply SDMXControlConsoleEditorView::OnAddFirstFaderGroup()
{
	const TObjectPtr<UDMXControlConsoleData> ControlConsole = GetControlConsoleData();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't add fader group correctly.")))
	{
		return FReply::Unhandled();
	}

	const FScopedTransaction AddFaderGroupTransaction(LOCTEXT("AddFaderGroupTransaction", "Add Fader Group"));
	ControlConsole->PreEditChange(nullptr);

	ControlConsole->AddFaderGroupRow(0);

	ControlConsole->PostEditChange();
	return FReply::Handled();
}

void SDMXControlConsoleEditorView::OnFaderGroupsViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.SetFaderGroupsViewMode(ViewMode);
}

void SDMXControlConsoleEditorView::OnFadersViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.SetFadersViewMode(ViewMode);
}

void SDMXControlConsoleEditorView::OnSelectAll(bool bOnlyMatchingFilter) const
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
	SelectionHandler->SelectAll(bOnlyMatchingFilter);
}

void SDMXControlConsoleEditorView::OnClearAll()
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	EditorConsoleModel.ClearAll();
	UpdateFixturePatchVerticalBox();
}

void SDMXControlConsoleEditorView::OnSelectedPortsChanged()
{
	if (UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData())
	{
		if (PortSelector.IsValid())
		{
			const TArray<FDMXOutputPortSharedRef> SelectedOutputPorts = PortSelector->GetSelectedOutputPorts();
			ControlConsoleData->UpdateOutputPorts(SelectedOutputPorts);
		}
	}
}

void SDMXControlConsoleEditorView::OnControlConsoleDataPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->NamePrivate == UDMXControlConsoleData::GetDMXLibraryPropertyName())
	{
		UpdateFixturePatchVerticalBox();
	}
}

void SDMXControlConsoleEditorView::OnBrowseToAssetClicked()
{
	const UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	if (UDMXControlConsole* EditorConsole = EditorConsoleModel.GetEditorConsole())
	{
		TArray<UObject*> BrowseToObjects{ EditorConsole };
		GEditor->SyncBrowserToObjects(BrowseToObjects);
	}
}

void SDMXControlConsoleEditorView::OnConsoleLoaded()
{
	RequestUpdateDetailsViews();
	OnFaderGroupRowAdded();
	OnFaderGroupRowRemoved();
	UpdateFixturePatchVerticalBox();
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXControlConsoleEditorView::RestoreGlobalFilter));

	FSlateApplication::Get().SetKeyboardFocus(AsShared());
}

void SDMXControlConsoleEditorView::OnConsoleSaved()
{
	FSlateApplication::Get().SetKeyboardFocus(AsShared());
}

void SDMXControlConsoleEditorView::OnScrollIntoView(const UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup)
	{
		return;
	}

	if (HorizontalScrollBox.IsValid() && VerticalScrollBox.IsValid())
	{
		const UDMXControlConsoleFaderGroupRow& OwnerRow = FaderGroup->GetOwnerFaderGroupRowChecked();
		const TSharedPtr<SDMXControlConsoleEditorFaderGroupRowView> OwnerRowView = FindFaderGroupRowView(&OwnerRow);
		if (OwnerRowView.IsValid())
		{
			const TSharedPtr<SDMXControlConsoleEditorFaderGroupView> FaderGroupView = OwnerRowView->FindFaderGroupView(FaderGroup);
			if (FaderGroupView.IsValid())
			{
				VerticalScrollBox->ScrollDescendantIntoView(OwnerRowView, true, EDescendantScrollDestination::Center);
				HorizontalScrollBox->ScrollDescendantIntoView(FaderGroupView, true, EDescendantScrollDestination::TopOrLeft);
			}
		}
	}
}

void SDMXControlConsoleEditorView::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	if (IsWidgetInTab(PreviouslyActive, AsShared()))
	{
		UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
		SelectionHandler->ClearSelection();
	}

	if (!FadersDetailsView.IsValid())
	{
		return;
	}

	bool bDisableThrottle = false; 
	if (IsWidgetInTab(PreviouslyActive, FadersDetailsView))
	{
		FSlateThrottleManager::Get().DisableThrottle(bDisableThrottle);
	}

	if (IsWidgetInTab(NewlyActivated, FadersDetailsView))
	{
		bDisableThrottle = true;
		FSlateThrottleManager::Get().DisableThrottle(bDisableThrottle);
	}
}

bool SDMXControlConsoleEditorView::IsWidgetInTab(TSharedPtr<SDockTab> InDockTab, TSharedPtr<SWidget> InWidget) const
{
	if (InDockTab.IsValid())
	{
		// Tab content that should be a parent of this widget on some level
		TSharedPtr<SWidget> TabContent = InDockTab->GetContent();
		// Current parent being checked against
		TSharedPtr<SWidget> CurrentParent = InWidget;

		while (CurrentParent.IsValid())
		{
			if (CurrentParent == TabContent)
			{
				return true;
			}
			CurrentParent = CurrentParent->GetParentWidget();
		}

		// reached top widget (parent is invalid) and none was the tab
		return false;
	}

	return false;
}

EVisibility SDMXControlConsoleEditorView::GetFaderGroupRowViewVisibility(UDMXControlConsoleFaderGroupRow* FaderGroupRow) const
{
	if (!FaderGroupRow)
	{
		return EVisibility::Collapsed;
	}

	const auto IsAnyFaderGroupVisibleLambda = [](const UDMXControlConsoleFaderGroup* FaderGroup)
		{
			if (!FaderGroup)
			{
				return false;
			}

			return FaderGroup->IsActive() && FaderGroup->IsMatchingFilter();
		};

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow->GetFaderGroups();
	const bool bIsVisible = Algo::AnyOf(FaderGroups, IsAnyFaderGroupVisibleLambda);
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDMXControlConsoleEditorView::GetAddButtonVisibility() const
{
	bool bIsVisible = false;

	// Visible if there are no fader group rows and there's no global filter
	if (UDMXControlConsoleData* ControlConsoleData = GetControlConsoleData())
	{
		bIsVisible = 
			ControlConsoleData->GetFaderGroupRows().IsEmpty() ||
			ControlConsoleData->GetAllActiveFaderGroups().IsEmpty();

		if (GlobalFilterSearchBox.IsValid())
		{
			bIsVisible &= GlobalFilterSearchBox->GetText().IsEmpty();
		}
	}

	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDMXControlConsoleEditorView::GetDetailViewsSectionVisibility() const
{
	UDMXControlConsoleEditorModel& EditorConsoleModel = GetEditorConsoleModel();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel.GetSelectionHandler();
	bool bIsVisible = !SelectionHandler->GetSelectedFaderGroups().IsEmpty() || !SelectionHandler->GetSelectedFaders().IsEmpty();
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
