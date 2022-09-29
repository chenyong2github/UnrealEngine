// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterColorGradingDrawer.h"

#include "DisplayClusterColorGradingStyle.h"
#include "IDisplayClusterColorGrading.h"
#include "IDisplayClusterColorGradingDrawerSingleton.h"
#include "SDisplayClusterColorGradingObjectList.h"
#include "SDisplayClusterColorGradingColorWheelPanel.h"
#include "SDisplayClusterColorGradingDetailsPanel.h"

#include "IDisplayClusterOperator.h"
#include "IDisplayClusterOperatorViewModel.h"
#include "DisplayClusterOperatorStatusBarExtender.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "DisplayClusterConfigurationTypes.h"

#include "ColorCorrectRegion.h"
#include "ColorCorrectWindow.h"
#include "Engine/PostProcessVolume.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "WidgetDrawerConfig.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

SDisplayClusterColorGradingDrawer::~SDisplayClusterColorGradingDrawer()
{
	OperatorViewModel = IDisplayClusterOperator::Get().GetOperatorViewModel();
	OperatorViewModel->OnActiveRootActorChanged().RemoveAll(this);
	OperatorViewModel->OnDetailObjectsChanged().RemoveAll(this);

	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
	GEngine->OnLevelActorAdded().RemoveAll(this);
	GEngine->OnLevelActorDeleted().RemoveAll(this);

	GEditor->UnregisterForUndo(this);

	for (const FDisplayClusterColorGradingListItemRef& LevelColorGradingItem : LevelColorGradingItems)
	{
		if (LevelColorGradingItem->Component.IsValid())
		{
			UnbindBlueprintCompiledDelegate(LevelColorGradingItem->Component->GetClass());
		}

		if (LevelColorGradingItem->Actor.IsValid())
		{
			UnbindBlueprintCompiledDelegate(LevelColorGradingItem->Actor->GetClass());
		}
	}

	for (const FDisplayClusterColorGradingListItemRef& RootActorColorGradingItem : RootActorColorGradingItems)
	{
		if (RootActorColorGradingItem->Component.IsValid())
		{
			UnbindBlueprintCompiledDelegate(RootActorColorGradingItem->Component->GetClass());
		}

		if (RootActorColorGradingItem->Actor.IsValid())
		{
			UnbindBlueprintCompiledDelegate(RootActorColorGradingItem->Actor->GetClass());
		}
	}
}

