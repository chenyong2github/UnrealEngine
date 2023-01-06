// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleView.h"

#include "DMXEditorModule.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsolePreset.h"
#include "DMXControlConsoleSelection.h"
#include "Commands/DMXControlConsoleCommands.h"
#include "Customizations/DMXControlConsoleDetails.h"
#include "Customizations/DMXControlConsoleFaderGroupDetails.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Style/DMXControlConsoleStyle.h"
#include "Views/SDMXControlConsoleFaderGroupRowView.h"
#include "Widgets/SDMXControlConsoleAddButton.h"
#include "Widgets/SDMXControlConsoleFixturePatchVerticalBox.h"
#include "Widgets/SDMXControlConsolePresetWidget.h"

#include "IDetailsView.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Application/ThrottleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Layout/Visibility.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleView"

SDMXControlConsoleView::~SDMXControlConsoleView()
{
	FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(OnActiveTabChangedDelegateHandle);
}

void SDMXControlConsoleView::Construct(const FArguments& InArgs)
{
	FDMXControlConsoleManager& ControlConsoleManager = FDMXControlConsoleManager::Get();
	ControlConsoleManager.GetOnControlConsoleLoaded().AddSP(this, &SDMXControlConsoleView::UpdateDetailsViews);
	ControlConsoleManager.GetOnControlConsoleLoaded().AddSP(this, &SDMXControlConsoleView::OnFaderGroupRowRemoved);
	ControlConsoleManager.GetOnControlConsoleLoaded().AddSP(this, &SDMXControlConsoleView::OnFaderGroupRowAdded);

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = ControlConsoleManager.GetSelectionHandler();
	SelectionHandler->GetOnSelectionChanged().AddSP(this, &SDMXControlConsoleView::UpdateDetailsViews);
	SelectionHandler->GetOnClearFaderGroupSelection().AddSP(this, &SDMXControlConsoleView::UpdateDetailsViews);
	SelectionHandler->GetOnClearFaderSelection().AddSP(this, &SDMXControlConsoleView::UpdateDetailsViews);

	OnActiveTabChangedDelegateHandle = FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &SDMXControlConsoleView::OnActiveTabChanged));

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

	ControlConsoleDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	FaderGroupsDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	FadersDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);

	FOnGetDetailCustomizationInstance ControlConsoleCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXControlConsoleDetails::MakeInstance);
	ControlConsoleDetailsView->RegisterInstancedCustomPropertyLayout(UDMXControlConsole::StaticClass(), ControlConsoleCustomizationInstance);
	ControlConsoleDetailsView->GetOnDisplayedPropertiesChanged().BindSP(this, &SDMXControlConsoleView::UpdateFixturePatchRows);

	FOnGetDetailCustomizationInstance FaderGroupsCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXControlConsoleFaderGroupDetails::MakeInstance);
	FaderGroupsDetailsView->RegisterInstancedCustomPropertyLayout(UDMXControlConsoleFaderGroup::StaticClass(), FaderGroupsCustomizationInstance);

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
				// DMX Control Console Section
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				.ResizeMode(ESplitterResizeMode::FixedSize)
				+ SSplitter::Slot()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)

					+ SScrollBox::Slot()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("NoBorder"))
						.Padding(10.f)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SScrollBox)
							.Orientation(Orient_Horizontal)
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
									SNew(SDMXControlConsoleAddButton)
									.OnClicked(this, &SDMXControlConsoleView::OnAddFirstFaderGroup)
									.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleView::GetAddButtonVisibility))
								]
							]

							+ SScrollBox::Slot()
							[
								SAssignNew(FaderGroupRowsVerticalBox, SVerticalBox)
							]
						]
					]
				]

				// Details View Section
				+ SSplitter::Slot()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
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

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SSeparator)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							ControlConsoleDetailsView.ToSharedRef()	
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(FixturePatchVerticalBox, SDMXControlConsoleFixturePatchVerticalBox)
						]
					]
				]
			]
		];
	
	UpdateDetailsViews();
}

UDMXControlConsole* SDMXControlConsoleView::GetControlConsole() const
{ 
	return FDMXControlConsoleManager::Get().GetDMXControlConsole();
}

void SDMXControlConsoleView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const TObjectPtr<UDMXControlConsole> ControlConsole = GetControlConsole();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't update DMX Control Console state correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsole->GetFaderGroupRows();

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

