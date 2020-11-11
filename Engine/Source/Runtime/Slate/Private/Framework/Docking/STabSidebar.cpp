// Copyright Epic Games, Inc. All Rights Reserved.

#include "STabSidebar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "SDockingTabWell.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/STabDrawer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/App.h"
#include "Widgets/Colors/SComplexGradient.h"

#define LOCTEXT_NAMESPACE "TabSidebar"

DECLARE_DELEGATE_OneParam(FOnTabDrawerButtonClicked, TSharedRef<SDockTab>);

class STabDrawerButton : public SCompoundWidget
{

	SLATE_BEGIN_ARGS(STabDrawerButton)
	{}
		SLATE_EVENT(FOnTabDrawerButtonClicked, OnDrawerButtonClicked)
		SLATE_EVENT(FOnGetContent, OnGetContextMenuContent)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedRef<SDockTab> ForTab, ESidebarLocation InLocation)
	{
		const FVector2D Size = FDockingConstants::GetMaxTabSizeFor(ETabRole::PanelTab);

		DockTabStyle = &FAppStyle::Get().GetWidgetStyle<FDockTabStyle>("Docking.Tab");

		// Sometimes tabs can be renamed so ensure that we pick up the rename
		ForTab->SetOnTabRenamed(SDockTab::FOnTabRenamed::CreateSP(this, &STabDrawerButton::OnTabRenamed));

		OnDrawerButtonClicked = InArgs._OnDrawerButtonClicked;
		OnGetContextMenuContent = InArgs._OnGetContextMenuContent;
		Tab = ForTab;
		Location = InLocation;

		static FLinearColor ActiveBorderColor = FAppStyle::Get().GetSlateColor("Docking.Tab.ActiveTabIndicatorColor").GetSpecifiedColor();
		static FLinearColor ActiveBorderColorTransparent = FLinearColor(ActiveBorderColor.R, ActiveBorderColor.G, ActiveBorderColor.B, 0.0f);
		static TArray<FLinearColor> GradientStops{ ActiveBorderColorTransparent, ActiveBorderColor, ActiveBorderColorTransparent };

		ChildSlot
		.Padding(0, 0, 0, 0)
		[
			SNew(SBox)
			.WidthOverride(Size.X)
			.HeightOverride(Size.Y)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SAssignNew(MainButton, SButton)
				.ContentPadding(FMargin(0.0f, DockTabStyle->TabPadding.Top, 0.0f, DockTabStyle->TabPadding.Bottom))
				.OnClicked_Lambda([this](){OnDrawerButtonClicked.ExecuteIfBound(Tab.ToSharedRef()); return FReply::Handled(); })
				.ForegroundColor(FSlateColor::UseForeground())
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(Location == ESidebarLocation::Left ? HAlign_Left : HAlign_Right)
					[
						SAssignNew(OpenIndicator, SComplexGradient)
						.DesiredSizeOverride(FVector2D(1.0f, 1.0f))
						.GradientColors(GradientStops)
						.Orientation(EOrientation::Orient_Horizontal)
					]
					+ SOverlay::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(0.0f, 5.0f, 0.0f, 5.0f)
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image(ForTab->GetTabIcon())
							.DesiredSizeOverride(FVector2D(16,16))
							//.RenderTransform(Rotate90)
							//.RenderTransformPivot(FVector2D(.5f, .5f))
						]
						+ SVerticalBox::Slot()
						.Padding(0, 10, 0, 0)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						[
							SAssignNew(Label, STextBlock)
							.TextStyle(&DockTabStyle->TabTextStyle)
							.Text(ForTab->GetTabLabel())
							.Clipping(EWidgetClipping::Inherit)
							.RenderTransformPivot(FVector2D(0.5f, 0.5f))
						]
					]
				]
			]
		];

		UpdateAppearance(nullptr);
	}

	void UpdateAppearance(const TSharedPtr<SDockTab> OpenedDrawer)
	{
		bool bShouldAppearOpened = OpenedDrawer.IsValid();

		switch (Location)
		{
		case ESidebarLocation::Left:
			bShouldAppearOpened ? Rotate90CounterClockwise() : Rotate90Clockwise();
			break;
		case ESidebarLocation::Right:
		default:
			bShouldAppearOpened ? Rotate90Clockwise() : Rotate90CounterClockwise();
			break;
		}

		if (OpenedDrawer == Tab)
		{
			// this button is the one with the tab that is actually opened so show the opened indicator
			OpenIndicator->SetVisibility(EVisibility::HitTestInvisible);
			MainButton->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Docking.SidebarButton.Opened"));
		}
		else
		{
			OpenIndicator->SetVisibility(EVisibility::Collapsed);
			MainButton->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Docking.SidebarButton.Closed"));
		}
	}

	void OnTabRenamed(TSharedRef<SDockTab> ForTab)
	{
		if (ensure(ForTab == Tab))
		{
			Label->SetText(ForTab->GetTabLabel());
		}
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const FVector2D Size = FDockingConstants::GetMaxTabSizeFor(ETabRole::PanelTab);

		FVector2D LocalDesiredSize = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
		return FVector2D(LocalDesiredSize.Y, FMath::Min(LocalDesiredSize.X, Size.X));
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnGetContextMenuContent.IsBound())
		{
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, OnGetContextMenuContent.Execute(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect::ContextMenu);
			return FReply::Handled();
		}
		else
		{
			return FReply::Unhandled();
		}
	}

	virtual FSlateColor GetForegroundColor() const
	{
		if (OpenIndicator->GetVisibility() != EVisibility::Collapsed)
		{
			return DockTabStyle->ActiveForegroundColor;
		}

		return FSlateColor::UseStyle();
	}