void SDisplayClusterColorGradingDrawer::Construct(const FArguments& InArgs, bool bInIsInDrawer)
{
	ColorGradingDataModel = MakeShared<FDisplayClusterColorGradingDataModel>();
	ColorGradingDataModel->OnDataModelGenerated().AddSP(this, &SDisplayClusterColorGradingDrawer::OnColorGradingDataModelGenerated);

	bIsInDrawer = bInIsInDrawer;
	OperatorViewModel = IDisplayClusterOperator::Get().GetOperatorViewModel();
	OperatorViewModel->OnActiveRootActorChanged().AddSP(this, &SDisplayClusterColorGradingDrawer::OnActiveRootActorChanged);
	OperatorViewModel->OnDetailObjectsChanged().AddSP(this, &SDisplayClusterColorGradingDrawer::OnDetailObjectsChanged);

	FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SDisplayClusterColorGradingDrawer::OnObjectsReplaced);
	GEngine->OnLevelActorAdded().AddSP(this, &SDisplayClusterColorGradingDrawer::OnLevelActorAdded);
	GEngine->OnLevelActorDeleted().AddSP(this, &SDisplayClusterColorGradingDrawer::OnLevelActorDeleted);

	GEditor->RegisterForUndo(this);

	FillLevelColorGradingList();
	FillRootActorColorGradingList();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.0f, 0.0f))
		[
			// Splitter to divide the object list and the color panel
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(2.0f)

			+SSplitter::Slot()
			.Value(0.12f)
			[
				SNew(SVerticalBox)

				// Toolbar slot for the object list 
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 0)
				[
					SNew(SBorder)
					.Padding(FMargin(3))
					.BorderImage(bIsInDrawer ? FStyleDefaults::GetNoBrush() : FAppStyle::Get().GetBrush("Brushes.Panel"))
					[
						SNew(SBox)
						.HeightOverride(24.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 2, 0)
							[
								SNew(SCheckBox)
								.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
								.ToolTipText(LOCTEXT("ColorGradingDrawerModeToolTip", "Sets the drawer to display the color grading properties of the selected items"))
								.OnCheckStateChanged(this, &SDisplayClusterColorGradingDrawer::OnDrawerModeSelected, EDisplayClusterColorGradingDrawerMode::ColorGrading)
								.IsChecked(this, &SDisplayClusterColorGradingDrawer::IsDrawerModeSelected, EDisplayClusterColorGradingDrawerMode::ColorGrading)
								[
									SNew(SImage)
									.ColorAndOpacity(FSlateColor::UseForeground())
									.Image(FDisplayClusterColorGradingStyle::Get().GetBrush("ColorGradingDrawer.ColorGradingMode"))
								]
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 2, 0)
							[
								SNew(SCheckBox)
								.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
								.ToolTipText(LOCTEXT("DetailsViewDrawerModeToolTip", "Sets the drawer to display the additional in-camera VFX properties of the selected items"))
								.OnCheckStateChanged(this, &SDisplayClusterColorGradingDrawer::OnDrawerModeSelected, EDisplayClusterColorGradingDrawerMode::DetailsView)
								.IsChecked(this, &SDisplayClusterColorGradingDrawer::IsDrawerModeSelected, EDisplayClusterColorGradingDrawerMode::DetailsView)
								[
									SNew(SImage)
									.ColorAndOpacity(FSlateColor::UseForeground())
									.Image(FAppStyle::Get().GetBrush("EditorPreferences.TabIcon"))
								]
							]
						]
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSeparator)
					.Thickness(2.0f)
				]

				// Slot for the object lists, wrapped in expandable areas
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBox)
					.Padding(FMargin(4.f))
					[
						SNew(SBorder)
						.Padding(FMargin(0.0f))
						.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(SVerticalBox)

								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SExpandableArea)
									.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
									.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
									.HeaderPadding(FMargin(4.0f, 2.0f))
									.InitiallyCollapsed(false)
									.AllowAnimatedTransition(false)
									.Visibility_Lambda([this]() { return LevelColorGradingItems.Num() ? EVisibility::Visible : EVisibility::Collapsed; })
									.HeaderContent()
									[
										SNew(STextBlock)
										.Text(this, &SDisplayClusterColorGradingDrawer::GetCurrentLevelName)
										.TextStyle(FAppStyle::Get(), "ButtonText")
										.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
									]
									.BodyContent()
									[
										SAssignNew(LevelActorsList, SDisplayClusterColorGradingObjectList)
										.ColorGradingItemsSource(&LevelColorGradingItems)
										.OnSelectionChanged(this, &SDisplayClusterColorGradingDrawer::OnListSelectionChanged)
									]
								]

								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SExpandableArea)
									.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
									.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
									.HeaderPadding(FMargin(4.0f, 2.0f))
									.InitiallyCollapsed(false)
									.AllowAnimatedTransition(false)
									.Visibility_Lambda([this]() { return RootActorColorGradingItems.Num() ? EVisibility::Visible : EVisibility::Collapsed; })
									.HeaderContent()
									[
										SNew(STextBlock)
										.Text(this, &SDisplayClusterColorGradingDrawer::GetCurrentRootActorName)
										.TextStyle(FAppStyle::Get(), "ButtonText")
										.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
									]
									.BodyContent()
									[
										SAssignNew(RootActorList, SDisplayClusterColorGradingObjectList)
										.ColorGradingItemsSource(&RootActorColorGradingItems)
										.OnSelectionChanged(this, &SDisplayClusterColorGradingDrawer::OnListSelectionChanged)
									]
								]
							]
						]
					]
				]
			]

			+SSplitter::Slot()
			.Value(0.88f)
			[
				SNew(SVerticalBox)

				// Toolbar slot for the main drawer toolbar
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 0)
				[
					SNew(SBorder)
					.Padding(FMargin(3))
					.BorderImage(bIsInDrawer ? FStyleDefaults::GetNoBrush() : FAppStyle::Get().GetBrush("Brushes.Panel"))
					[
						SNew(SBox)
						.HeightOverride(24.0f)
						[
							SNew(SHorizontalBox)

							// Slot for the color grading group toolbar
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Center)
							[
								SAssignNew(ColorGradingGroupToolBarBox, SHorizontalBox)
								.Visibility(this, &SDisplayClusterColorGradingDrawer::GetColorGradingGroupToolBarVisibility)
							]
							

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SSpacer)
							]

							// Slot for the "Dock in Layout" button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								CreateDockInLayoutButton()
							]

							// Slot for the Settings button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(5.0f, 0.0f, 0.0f, 0.0f)
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								SNew(SComboButton)
								.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
								.HasDownArrow(false)
								.ButtonContent()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(4.0, 0.0f)
									[
										SNew(SImage)
										.ColorAndOpacity(FSlateColor::UseForeground())
										.Image(FAppStyle::Get().GetBrush("Icons.Settings"))
									]
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									.Padding(4.0, 0.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("Settings", "Settings"))
										.ColorAndOpacity(FSlateColor::UseForeground())
									]
								]
							]
						]
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSeparator)
					.Thickness(2.0f)
				]

				// Slot for the color panel
				+SVerticalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
					.Padding(FMargin(2.0f, 2.0f, 2.0f, 0.0f))
					.Visibility(this, &SDisplayClusterColorGradingDrawer::GetDrawerModeVisibility, EDisplayClusterColorGradingDrawerMode::ColorGrading)
					[
						SAssignNew(ColorWheelPanel, SDisplayClusterColorGradingColorWheelPanel)
						.ColorGradingDataModelSource(ColorGradingDataModel)
					]
				]

				// Slot for the details views
				+SVerticalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
					.Padding(FMargin(2.0f, 2.0f, 2.0f, 0.0f))
					.Visibility(this, &SDisplayClusterColorGradingDrawer::GetDrawerModeVisibility, EDisplayClusterColorGradingDrawerMode::DetailsView)
					[
						SAssignNew(DetailsPanel, SDisplayClusterColorGradingDetailsPanel)
						.ColorGradingDataModelSource(ColorGradingDataModel)
					]
				]
			]
		]
	];
}