TSharedRef<SWidget> SDMXControlConsoleView::GenerateToolbar()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	FSlimHorizontalToolBarBuilder ToolbarBuilder = FSlimHorizontalToolBarBuilder(CommandList, FMultiBoxCustomization::None);
	
	ToolbarBuilder.BeginSection("Saving");
	{
		SAssignNew(ControlConsolePresetWidget, SDMXControlConsolePresetWidget);

		ToolbarBuilder.AddWidget(ControlConsolePresetWidget.ToSharedRef());
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Clearing");
	{
		ToolbarBuilder.AddToolBarButton(FDMXControlConsoleCommands::Get().ClearAll,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(),
			FName(TEXT("Clear All")));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Picking");
	{
		ToolbarBuilder.AddToolBarButton(FDMXControlConsoleCommands::Get().SendDMX,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FDMXControlConsoleStyle::Get().GetStyleSetName(), "DMXControlConsole.PlayDMX"),
			FName(TEXT("Send DMX")));

		ToolbarBuilder.AddToolBarButton(FDMXControlConsoleCommands::Get().StopDMX,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FDMXControlConsoleStyle::Get().GetStyleSetName(), "DMXControlConsole.StopPlayingDMX"),
			FName(TEXT("Stop Sending DMX")));
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

void SDMXControlConsoleView::UpdateDetailsViews()
{
	UDMXControlConsole* ControlConsole = GetControlConsole();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't update details view correctly.")))
	{
		return;
	}

	constexpr bool bForceRefresh = true;
	ControlConsoleDetailsView->SetObject(ControlConsole, bForceRefresh);

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroups = SelectionHandler->GetSelectedFaderGroups();
	FaderGroupsDetailsView->SetObjects(SelectedFaderGroups, bForceRefresh);

	const TArray<TWeakObjectPtr<UObject>> SelectedFaders = SelectionHandler->GetSelectedFaders();
	FadersDetailsView->SetObjects(SelectedFaders, bForceRefresh);
}

void SDMXControlConsoleView::UpdateFixturePatchRows()
{
	if (!FixturePatchVerticalBox.IsValid())
	{
		return;
	}

	FixturePatchVerticalBox->UpdateFixturePatchRows();
}

void SDMXControlConsoleView::OnFaderGroupRowAdded()
{
	const UDMXControlConsole* ControlConsole = GetControlConsole();
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

void SDMXControlConsoleView::AddFaderGroupRow(UDMXControlConsoleFaderGroupRow* FaderGroupRow)
{
	if (!ensureMsgf(FaderGroupRow, TEXT("Invalid fader group row, can't add new fader group row view correctly.")))
	{
		return;
	}

	const int32 RowIndex = FaderGroupRow->GetRowIndex();
	const TSharedRef<SDMXControlConsoleFaderGroupRowView> FaderGroupRowWidget = SNew(SDMXControlConsoleFaderGroupRowView, FaderGroupRow);
	FaderGroupRowViews.Insert(FaderGroupRowWidget, RowIndex);

	FaderGroupRowsVerticalBox->InsertSlot(RowIndex)
		.AutoHeight()
		.VAlign(VAlign_Top)
		.Padding(0.f, 8.f)
		[
			FaderGroupRowWidget
		];
}

void SDMXControlConsoleView::OnFaderGroupRowRemoved()
{
	const UDMXControlConsole* ControlConsole = GetControlConsole();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't delete fader group row correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsole->GetFaderGroupRows();

	TArray<TWeakPtr<SDMXControlConsoleFaderGroupRowView>>FaderGroupRowViewsToRemove;
	for (TWeakPtr<SDMXControlConsoleFaderGroupRowView>& FaderGroupRowView : FaderGroupRowViews)
	{
		if (!FaderGroupRowView.IsValid())
		{
			continue;
		}

		const UDMXControlConsoleFaderGroupRow* FaderGroupRow = FaderGroupRowView.Pin()->GetFaderGropuRow();
		if (!FaderGroupRow || !FaderGroupRows.Contains(FaderGroupRow))
		{
			FaderGroupRowsVerticalBox->RemoveSlot(FaderGroupRowView.Pin().ToSharedRef());
			FaderGroupRowViewsToRemove.Add(FaderGroupRowView);
		}
	}

	FaderGroupRowViews.RemoveAll([&FaderGroupRowViewsToRemove](const TWeakPtr<SDMXControlConsoleFaderGroupRowView> FaderGroupRowView)
		{
			return !FaderGroupRowView.IsValid() || FaderGroupRowViewsToRemove.Contains(FaderGroupRowView);
		});
}

bool SDMXControlConsoleView::IsFaderGroupRowContained(UDMXControlConsoleFaderGroupRow* FaderGroupRow)
{
	const TWeakObjectPtr<UDMXControlConsoleFaderGroupRow> FaderGroupRowWeakPtr = FaderGroupRow;

	auto IsContainedLambda = [FaderGroupRowWeakPtr](const TWeakPtr<SDMXControlConsoleFaderGroupRowView> FaderGroupRowView)
	{
		if (!FaderGroupRowView.IsValid())
		{
			return false;
		}

		const TWeakObjectPtr<UDMXControlConsoleFaderGroupRow> FaderGroupRow = FaderGroupRowView.Pin()->GetFaderGropuRow();
		if (!FaderGroupRow.IsValid())
		{
			return false;
		}

		return FaderGroupRow == FaderGroupRowWeakPtr;
	};

	return FaderGroupRowViews.ContainsByPredicate(IsContainedLambda);
}

FReply SDMXControlConsoleView::OnAddFirstFaderGroup()
{
	const TObjectPtr<UDMXControlConsole> ControlConsole = GetControlConsole();
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't add fader group correctly.")))
	{
		return FReply::Unhandled();
	}

	const FScopedTransaction ControlConsoleTransaction(LOCTEXT("ControlConsoleTransaction", "Add Fader Group"));
	ControlConsole->PreEditChange(nullptr);

	ControlConsole->AddFaderGroupRow(0);

	ControlConsole->PostEditChange();
	return FReply::Handled();
}

void SDMXControlConsoleView::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	if (IsWidgetInTab(PreviouslyActive, AsShared()))
	{
		const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
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

bool SDMXControlConsoleView::IsWidgetInTab(TSharedPtr<SDockTab> InDockTab, TSharedPtr<SWidget> InWidget) const
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

EVisibility SDMXControlConsoleView::GetAddButtonVisibility() const
{
	UDMXControlConsole* ControlConsole = GetControlConsole();
	if (!ControlConsole)
	{
		return EVisibility::Collapsed;
	}

	return ControlConsole->GetFaderGroupRows().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