private:
	void Rotate90Clockwise()
	{
		Label->SetJustification(ETextJustify::Left);
		Label->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90))));
	}

	void Rotate90CounterClockwise()
	{
		Label->SetJustification(ETextJustify::Right);
		Label->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-90))));
	}
private:
	TSharedPtr<SDockTab> Tab;
	TSharedPtr<STextBlock> Label;
	TSharedPtr<SWidget> OpenIndicator;
	TSharedPtr<SButton> MainButton;
	FOnGetContent OnGetContextMenuContent;
	FOnTabDrawerButtonClicked OnDrawerButtonClicked;
	const FDockTabStyle* DockTabStyle;
	ESidebarLocation Location;
};

STabSidebar::~STabSidebar()
{
	// ensure all drawers are removed when closing a sidebar
	RemoveAllDrawers();
}

void STabSidebar::Construct(const FArguments& InArgs)
{
	Location = InArgs._Location;

#if WITH_EDITOR
	FSlateApplication::Get().OnWindowDPIScaleChanged().AddSP(this, &STabSidebar::OnWindowDPIScaleChanged);
#endif

	FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &STabSidebar::OnActiveTabChanged));

	ChildSlot
	.Padding(2.0f,0.0f)
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FAppStyle::Get().GetBrush("Docking.Sidebar.Background"))
		[
			SAssignNew(TabBox, SVerticalBox)
		]
	];

}

void STabSidebar::SetOffset(float Offset)
{
	ChildSlot.Padding(0.0f, Offset+4, 0.0f, 0.0f);
}

void STabSidebar::AddTab(TSharedRef<SDockTab> Tab)
{
	if(!ContainsTab(Tab))
	{
		SetVisibility(EVisibility::SelfHitTestInvisible);

		TSharedRef<STabDrawerButton> TabButton =
			SNew(STabDrawerButton, Tab, Location)
			.OnDrawerButtonClicked(this, &STabSidebar::OnTabDrawerButtonClicked)
			.OnGetContextMenuContent(this, &STabSidebar::OnGetTabDrawerContextMenuWidget, Tab);

		// Figure out the size this tab should be when opened later. We do it now when the tab still has valid geometry.  Once it is moved to the sidebar it will not.
		float TargetDrawerSizePct = Tab->GetParentDockTabStack()->GetTabSidebarSizeCoefficient(Tab);
		if (TargetDrawerSizePct == 0)
		{
			TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
			if (MyWindow.IsValid())
			{
				TargetDrawerSizePct = Tab->GetParentDockTabStack()->GetPaintSpaceGeometry().GetLocalSize().X / MyWindow->GetPaintSpaceGeometry().GetLocalSize().X;
				Tab->GetParentDockTabStack()->SetTabSidebarSizeCoefficient(Tab, TargetDrawerSizePct);
			}
		}

		TabBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				TabButton
			];

		Tabs.Emplace(Tab, TabButton);
	}
}