void SDisplayClusterColorGradingDrawer::Refresh(bool bPreserveDrawerState)
{
	FDisplayClusterColorGradingDrawerState DrawerState = GetDrawerState();

	ColorGradingDataModel->Reset();

	FillLevelColorGradingList();
	FillRootActorColorGradingList();

	if (ColorWheelPanel.IsValid())
	{
		ColorWheelPanel->Refresh();
	}

	if (DetailsPanel.IsValid())
	{
		DetailsPanel->Refresh();
	}

	if (bPreserveDrawerState)
	{
		SetDrawerState(DrawerState);
	}
	else
	{
		SetDrawerStateToDefault();
	}
}

void SDisplayClusterColorGradingDrawer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bRefreshOnNextTick)
	{
		const bool bPreserveDrawerState = true;
		Refresh(bPreserveDrawerState);

		bRefreshOnNextTick = false;
	}
}

void SDisplayClusterColorGradingDrawer::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		const bool bPreserveDrawerState = true;
		Refresh(bPreserveDrawerState);
	}
}

void SDisplayClusterColorGradingDrawer::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		const bool bPreserveDrawerState = true;
		Refresh(bPreserveDrawerState);
	}
}

FDisplayClusterColorGradingDrawerState SDisplayClusterColorGradingDrawer::GetDrawerState() const
{
	FDisplayClusterColorGradingDrawerState DrawerState;

	DrawerState.SelectedColorGradingGroup = ColorGradingDataModel->GetSelectedColorGradingGroupIndex();
	DrawerState.SelectedColorGradingElement = ColorGradingDataModel->GetSelectedColorGradingElementIndex();
	DrawerState.DrawerMode = CurrentDrawerMode;

	if (ColorWheelPanel.IsValid())
	{
		ColorWheelPanel->GetDrawerState(DrawerState);
	}

	if (DetailsPanel.IsValid())
	{
		DetailsPanel->GetDrawerState(DrawerState);
	}

	if (LevelActorsList.IsValid())
	{
		TArray<FDisplayClusterColorGradingListItemRef> SelectedLevelItems = LevelActorsList->GetSelectedItems();

		for (const FDisplayClusterColorGradingListItemRef& SelectedLevelItem : SelectedLevelItems)
		{
			if (SelectedLevelItem.IsValid())
			{
				if (SelectedLevelItem->Component.IsValid())
				{
					DrawerState.SelectedObjects.Add(SelectedLevelItem->Component);
				}
				else if (SelectedLevelItem->Actor.IsValid())
				{
					DrawerState.SelectedObjects.Add(SelectedLevelItem->Actor);
				}
			}
		}
	}

	if (RootActorList.IsValid())
	{
		TArray<FDisplayClusterColorGradingListItemRef> SelectedRootActorItems = RootActorList->GetSelectedItems();

		for (const FDisplayClusterColorGradingListItemRef& SelectedRootActorItem : SelectedRootActorItems)
		{
			if (SelectedRootActorItem.IsValid())
			{
				if (SelectedRootActorItem->Component.IsValid())
				{
					DrawerState.SelectedObjects.Add(SelectedRootActorItem->Component);
				}
				else if (SelectedRootActorItem->Actor.IsValid())
				{
					DrawerState.SelectedObjects.Add(SelectedRootActorItem->Actor);
				}
			}
		}
	}

	return DrawerState;
}

