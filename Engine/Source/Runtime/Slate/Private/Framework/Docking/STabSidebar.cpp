// Copyright Epic Games, Inc. All Rights Reserved.

#include "STabSidebar.h"
#include "Widgets/Input/SButton.h"
#include "SDockingTabWell.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/STabDrawer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Misc/App.h"
#include "Widgets/Colors/SComplexGradient.h"
#include "Widgets/Text/SlateTextBlockLayout.h"

#define LOCTEXT_NAMESPACE "TabSidebar"

DECLARE_DELEGATE_OneParam(FOnTabDrawerButtonClicked, TSharedRef<SDockTab>);

/**
 * Vertical text block for use in the tab drawer button.
 * Text is aligned to the top of the widget if it fits without clipping;
 * otherwise it is ellipsized and fills the widget height.
 */
class STabDrawerTextBlock : public SLeafWidget
{
public:
	enum class ERotation
	{
		Clockwise,
		CounterClockwise,
	};

	SLATE_BEGIN_ARGS(STabDrawerTextBlock)
		: _Text()
		, _TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		, _Rotation(ERotation::Clockwise)
		, _OverflowPolicy()
		{}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(ERotation, Rotation)
		SLATE_ARGUMENT(TOptional<ETextOverflowPolicy>, OverflowPolicy)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Text = InArgs._Text;
		TextStyle = *InArgs._TextStyle;
		Rotation = InArgs._Rotation;
		TextLayoutCache = MakeUnique<FSlateTextBlockLayout>(
			this, FTextBlockStyle::GetDefault(), TOptional<ETextShapingMethod>(), TOptional<ETextFlowDirection>(),
			FCreateSlateTextLayout(), FPlainTextLayoutMarshaller::Create(), nullptr);
		TextLayoutCache->SetTextOverflowPolicy(InArgs._OverflowPolicy.IsSet() ? InArgs._OverflowPolicy : TextStyle.OverflowPolicy);
	}

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
	{
		// We're going to figure out the bounds of the corresponding horizontal text, and then rotate it into a vertical orientation.
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const FVector2D DesiredHorizontalTextSize = TextLayoutCache->GetDesiredSize();
		const FVector2D ActualHorizontalTextSize(FMath::Min(DesiredHorizontalTextSize.X, LocalSize.Y), FMath::Min(DesiredHorizontalTextSize.Y, LocalSize.X));

		// Now determine the center of the vertical text by rotating the dimensions of the horizontal text.
		// The center should align it to the top of the widget.
		const FVector2D VerticalTextSize(ActualHorizontalTextSize.Y, ActualHorizontalTextSize.X);
		const FVector2D VerticalTextCenter = VerticalTextSize / 2.0f;

		// Now determine where the horizontal text should be positioned so that it is centered on the vertical text:
		//      +-+
		//      |v|
		//      |e|
		// [ horizontal ]
		//      |r|
		//      |t|
		//      +-+
		const FVector2D HorizontalTextPosition = VerticalTextCenter - ActualHorizontalTextSize / 2.0f;

		// Define the text's geometry using the horizontal bounds, then rotate it 90/-90 degrees into place to become vertical.
		const FSlateRenderTransform RotationTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(Rotation.Get() == ERotation::Clockwise ? 90 : -90))));
		const FGeometry TextGeometry = AllottedGeometry.MakeChild(ActualHorizontalTextSize, FSlateLayoutTransform(HorizontalTextPosition), RotationTransform, FVector2D(0.5f, 0.5f));

		return TextLayoutCache->OnPaint(Args, TextGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		// The text's desired size reflects the horizontal/untransformed text.
		// Switch the dimensions for vertical text.
		const FVector2D DesiredHorizontalTextSize = TextLayoutCache->ComputeDesiredSize(
			FSlateTextBlockLayout::FWidgetDesiredSizeArgs(
				Text.Get(),
				FText(),
				0.0f,
				false,
				ETextWrappingPolicy::DefaultWrapping,
				ETextTransformPolicy::None,
				FMargin(),
				1.0f,
				ETextJustify::Left),
			LayoutScaleMultiplier, TextStyle);
		return FVector2D(DesiredHorizontalTextSize.Y, DesiredHorizontalTextSize.X);
	}

	void SetText(TAttribute<FText> InText)
	{
		Text = InText;
	}

	void SetRotation(TAttribute<ERotation> InRotation)
	{
		Rotation = InRotation;
	}

private:
	TAttribute<FText> Text;
	FTextBlockStyle TextStyle;
	TAttribute<ERotation> Rotation;
	TUniquePtr<FSlateTextBlockLayout> TextLayoutCache;
};

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
			.WidthOverride(Size.Y) // Swap desired dimensions for a vertical tab
			.HeightOverride(Size.X)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SAssignNew(MainButton, SButton)
				.ToolTip(ForTab->GetToolTip() ? ForTab->GetToolTip() : TAttribute<TSharedPtr<IToolTip>>())
				.ToolTipText(ForTab->GetToolTip() ? TAttribute<FText>() : ForTab->GetTabLabel())
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
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Center)
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
						.Padding(0.0f, 5.0f, 0.0f, 0.0f)
						.FillHeight(1.0f)
						[
							SAssignNew(Label, STabDrawerTextBlock)
								.TextStyle(&DockTabStyle->TabTextStyle)
								.Text(ForTab->GetTabLabel())
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Clipping(EWidgetClipping::ClipToBounds)
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

		STabDrawerTextBlock::ERotation Rotation;
		switch (Location)
		{
		case ESidebarLocation::Left:
			Rotation = bShouldAppearOpened ? STabDrawerTextBlock::ERotation::CounterClockwise : STabDrawerTextBlock::ERotation::Clockwise;
			break;
		case ESidebarLocation::Right:
		default:
			Rotation = bShouldAppearOpened ? STabDrawerTextBlock::ERotation::Clockwise : STabDrawerTextBlock::ERotation::CounterClockwise;
			break;
		}

		check(Label);
		Label->SetRotation(Rotation);

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

			if (TSharedPtr<IToolTip> ToolTip = ForTab->GetToolTip())
			{
				MainButton->SetToolTip(ToolTip);
			}
			else
			{
				MainButton->SetToolTipText(ForTab->GetTabLabel());
			}
		}
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
	TSharedPtr<SDockTab> Tab;
	TSharedPtr<STabDrawerTextBlock> Label;
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
			// Make the tabs evenly fill the sidebar until they reach the max size
			.FillHeight(1.0f)
			.MaxHeight(FDockingConstants::GetMaxTabSizeFor(ETabRole::PanelTab).X)
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

		const float TopOffset = ((ChildSlot.GetPadding().Top + MyGeometry.GetAbsolutePosition().Y) - WindowGeometry.GetAbsolutePosition().Y);

		const float BottomOffset = ((ChildSlot.GetPadding().Bottom + WindowGeometry.GetAbsolutePositionAtCoordinates(FVector2D::UnitVector).Y) - MyGeometry.GetAbsolutePositionAtCoordinates(FVector2D::UnitVector).Y);

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