bool STabSidebar::RemoveTab(TSharedRef<SDockTab> TabToRemove)
{
	int32 FoundIndex = Tabs.IndexOfByPredicate(
		[TabToRemove](auto TabPair)
		{
			return TabPair.Key == TabToRemove;
		});


	if(FoundIndex != INDEX_NONE)
	{
		TPair<TSharedRef<SDockTab>, TSharedRef<STabDrawerButton>> TabPair = Tabs[FoundIndex];

		Tabs.RemoveAt(FoundIndex);
		TabBox->RemoveSlot(TabPair.Value);

		RemoveDrawer(TabToRemove);

		if (Tabs.Num() == 0)
		{
			SetVisibility(EVisibility::Collapsed);
		}
	}

	return FoundIndex != INDEX_NONE;
}

bool STabSidebar::RestoreTab(TSharedRef<SDockTab> TabToRestore)
{
	if(RemoveTab(TabToRestore))
	{
		TabToRestore->GetParentDockTabStack()->RestoreTabFromSidebar(TabToRestore);
		return true;
	}

	return false;
}

bool STabSidebar::ContainsTab(TSharedPtr<SDockTab> Tab) const
{
	return Tabs.ContainsByPredicate(
		[Tab](auto TabPair)
		{
			return TabPair.Key == Tab;
		});
}

TArray<FTabId> STabSidebar::GetAllTabIds() const
{
	TArray<FTabId> TabIds;
	for (auto TabPair : Tabs)
	{
		TabIds.Add(TabPair.Key->GetLayoutIdentifier());
	}

	return TabIds;
}

TArray<TSharedRef<SDockTab>> STabSidebar::GetAllTabs() const
{
	TArray<TSharedRef<SDockTab>> DockTabs;
	for (auto TabPair : Tabs)
	{
		DockTabs.Add(TabPair.Key);
	}

	return DockTabs;
}

bool STabSidebar::TryOpenSidebarDrawer(TSharedRef<SDockTab> ForTab)
{
	int32 FoundIndex = Tabs.IndexOfByPredicate(
		[ForTab](auto TabPair)
		{
			return TabPair.Key == ForTab;
		});

	if (FoundIndex != INDEX_NONE)
	{
		OpenDrawerNextFrame(ForTab);
		return true;
	}

	return false;
}

void STabSidebar::OnTabDrawerButtonClicked(TSharedRef<SDockTab> ForTab)
{
	OpenDrawerInternal(ForTab);
}

void STabSidebar::OnTabDrawerFocusLost(TSharedRef<STabDrawer> Drawer)
{
	Drawer->Close();
}

void STabSidebar::OnTabDrawerClosed(TSharedRef<STabDrawer> Drawer)
{
	RemoveDrawer(Drawer->GetTab());
}

void STabSidebar::OnTargetDrawerSizeChanged(TSharedRef<STabDrawer> Drawer, float NewSize)
{
	TSharedRef<SDockTab> Tab = Drawer->GetTab();
	TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (MyWindow.IsValid())
	{
		const float TargetDrawerSizePct = NewSize / MyWindow->GetPaintSpaceGeometry().GetLocalSize().X;
		Tab->GetParentDockTabStack()->SetTabSidebarSizeCoefficient(Tab, TargetDrawerSizePct);
	}
}

void STabSidebar::OnWindowDPIScaleChanged(TSharedRef<SWindow> WindowThatChanged)
{
	if (WindowThatChanged == WindowWithOverlayContent)
	{
		RemoveAllDrawers();
	}
}

void STabSidebar::OnActiveTabChanged(TSharedPtr<SDockTab> NewlyActivated, TSharedPtr<SDockTab> PreviouslyActive)
{
	// If a new major tab was activated remove any visible drawer instantly
	if (NewlyActivated.IsValid() && NewlyActivated->GetVisualTabRole() == ETabRole::MajorTab)
	{
		RemoveAllDrawers();
	}
}

TSharedRef<SWidget> STabSidebar::OnGetTabDrawerContextMenuWidget(TSharedRef<SDockTab> ForTab)
{
	const bool bCloseAfterSelection = true;
	const bool bCloseSelfOnly = false;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, nullptr, TSharedPtr<FExtender>(), bCloseSelfOnly, &FAppStyle::Get());
	{
		MenuBuilder.BeginSection("RestoreOptions", LOCTEXT("RestoreOptions", "Options"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("AutoHideTab", "Restore Tab"),
				LOCTEXT("HideTabWellTooltip", "Moves this tab out of the sidebar and back to a full tab where it previously was before it was added to the sidebar."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &STabSidebar::OnRestoreTab, ForTab)
				)
			);

		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("CloseOptions");
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CloseTab", "Close Tab"),
				LOCTEXT("CloseTabTooltip", "Close this tab, removing it from the sidebar and its parent tab well."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &STabSidebar::OnCloseTab, ForTab)
				)
			);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void STabSidebar::OnRestoreTab(TSharedRef<SDockTab> TabToRestore)
{
	RestoreTab(TabToRestore);
}

void STabSidebar::OnCloseTab(TSharedRef<SDockTab> TabToClose)
{
	if(TabToClose->RequestCloseTab())
	{
		RemoveTab(TabToClose);
		TabToClose->GetParentDockTabStack()->OnTabClosed(TabToClose, SDockingNode::TabRemoval_Closed);
	}
}

void STabSidebar::RemoveDrawer(TSharedRef<SDockTab> ForTab)
{
	TSharedRef<STabDrawer>* OpenedDrawer =
		OpenedDrawers.FindByPredicate(
			[&ForTab](TSharedRef<STabDrawer>& Drawer)
			{
				return ForTab == Drawer->GetTab();
			}
	);

	if(OpenedDrawer)
	{
		TSharedRef<STabDrawer> OpenedDrawerRef = *OpenedDrawer;

		if (TSharedPtr<SWindow> MyWindow = WindowWithOverlayContent.Pin())
		{
			bool bRemoveSuccessful = MyWindow->RemoveOverlaySlot(OpenedDrawerRef);
			ensure(bRemoveSuccessful);
		}

		OpenedDrawers.Remove(OpenedDrawerRef);
	}

	if (OpenedDrawers.Num() == 0)
	{
		WindowWithOverlayContent.Reset();
	}


	ForTab->OnTabDrawerClosed();

	UpdateDrawerAppearance();
}

void STabSidebar::CloseAllDrawers()
{
	PendingTabToOpen.Reset();

	// Closing drawers can remove them from the opened drawers list so copy the list first
	TArray<TSharedRef<STabDrawer>> OpenedDrawersCopy = OpenedDrawers;

	for (TSharedRef<STabDrawer>& Drawer : OpenedDrawersCopy)
	{
		Drawer->Close();
	}
}

void STabSidebar::RemoveAllDrawers()
{
	PendingTabToOpen.Reset();

	// Closing drawers can remove them from the opened drawers list so copy the list first
	TArray<TSharedRef<STabDrawer>> OpenedDrawersCopy = OpenedDrawers;

	for (TSharedRef<STabDrawer>& Drawer : OpenedDrawersCopy)
	{
		RemoveDrawer(Drawer->GetTab());
	}
}

EActiveTimerReturnType STabSidebar::OnOpenPendingDrawerTimer(double CurrentTime, float DeltaTime)
{
	if (TSharedPtr<SDockTab> Tab = PendingTabToOpen.Pin())
	{
		OpenDrawerInternal(Tab.ToSharedRef());
	}

	OpenPendingDrawerTimerHandle.Reset();
	PendingTabToOpen.Reset();

	return EActiveTimerReturnType::Stop;
}

void STabSidebar::OpenDrawerNextFrame(TSharedRef<SDockTab> ForTab)
{
	PendingTabToOpen = ForTab;
	if (!OpenPendingDrawerTimerHandle.IsValid())
	{
		OpenPendingDrawerTimerHandle = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &STabSidebar::OnOpenPendingDrawerTimer));
	}
}