void SDisplayClusterColorGradingDrawer::SetDrawerState(const FDisplayClusterColorGradingDrawerState& InDrawerState)
{
	TArray<FDisplayClusterColorGradingListItemRef> LevelItemsToSelect;
	TArray<FDisplayClusterColorGradingListItemRef> RootActorItemsToSelect;
	for (const TWeakObjectPtr<UObject>& SelectedObject : InDrawerState.SelectedObjects)
	{
		if (SelectedObject.IsValid())
		{
			FDisplayClusterColorGradingListItemRef* FoundColorGradingItemPtr = LevelColorGradingItems.FindByPredicate([&SelectedObject](const FDisplayClusterColorGradingListItemRef& ColorGradingItem)
			{
				return ColorGradingItem->Actor == SelectedObject || ColorGradingItem->Component == SelectedObject;
			});

			if (FoundColorGradingItemPtr)
			{
				LevelItemsToSelect.Add(*FoundColorGradingItemPtr);
			}
			else
			{
				FoundColorGradingItemPtr = RootActorColorGradingItems.FindByPredicate([&SelectedObject](const FDisplayClusterColorGradingListItemRef& ColorGradingItem)
				{
					return ColorGradingItem->Actor == SelectedObject || ColorGradingItem->Component == SelectedObject;
				});

				if (FoundColorGradingItemPtr)
				{
					RootActorItemsToSelect.Add(*FoundColorGradingItemPtr);
				}
			}
		}
	}

	// TODO: For now, since we don't support multiple color grading items selected at once, ensure either a level item or a root actor item is seleccted, not both
	if (LevelItemsToSelect.Num())
	{
		if (LevelActorsList.IsValid())
		{
			LevelActorsList->SetSelectedItems(LevelItemsToSelect);
		}
	}
	else if (RootActorItemsToSelect.Num())
	{
		if (RootActorList.IsValid())
		{
			RootActorList->SetSelectedItems(RootActorItemsToSelect);
		}
	}

	ColorGradingDataModel->SetSelectedColorGradingGroup(InDrawerState.SelectedColorGradingGroup);
	ColorGradingDataModel->SetSelectedColorGradingElement(InDrawerState.SelectedColorGradingElement);

	CurrentDrawerMode = InDrawerState.DrawerMode;

	if (ColorWheelPanel.IsValid())
	{
		ColorWheelPanel->SetDrawerState(InDrawerState);
	}

	if (DetailsPanel.IsValid())
	{
		DetailsPanel->SetDrawerState(InDrawerState);
	}
}

void SDisplayClusterColorGradingDrawer::SetDrawerStateToDefault()
{
	if (RootActorList.IsValid() && RootActorColorGradingItems.Num() > 0)
	{
		auto FindCCW = [](const TWeakObjectPtr<UObject>& Object)
		{
			return Object.IsValid() && Object->IsA<AColorCorrectionWindow>();
		};

		const bool bIsCCWSelected = OperatorViewModel->GetDetailObjects().ContainsByPredicate(FindCCW);

		// If there is a CCW in the currently selected detail objects, automatically select the last (most recently selected) one
		if (bIsCCWSelected)
		{
			int32 CCWIndex = OperatorViewModel->GetDetailObjects().FindLastByPredicate(FindCCW);
			if (CCWIndex > INDEX_NONE)
			{
				AColorCorrectionWindow* CCW = Cast<AColorCorrectionWindow>(OperatorViewModel->GetDetailObjects()[CCWIndex]);
				FDisplayClusterColorGradingListItemRef* ListItemPtr = RootActorColorGradingItems.FindByPredicate([CCW](const FDisplayClusterColorGradingListItemRef& ListItem)
				{
					return ListItem.IsValid() && ListItem->Actor.Get() == CCW;
				});

				if (ListItemPtr)
				{
					RootActorList->SetSelectedItems({ *ListItemPtr });
				}
			}
		}
		else
		{
			// The nDisplay stage actor is always the first item in the root actor color grading items list, so set that as the currently selected item
			RootActorList->SetSelectedItems({ RootActorColorGradingItems[0] });
		}
	}
}

TSharedRef<SWidget> SDisplayClusterColorGradingDrawer::CreateDockInLayoutButton()
{
	if (bIsInDrawer)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("DockInLayout_Tooltip", "Docks this panel in the current operator window, copying all settings from the drawer.\nThe drawer will still be usable."))
			.OnClicked(this, &SDisplayClusterColorGradingDrawer::DockInLayout)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Icons.Layout"))
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DockInLayout", "Dock in Layout"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}
	
	return SNullWidget::NullWidget;
}

FText SDisplayClusterColorGradingDrawer::GetCurrentLevelName() const
{
	if (OperatorViewModel->HasRootActor())
	{
		if (UWorld* World = OperatorViewModel->GetRootActor()->GetWorld())
		{
			return FText::FromString(World->GetMapName());
		}
	}

	return FText::GetEmpty();
}

FText SDisplayClusterColorGradingDrawer::GetCurrentRootActorName() const
{
	if (OperatorViewModel->HasRootActor())
	{
		return FText::FromString(OperatorViewModel->GetRootActor()->GetActorLabel());
	}

	return FText::GetEmpty();
}

void SDisplayClusterColorGradingDrawer::BindBlueprintCompiledDelegate(const UClass* Class)
{
	if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class))
	{
		if (!Blueprint->OnCompiled().IsBoundToObject(this))
		{
			Blueprint->OnCompiled().AddSP(this, &SDisplayClusterColorGradingDrawer::OnBlueprintCompiled);
		}
	}
}

void SDisplayClusterColorGradingDrawer::UnbindBlueprintCompiledDelegate(const UClass* Class)
{
	if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class))
	{
		Blueprint->OnCompiled().RemoveAll(this);
	}
}

// Macros to create the lambdas used for a specified UObject's color grading enabled property in the color grading object list
#define CREATE_IS_ENABLED_LAMBDA(Object, IsEnabledProperty) TAttribute<bool>::CreateLambda([Object]() { return (bool)IsEnabledProperty; })
#define CREATE_ON_ENABLED_CHANGED_LAMBDA(Object, IsEnabledProperty) FOnColorGradingItemEnabledChanged::CreateLambda([Object](FDisplayClusterColorGradingListItemRef ListItem, bool bIsEnabled) \
	{ \
		FScopedTransaction Transaction(LOCTEXT("ColorGradingToggledTransaction", "Color Grading Toggled")); \
		Object->Modify(); \
		IsEnabledProperty = bIsEnabled; \
	})

void SDisplayClusterColorGradingDrawer::FillLevelColorGradingList()
{
	for (const FDisplayClusterColorGradingListItemRef& LevelColorGradingItem : LevelColorGradingItems)
	{
		if (LevelColorGradingItem->Component.IsValid())
		{
			UnbindBlueprintCompiledDelegate(LevelColorGradingItem->Component->GetClass());
		}

		if (LevelColorGradingItem->Actor.IsValid())
		{
			UnbindBlueprintCompiledDelegate(LevelColorGradingItem->Actor->GetClass());
		}
	}

	LevelColorGradingItems.Empty();

	if (ADisplayClusterRootActor* RootActor = OperatorViewModel->GetRootActor())
	{
		if (UWorld* World = RootActor->GetWorld())
		{
			// Sorter that sorts the list items alphabetically by their display name
			auto AlphabeticalSort = [](const FDisplayClusterColorGradingListItemRef& A, const FDisplayClusterColorGradingListItemRef& B)
			{
				if (A.IsValid() && B.IsValid())
				{
					return *A < *B;
				}
				else
				{
					return false;
				}
			};

			// Add all post process volumes that are in the same world as the stage actor
			{
				TArray<FDisplayClusterColorGradingListItemRef> SortedPPVs;
				for (TActorIterator<APostProcessVolume> PPVIter(World); PPVIter; ++PPVIter)
				{
					APostProcessVolume* PostProcessVolume = *PPVIter;
					BindBlueprintCompiledDelegate(PostProcessVolume->GetClass());

					FDisplayClusterColorGradingListItemRef PPVListItemRef = MakeShared<FDisplayClusterColorGradingListItem>(PostProcessVolume);
					PPVListItemRef->IsItemEnabled = CREATE_IS_ENABLED_LAMBDA(PostProcessVolume, PostProcessVolume->bEnabled);
					PPVListItemRef->OnItemEnabledChanged = CREATE_ON_ENABLED_CHANGED_LAMBDA(PostProcessVolume, PostProcessVolume->bEnabled);

					SortedPPVs.Add(PPVListItemRef);
				}

				SortedPPVs.Sort(AlphabeticalSort);
				LevelColorGradingItems.Append(SortedPPVs);
			}

			// Add all color correction regions that are in the same world as the stage actor
			{
				TArray<FDisplayClusterColorGradingListItemRef> SortedCCRs;
				for (TActorIterator<AColorCorrectRegion> CCRIter(World); CCRIter; ++CCRIter)
				{
					AColorCorrectRegion* ColorCorrectRegion = *CCRIter;

					// Skip color correct windows, since those are stage managed actors and will be handled in a separate way
					if (ColorCorrectRegion->IsA<AColorCorrectionWindow>())
					{
						continue;
					}

					BindBlueprintCompiledDelegate(ColorCorrectRegion->GetClass());

					FDisplayClusterColorGradingListItemRef CCRListItemRef = MakeShared<FDisplayClusterColorGradingListItem>(ColorCorrectRegion);
					CCRListItemRef->IsItemEnabled = CREATE_IS_ENABLED_LAMBDA(ColorCorrectRegion, ColorCorrectRegion->Enabled);
					CCRListItemRef->OnItemEnabledChanged = CREATE_ON_ENABLED_CHANGED_LAMBDA(ColorCorrectRegion, ColorCorrectRegion->Enabled);

					SortedCCRs.Add(CCRListItemRef);
				}

				SortedCCRs.Sort(AlphabeticalSort);
				LevelColorGradingItems.Append(SortedCCRs);
			}
		}
	}

	if (LevelActorsList.IsValid())
	{
		LevelActorsList->RefreshList();
	}
}