void STabSidebar::OpenDrawerInternal(TSharedRef<SDockTab> ForTab)
{
	TSharedRef<STabDrawer>* OpenedDrawer =
		OpenedDrawers.FindByPredicate(
			[&ForTab](TSharedRef<STabDrawer>& Drawer)
			{
				return ForTab == Drawer->GetTab();
			}
	);

	if (OpenedDrawer)
	{
		// Drawer already opened close it
		(*OpenedDrawer)->Close();
	}
	else
	{
		PendingTabToOpen.Reset();

		TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());

		const FGeometry WindowGeometry = MyWindow->GetTickSpaceGeometry();
		const FGeometry MyGeometry = GetTickSpaceGeometry();

		// Calculate padding for the drawer itself
		const float MinDrawerSize = MyGeometry.GetLocalSize().X + MyWindow->GetWindowBorderSize().Left;

		const FVector2D ShadowOffset(8, 8);

		const float DPIScale = MyWindow->GetDPIScaleFactor();

		const float TopOffset = ((ChildSlot.SlotPadding.Get().Top + MyGeometry.GetAbsolutePosition().Y) - WindowGeometry.GetAbsolutePosition().Y);

		const float BottomOffset = ((ChildSlot.SlotPadding.Get().Bottom + WindowGeometry.GetAbsolutePositionAtCoordinates(FVector2D::UnitVector).Y) - MyGeometry.GetAbsolutePositionAtCoordinates(FVector2D::UnitVector).Y);

		FMargin SlotPadding(
			Location == ESidebarLocation::Left ? MinDrawerSize : 0.0f,
			TopOffset / DPIScale - ShadowOffset.Y,
			Location == ESidebarLocation::Right ? MinDrawerSize : 0.0f,
			BottomOffset / DPIScale - ShadowOffset.Y
		);

		const float MaxPct = .5f;
		const float MaxDrawerSize = MyWindow->GetSizeInScreen().X * 0.50f;

		float TargetDrawerSizePct = ForTab->GetParentDockTabStack()->GetTabSidebarSizeCoefficient(ForTab);
		TargetDrawerSizePct = FMath::Clamp(TargetDrawerSizePct, .0f, .5f);


		const float TargetDrawerSize = (MyWindow->GetSizeInScreen().X * TargetDrawerSizePct) / MyWindow->GetDPIScaleFactor();

		TSharedRef<STabDrawer> NewDrawer =
			SNew(STabDrawer, ForTab, Location == ESidebarLocation::Left ? ETabDrawerOpenDirection::Left : ETabDrawerOpenDirection::Right)
			.MinDrawerSize(MinDrawerSize)
			.TargetDrawerSize(TargetDrawerSize)
			.MaxDrawerSize(MaxDrawerSize)
			.OnDrawerFocusLost(this, &STabSidebar::OnTabDrawerFocusLost)
			.OnDrawerClosed(this, &STabSidebar::OnTabDrawerClosed)
			.OnTargetDrawerSizeChanged(this, &STabSidebar::OnTargetDrawerSizeChanged)
			[
				ForTab->GetContent()
			];

		check(!WindowWithOverlayContent.IsValid() || WindowWithOverlayContent == MyWindow);
		WindowWithOverlayContent = MyWindow;

		MyWindow->AddOverlaySlot()
			.Padding(SlotPadding)
			.HAlign(Location == ESidebarLocation::Left ? HAlign_Left : HAlign_Right)
			[
				NewDrawer
			];

		NewDrawer->Open();

		FSlateApplication::Get().SetKeyboardFocus(NewDrawer);

		OpenedDrawers.Add(NewDrawer);

		ForTab->OnTabDrawerOpened();
	}

	UpdateDrawerAppearance();
}

void STabSidebar::UpdateDrawerAppearance()
{
	TSharedPtr<SDockTab> OpenedTab;
	if (OpenedDrawers.Num() > 0)
	{
		OpenedTab = OpenedDrawers.Last()->GetTab();
	}

	for (auto& TabPair : Tabs)
	{
		TabPair.Value->UpdateAppearance(OpenedTab);
	}
}

#undef LOCTEXT_NAMESPACE