void SDisplayClusterColorGradingDrawer::FillRootActorColorGradingList()
{
	for (const FDisplayClusterColorGradingListItemRef& RootActorColorGradingItem : RootActorColorGradingItems)
	{
		if (RootActorColorGradingItem->Component.IsValid())
		{
			UnbindBlueprintCompiledDelegate(RootActorColorGradingItem->Component->GetClass());
		}

		if (RootActorColorGradingItem->Actor.IsValid())
		{
			UnbindBlueprintCompiledDelegate(RootActorColorGradingItem->Actor->GetClass());
		}
	}

	RootActorColorGradingItems.Empty();

	if (ADisplayClusterRootActor* RootActor = OperatorViewModel->GetRootActor())
	{
		BindBlueprintCompiledDelegate(RootActor->GetClass());

		FDisplayClusterColorGradingListItemRef RootActorListItemRef = MakeShared<FDisplayClusterColorGradingListItem>(RootActor);
		RootActorListItemRef->IsItemEnabled = CREATE_IS_ENABLED_LAMBDA(RootActor, RootActor->GetConfigData()->StageSettings.EnableColorGrading);
		RootActorListItemRef->OnItemEnabledChanged = CREATE_ON_ENABLED_CHANGED_LAMBDA(RootActor, RootActor->GetConfigData()->StageSettings.EnableColorGrading);

		RootActorColorGradingItems.Add(RootActorListItemRef);

		auto AlphabeticalSort = [](const FDisplayClusterColorGradingListItemRef& A, const FDisplayClusterColorGradingListItemRef& B)
		{
			if (A.IsValid() && B.IsValid())
			{
				return *A < *B;
			}
			else
			{
				return false;
			}
		};

		// Add any ICVFX camera component the root actor has to the color grading list
		{
			TArray<FDisplayClusterColorGradingListItemRef> SortedICVFXCameras;
			RootActor->ForEachComponent<UDisplayClusterICVFXCameraComponent>(false, [this, RootActor, &SortedICVFXCameras](UDisplayClusterICVFXCameraComponent* ICVFXCameraComponent)
			{
				BindBlueprintCompiledDelegate(ICVFXCameraComponent->GetClass());

				FDisplayClusterColorGradingListItemRef ICVFXCameraListItemRef = MakeShared<FDisplayClusterColorGradingListItem>(RootActor, ICVFXCameraComponent);
				ICVFXCameraListItemRef->IsItemEnabled = CREATE_IS_ENABLED_LAMBDA(ICVFXCameraComponent, ICVFXCameraComponent->CameraSettings.EnableInnerFrustumColorGrading);
				ICVFXCameraListItemRef->OnItemEnabledChanged = CREATE_ON_ENABLED_CHANGED_LAMBDA(ICVFXCameraComponent, ICVFXCameraComponent->CameraSettings.EnableInnerFrustumColorGrading);

				SortedICVFXCameras.Add(ICVFXCameraListItemRef);
			});

			SortedICVFXCameras.Sort(AlphabeticalSort);
			RootActorColorGradingItems.Append(SortedICVFXCameras);
		}

		// Add any color correction window that is currently selected in the operator's details panel to the list
		{
			const TArray<TWeakObjectPtr<UObject>>& DetailObjects = OperatorViewModel->GetDetailObjects();

			TArray<FDisplayClusterColorGradingListItemRef> SortedCCWs;
			for (const TWeakObjectPtr<UObject>& Object : DetailObjects)
			{
				if (Object.IsValid())
				{
					if (AColorCorrectionWindow* CCW = Cast<AColorCorrectionWindow>(Object.Get()))
					{
						BindBlueprintCompiledDelegate(CCW->GetClass());

						FDisplayClusterColorGradingListItemRef ICVFXCameraListItemRef = MakeShared<FDisplayClusterColorGradingListItem>(CCW);
						ICVFXCameraListItemRef->IsItemEnabled = CREATE_IS_ENABLED_LAMBDA(CCW, CCW->Enabled);
						ICVFXCameraListItemRef->OnItemEnabledChanged = CREATE_ON_ENABLED_CHANGED_LAMBDA(CCW, CCW->Enabled);

						SortedCCWs.Add(ICVFXCameraListItemRef);
					}
				}
			}

			SortedCCWs.Sort(AlphabeticalSort);
			RootActorColorGradingItems.Append(SortedCCWs);
		}
	}

	if (RootActorList.IsValid())
	{
		RootActorList->RefreshList();
	}
}

void SDisplayClusterColorGradingDrawer::FillColorGradingGroupToolBar()
{
	if (ColorGradingGroupToolBarBox.IsValid())
	{
		ColorGradingGroupToolBarBox->ClearChildren();

		for (int32 Index = 0; Index < ColorGradingDataModel->ColorGradingGroups.Num(); ++Index)
		{
			const FDisplayClusterColorGradingDataModel::FColorGradingGroup& Group = ColorGradingDataModel->ColorGradingGroups[Index];
			ColorGradingGroupToolBarBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "DetailsView.SectionButton")
				.OnCheckStateChanged(this, &SDisplayClusterColorGradingDrawer::OnColorGradingGroupCheckedChanged, Index)
				.IsChecked(this, &SDisplayClusterColorGradingDrawer::IsColorGradingGroupSelected, Index)
				[
					SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.Text(Group.DisplayName)
				]
			];
		}

		if (ColorGradingDataModel->ColorGradingGroupToolBarWidget.IsValid())
		{
			ColorGradingGroupToolBarBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 2.0f, 0.0f)
			[
				ColorGradingDataModel->ColorGradingGroupToolBarWidget.ToSharedRef()
			];
		}
	}
}

ECheckBoxState SDisplayClusterColorGradingDrawer::IsDrawerModeSelected(EDisplayClusterColorGradingDrawerMode InDrawerMode) const
{
	return CurrentDrawerMode == InDrawerMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility SDisplayClusterColorGradingDrawer::GetDrawerModeVisibility(EDisplayClusterColorGradingDrawerMode InDrawerMode) const
{
	return CurrentDrawerMode == InDrawerMode ? EVisibility::Visible : EVisibility::Collapsed;
}

void SDisplayClusterColorGradingDrawer::OnDrawerModeSelected(ECheckBoxState State, EDisplayClusterColorGradingDrawerMode InDrawerMode)
{
	if (State == ECheckBoxState::Checked)
	{
		CurrentDrawerMode = InDrawerMode;
	}
}

EVisibility SDisplayClusterColorGradingDrawer::GetColorGradingGroupToolBarVisibility() const
{
	if (ColorGradingDataModel->bShowColorGradingGroupToolBar && CurrentDrawerMode == EDisplayClusterColorGradingDrawerMode::ColorGrading)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

ECheckBoxState SDisplayClusterColorGradingDrawer::IsColorGradingGroupSelected(int32 GroupIndex) const
{
	return ColorGradingDataModel->GetSelectedColorGradingGroupIndex() == GroupIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SDisplayClusterColorGradingDrawer::OnColorGradingGroupCheckedChanged(ECheckBoxState State, int32 GroupIndex)
{
	if (State == ECheckBoxState::Checked)
	{
		ColorGradingDataModel->SetSelectedColorGradingGroup(GroupIndex);
	}
}

void SDisplayClusterColorGradingDrawer::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	bool bNeedsFullRefresh = false;
	bool bNeedsListRefresh = false;

	const TArray<TWeakObjectPtr<UObject>> SelectedObjects = ColorGradingDataModel->GetObjects();

	for (const TPair<UObject*, UObject*>& Pair : OldToNewInstanceMap)
	{
		if (Pair.Key && Pair.Value)
		{
			// Must use GetEvenIfUnreachable on the weak pointers here because most of the time, the objects being replaced have already been marked for GC, and TWeakObjectPtr
			// will return nullptr from Get on GC-marked objects
			FDisplayClusterColorGradingListItemRef* FoundColorGradingItemPtr =
				RootActorColorGradingItems.FindByPredicate([&Pair](const FDisplayClusterColorGradingListItemRef& ColorGradingItem)
				{
					return ColorGradingItem->Actor.GetEvenIfUnreachable() == Pair.Key || ColorGradingItem->Component.GetEvenIfUnreachable() == Pair.Key;
				});

			if (FoundColorGradingItemPtr)
			{
				FDisplayClusterColorGradingListItemRef FoundColorGradingItem = *FoundColorGradingItemPtr;
				if (FoundColorGradingItem->Actor.GetEvenIfUnreachable() == Pair.Key)
				{
					FoundColorGradingItem->Actor = Cast<AActor>(Pair.Value);
				}
				else if (FoundColorGradingItem->Component.GetEvenIfUnreachable() == Pair.Key)
				{
					FoundColorGradingItem->Component = Cast<UActorComponent>(Pair.Value);
				}

				bNeedsListRefresh = true;
			}

			if (SelectedObjects.Contains(Pair.Key))
			{
				bNeedsFullRefresh = true;
			}
		}
	}

	if (bNeedsFullRefresh)
	{
		const bool bPreserveDrawerState = true;
		Refresh(bPreserveDrawerState);
	}
	else if (bNeedsListRefresh)
	{
		if (LevelActorsList.IsValid())
		{
			LevelActorsList->RefreshList();
		}

		if (RootActorList.IsValid())
		{
			RootActorList->RefreshList();
		}
	}
}

void SDisplayClusterColorGradingDrawer::OnLevelActorAdded(AActor* Actor)
{
	// Only refresh when the actor being added is being added to the root actor's world and is of a type this drawer cares about
	if (OperatorViewModel->HasRootActor())
	{
		if (UWorld* World = OperatorViewModel->GetRootActor()->GetWorld())
		{
			if (World == Actor->GetWorld())
			{
				if (Actor->IsA<ADisplayClusterRootActor>() || Actor->IsA<APostProcessVolume>() || Actor->IsA<AColorCorrectRegion>())
				{
					// Wait to refresh, as this event can be fired off for several actors in a row in certain cases, such as when the root actor is recompiled after a property change
					bRefreshOnNextTick = true;
				}
			}
		}
	}
}

void SDisplayClusterColorGradingDrawer::OnLevelActorDeleted(AActor* Actor)
{
	auto ContainsActorRef = [Actor](const FDisplayClusterColorGradingListItemRef& ColorGradingItem)
	{
		return ColorGradingItem->Actor.GetEvenIfUnreachable() == Actor;
	};

	// Only refresh if the actor being deleted is being referenced by the drawer
	if (RootActorColorGradingItems.ContainsByPredicate(ContainsActorRef) || LevelColorGradingItems.ContainsByPredicate(ContainsActorRef))
	{
		// Must wait for next tick to refresh because the actor has not actually been removed from the level at this point
		bRefreshOnNextTick = true;
	}
}

void SDisplayClusterColorGradingDrawer::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	const bool bPreserveDrawerState = true;
	Refresh(bPreserveDrawerState);
}

void SDisplayClusterColorGradingDrawer::OnActiveRootActorChanged(ADisplayClusterRootActor* NewRootActor)
{
	const bool bPreserveDrawerState = false;
	Refresh(bPreserveDrawerState);
}

void SDisplayClusterColorGradingDrawer::OnDetailObjectsChanged(const TArray<UObject*>& Objects)
{
	if (RootActorList.IsValid())
	{
		FillRootActorColorGradingList();

		SetDrawerStateToDefault();
	}
}

void SDisplayClusterColorGradingDrawer::OnColorGradingDataModelGenerated()
{
	FillColorGradingGroupToolBar();

	if (ColorWheelPanel.IsValid())
	{
		ColorWheelPanel->Refresh();
	}

	if (DetailsPanel.IsValid())
	{
		DetailsPanel->Refresh();
	}
}

void SDisplayClusterColorGradingDrawer::OnListSelectionChanged(TSharedRef<SDisplayClusterColorGradingObjectList> SourceList, FDisplayClusterColorGradingListItemRef SelectedItem, ESelectInfo::Type SelectInfo)
{
	TArray<FDisplayClusterColorGradingListItemRef> SelectedObjects = SourceList->GetSelectedItems();

	// When an item on one list is selected, clear the selection of the other list
	if (LevelActorsList == SourceList && SelectedObjects.Num())
	{
		if (RootActorList->GetSelectedItems().Num())
		{
			bUpdateDataModelOnSelectionChanged = false;
			RootActorList->SetSelectedItems(TArray<FDisplayClusterColorGradingListItemRef>());
			bUpdateDataModelOnSelectionChanged = true;
		}
	}
	else if (RootActorList == SourceList && SelectedObjects.Num())
	{
		if (LevelActorsList->GetSelectedItems().Num())
		{
			bUpdateDataModelOnSelectionChanged = false;
			LevelActorsList->SetSelectedItems(TArray<FDisplayClusterColorGradingListItemRef>());
			bUpdateDataModelOnSelectionChanged = true;
		}
	}

	if (bUpdateDataModelOnSelectionChanged)
	{
		TArray<UObject*> ObjectsToColorGrade;
		for (const FDisplayClusterColorGradingListItemRef& SelectedObject : SelectedObjects)
		{
			if (SelectedObject->Component.IsValid())
			{
				ObjectsToColorGrade.Add(SelectedObject->Component.Get());
			}
			else if (SelectedObject->Actor.IsValid())
			{
				ObjectsToColorGrade.Add(SelectedObject->Actor.Get());
			}
		}

		ColorGradingDataModel->SetObjects(ObjectsToColorGrade);

		// If the current drawer mode is the details view but the newly selected objects don't have any details sections to display,
		// switch to the color grading drawer mode
		if (CurrentDrawerMode == EDisplayClusterColorGradingDrawerMode::DetailsView && ColorGradingDataModel->DetailsSections.Num() == 0)
		{
			CurrentDrawerMode = EDisplayClusterColorGradingDrawerMode::ColorGrading;
		}
	}
}

FReply SDisplayClusterColorGradingDrawer::DockInLayout()
{
	IDisplayClusterColorGrading::Get().GetColorGradingDrawerSingleton().DockColorGradingDrawer();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE