// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStatusBar.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Docking/SDockTab.h"
#include "TimerManager.h"
#include "Framework/Commands/Commands.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"
#include "OutputLogModule.h"
#include "SourceControlMenuHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSplitter.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "StatusBar"


class SDrawerOverlay : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDrawerOverlay)
	{
		_Clipping = EWidgetClipping::ClipToBounds;
		_ShadowOffset = FVector2D(10.0f, 20.0f);
	}
	SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(float, MinDrawerHeight)
		SLATE_ARGUMENT(float, MaxDrawerHeight)
		SLATE_ARGUMENT(float, TargetDrawerHeight)
		SLATE_EVENT(FOnStatusBarDrawerTargetHeightChanged, OnTargetHeightChanged)
		SLATE_ARGUMENT(FVector2D, ShadowOffset)
	SLATE_END_ARGS()

	void UpdateHeightInterp(float InAlpha)
	{
		float NewHeight = FMath::Lerp(0.0f, TargetHeight, InAlpha);

		SetHeight(NewHeight);
	}

	void Construct(const FArguments& InArgs)
	{
		CurrentHeight = 0;

		ShadowOffset = InArgs._ShadowOffset;
		ExpanderSize = 5.0f;

		SplitterStyle = &FAppStyle::Get().GetWidgetStyle<FSplitterStyle>("Splitter");

		MinHeight = InArgs._MinDrawerHeight;

		MaxHeight = InArgs._MaxDrawerHeight;

		TargetHeight = FMath::Clamp(InArgs._TargetDrawerHeight, MinHeight, MaxHeight);

		OnTargetHeightChanged = InArgs._OnTargetHeightChanged;

		BackgroundBrush = FAppStyle::Get().GetBrush("StatusBar.Background");
		ShadowBrush = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserShadow");

		bIsResizeHandleHovered = false;
		bIsResizing = false;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual FVector2D ComputeDesiredSize(float) const
	{
		return FVector2D(1.0f, TargetHeight + ShadowOffset.Y);
	}

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
		if (ArrangedChildren.Accepts(ChildVisibility))
		{
			ArrangedChildren.AddWidget(
				AllottedGeometry.MakeChild(
					ChildSlot.GetWidget(),
					ShadowOffset,
					FVector2D(AllottedGeometry.GetLocalSize().X - (ShadowOffset.X * 2), TargetHeight)
				)
			);
		}
	}

	virtual FReply OnMouseButtonDown(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = FReply::Unhandled();
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			const FGeometry RenderTransformedChildGeometry = GetRenderTransformedGeometry(AllottedGeometry);
			const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

			if (ResizeHandleGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
			{
				bIsResizing = true;
				InitialResizeGeometry = ResizeHandleGeometry;
				InitialHeightAtResize = CurrentHeight;
				ResizeThrottleHandle = FSlateThrottleManager::Get().EnterResponsiveMode();

				Reply = FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}

		return Reply;

	}

	FReply OnMouseButtonUp(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsResizing == true)
		{
			bIsResizing = false;
			FSlateThrottleManager::Get().LeaveResponsiveMode(ResizeThrottleHandle);

			OnTargetHeightChanged.ExecuteIfBound(TargetHeight);
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	FReply OnMouseMove(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent) override
	{
		const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

		bIsResizeHandleHovered = ResizeHandleGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());

		if (bIsResizing && this->HasMouseCapture() && !MouseEvent.GetCursorDelta().IsZero())
		{
			const FVector2D LocalMousePos = InitialResizeGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			const float DeltaHeight = (InitialResizeGeometry.GetLocalPositionAtCoordinates(FVector2D::ZeroVector) - LocalMousePos).Y;

			TargetHeight = FMath::Clamp(InitialHeightAtResize + DeltaHeight, MinHeight, MaxHeight);
			SetHeight(InitialHeightAtResize + DeltaHeight);


			return FReply::Handled();
		}
		else
		{
			return FReply::Unhandled();
		}
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SCompoundWidget::OnMouseLeave(MouseEvent);

		bIsResizeHandleHovered = false;
	}

	FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		return bIsResizing || bIsResizeHandleHovered ? FCursorReply::Cursor(EMouseCursor::ResizeUpDown) : FCursorReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		static FSlateColor ShadowColor = FAppStyle::Get().GetSlateColor("Colors.Foldout");

		const FGeometry RenderTransformedChildGeometry = GetRenderTransformedGeometry(AllottedGeometry);
		const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

		// Draw the resize handle
		if (bIsResizing || bIsResizeHandleHovered)
		{
			const FSlateBrush* SplitterBrush = &SplitterStyle->HandleHighlightBrush;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				ResizeHandleGeometry.ToPaintGeometry(),
				SplitterBrush,
				ESlateDrawEffect::None,
				SplitterBrush->GetTint(InWidgetStyle));
		}

		// Top Shadow
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			RenderTransformedChildGeometry.ToPaintGeometry(),
			ShadowBrush,
			ESlateDrawEffect::None,
			ShadowBrush->GetTint(InWidgetStyle));

		// Background
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			RenderTransformedChildGeometry.ToPaintGeometry(ShadowOffset, FVector2D(AllottedGeometry.GetLocalSize().X - (ShadowOffset.X * 2), TargetHeight)),
			BackgroundBrush,
			ESlateDrawEffect::None,
			BackgroundBrush->GetTint(InWidgetStyle));

		int32 OutLayerId = SCompoundWidget::OnPaint(Args, RenderTransformedChildGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

		// Bottom shadow
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			OutLayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(0.0f, AllottedGeometry.GetLocalSize().Y - ShadowOffset.Y), FVector2D(AllottedGeometry.GetLocalSize().X, ShadowOffset.Y)),
			ShadowBrush,
			ESlateDrawEffect::None,
			ShadowBrush->GetTint(InWidgetStyle));

		return OutLayerId;

	}

private:
	FGeometry GetRenderTransformedGeometry(const FGeometry& AllottedGeometry) const
	{
		return AllottedGeometry.MakeChild(FSlateRenderTransform(FVector2D(0.0f, TargetHeight - CurrentHeight)));
	}

	FGeometry GetResizeHandleGeometry(const FGeometry& AllottedGeometry) const
	{
		return GetRenderTransformedGeometry(AllottedGeometry).MakeChild(ShadowOffset - FVector2D(0.0f, ExpanderSize), FVector2D(AllottedGeometry.GetLocalSize().X-ShadowOffset.X*2, ExpanderSize));
	}

	void SetHeight(float NewHeight)
	{
		CurrentHeight = FMath::Clamp(NewHeight, MinHeight, TargetHeight);
	}
private:
	FGeometry InitialResizeGeometry;
	FOnStatusBarDrawerTargetHeightChanged OnTargetHeightChanged;
	const FSlateBrush* BackgroundBrush;
	const FSlateBrush* ShadowBrush;
	const FSplitterStyle* SplitterStyle;
	FVector2D ShadowOffset;
	FThrottleRequest ResizeThrottleHandle;
	float ExpanderSize;
	float CurrentHeight;
	float MinHeight;
	float MaxHeight;
	float TargetHeight;
	float InitialHeightAtResize;
	bool bIsResizing;
	bool bIsResizeHandleHovered;
};


SStatusBar::~SStatusBar()
{
	// Ensure the content browser is removed if we're being destroyed
	CloseDrawerImmediately();
}

void SStatusBar::Construct(const FArguments& InArgs, FName InStatusBarName, const TSharedRef<SDockTab> InParentTab)
{
	StatusBarName = InStatusBarName;
	StatusBarToolBarName = FName(*(StatusBarName.ToString() + ".ToolBar"));
	
	ParentTab = InParentTab;

	UpArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserUp");
	DownArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserDown");

	const FSlateBrush* StatusBarBackground = FAppStyle::Get().GetBrush("StatusBar.Background");

	DrawerEasingCurve = FCurveSequence(0.0f, 0.15f, ECurveEaseFunction::QuadOut);

	FSlateApplication::Get().OnFocusChanging().AddSP(this, &SStatusBar::OnGlobalFocusChanging);
	FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &SStatusBar::OnActiveTabChanged));
	FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &SStatusBar::OnActiveTabChanged));
	
	ChildSlot
	[
		SNew(SBox)
		.HeightOverride(FAppStyle::Get().GetFloat("StatusBar.Height"))
		[	
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(DrawerBox, SHorizontalBox)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SHorizontalBox)
				 +SHorizontalBox::Slot()
			    .AutoWidth()
			    .Padding(1.0f, 0.0f)
			    [
				    SNew(SBorder)
				    .Padding(0.0f)
				    .BorderImage(StatusBarBackground)
				    .VAlign(VAlign_Center)
				    .Padding(FMargin(6.0f, 0.0f))
				    [
					    MakeDebugConsoleWidget(InArgs._OnConsoleClosed)
				    ]
			    ]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(1.0f, 0.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage(StatusBarBackground)
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 0.0f))
					[
						MakeStatusMessageWidget()
					]
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage(StatusBarBackground)
					.VAlign(VAlign_Center)
					[
						MakeStatusBarToolBarWidget()
					]
				]
			]
		]
	];
}

void SStatusBar::PushMessage(FStatusBarMessageHandle& InHandle, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText)
{
	MessageStack.Emplace(InMessage, InHintText, InHandle);
}

void SStatusBar::PopMessage(FStatusBarMessageHandle& InHandle)
{
	if (InHandle.IsValid() && MessageStack.Num() > 0)
	{
		MessageStack.RemoveAll([InHandle](const FStatusBarMessage& Message)
			{
				return Message.Handle == InHandle;
			});
	}
}

void SStatusBar::ClearAllMessages()
{
	MessageStack.Empty();
}

EVisibility SStatusBar::GetHelpIconVisibility() const
{
	if (MessageStack.Num() > 0)
	{
		const FStatusBarMessage& MessageData = MessageStack.Top();

		const FText& Message = MessageData.MessageText.Get();
		const FText& HintText = MessageData.HintText.Get();

		return (!Message.IsEmpty() || !HintText.IsEmpty()) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	}

	return EVisibility::Collapsed;

}

TSharedPtr<SDockTab> SStatusBar::GetParentTab() const
{
	return ParentTab.Pin();
}

bool SStatusBar::FocusDebugConsole()
{
	return FSlateApplication::Get().SetKeyboardFocus(ConsoleEditBox, EFocusCause::SetDirectly);
}

bool SStatusBar::IsDebugConsoleFocused() const
{
	return ConsoleEditBox->HasKeyboardFocus();
}

void SStatusBar::OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
{
	// Sometimes when dismissing focus can change which will trigger this again
	static bool bIsRentrant = false;

	if(!bIsRentrant)
	{
		TGuardValue<bool> RentrancyGuard(bIsRentrant, true);

		TSharedRef<SWidget> ThisWidget = AsShared();

		TSharedPtr<SWidget> ActiveDrawerOverlayContent;
		if (!OpenedDrawerData.Key.IsNone())
		{
			ActiveDrawerOverlayContent = OpenedDrawerData.Value;
		}

		bool bShouldDismiss = false;
		// Do not close due to slow tasks as those opening send window activation events
		if (!GIsSlowTask && !FSlateApplication::Get().GetActiveModalWindow().IsValid() && ActiveDrawerOverlayContent.IsValid() && (!NewFocusedWidgetPath.ContainsWidget(ActiveDrawerOverlayContent.ToSharedRef()) && !NewFocusedWidgetPath.ContainsWidget(ThisWidget)))
		{
			if (TSharedPtr<SWidget> MenuHost = FSlateApplication::Get().GetMenuHostWidget())
			{
				FWidgetPath MenuHostPath;

				// See if the menu being opened is part of the content browser path and if so the menu should not be dismissed
				FSlateApplication::Get().GeneratePathToWidgetUnchecked(MenuHost.ToSharedRef(), MenuHostPath, EVisibility::All);
				if (!MenuHostPath.ContainsWidget(ActiveDrawerOverlayContent.ToSharedRef()))
				{
					bShouldDismiss = true;
				}
			}
			else
			{
				bShouldDismiss = true;
			}
		}

		if (bShouldDismiss)
		{
			DismissDrawer(NewFocusedWidget);
		}
	}
}

void SStatusBar::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	bool bShouldRemoveDrawer = false;
	if (!PreviouslyActive || !NewlyActivated)
	{
		// Remove the content browser if there is some invalid state with the tabs
		bShouldRemoveDrawer = true;
	}
	else if(NewlyActivated->GetTabRole() == ETabRole::MajorTab)
	{
		// Remove the content browser if a newly activated tab is a major tab
		bShouldRemoveDrawer = true;
	}
	else if (PreviouslyActive->GetTabManager() != NewlyActivated->GetTabManager())
	{
		// Remove the content browser if we're switching tab managers (indicates a new status bar is becoming active)
		bShouldRemoveDrawer = true;
	}

	if (bShouldRemoveDrawer)
	{
		CloseDrawerImmediately();
	}
}

FText SStatusBar::GetStatusBarMessage() const
{
	FText FullMessage;
	if (MessageStack.Num() > 0)
	{
		const FStatusBarMessage& MessageData = MessageStack.Top();

		const FText& Message = MessageData.MessageText.Get();
		const FText& HintText = MessageData.HintText.Get();

		FullMessage = HintText.IsEmpty() ? Message : FText::Format(LOCTEXT("StatusBarMessageFormat", "{0} <StatusBar.Message.InHintText>{1}</>"), Message, HintText);
	}

	return FullMessage;
}

TSharedRef<SWidget> SStatusBar::MakeStatusBarDrawerButton(const FStatusBarDrawer& Drawer)
{
	const FName DrawerId = Drawer.UniqueId;

	return
		SNew(SButton)
		.IsFocusable(false)
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("StatusBar.StatusBarButton"))
		.OnClicked(this, &SStatusBar::OnDrawerButtonClicked, DrawerId)
		.ToolTipText(Drawer.ToolTipText)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image_Lambda([this, DrawerId](){ return IsDrawerOpened(DrawerId) ? DownArrow : UpArrow; })
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(Drawer.Icon)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Text(Drawer.ButtonText)
			]
		];	
}

TSharedRef<SWidget> SStatusBar::MakeStatusBarToolBarWidget()
{
	RegisterStatusBarMenu();
	
	FToolMenuContext MenuContext;
	RegisterSourceControlStatus();

	return UToolMenus::Get()->GenerateWidget(StatusBarToolBarName, MenuContext);
}

TSharedRef<SWidget> SStatusBar::MakeDebugConsoleWidget(FSimpleDelegate OnConsoleClosed)
{
	FOutputLogModule& OutputLogModule = FModuleManager::LoadModuleChecked<FOutputLogModule>(TEXT("OutputLog"));

	return
		SNew(SBox)
		.WidthOverride(350.f)
		[
			OutputLogModule.MakeConsoleInputBox(ConsoleEditBox, OnConsoleClosed)
		];
}

TSharedRef<SWidget> SStatusBar::MakeStatusMessageWidget()
{
	return 
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FAppStyle::Get().GetBrush("StatusBar.HelpIcon"))
			.Visibility(this, &SStatusBar::GetHelpIconVisibility)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(5.0f, 0.0f)
		[
			SNew(SRichTextBlock)
			.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("StatusBar.Message.MessageText"))
			.Text(this, &SStatusBar::GetStatusBarMessage)
			.DecoratorStyleSet(&FAppStyle::Get())
		];
}


bool SStatusBar::IsDrawerOpened(const FName DrawerId) const
{
	return OpenedDrawerData.Key == DrawerId ? true : false;
}

FReply SStatusBar::OnDrawerButtonClicked(const FName DrawerId)
{
	if (!IsDrawerOpened(DrawerId))
	{
		OpenDrawer(DrawerId);
	}
	else
	{
		DismissDrawer(nullptr);
	}

	return FReply::Handled();
}


EActiveTimerReturnType SStatusBar::UpdateDrawerAnimation(double CurrentTime, float DeltaTime)
{
	check(OpenedDrawerData.Value.IsValid());

	OpenedDrawerData.Value->UpdateHeightInterp(DrawerEasingCurve.GetLerp());

	if (!DrawerEasingCurve.IsPlaying())
	{
		if(DrawerEasingCurve.IsAtStart())
		{
			CloseDrawerImmediately();
		}

		FSlateThrottleManager::Get().LeaveResponsiveMode(AnimationThrottle);
		DrawerOpenCloseTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

void SStatusBar::OnDrawerHeightChanged(float TargetHeight)
{
	TSharedPtr<SWindow> MyWindow = WindowWithOverlayContent.Pin();

	// Save the height has a percentage of the screen
	const float TargetDrawerHeightPct = TargetHeight / (MyWindow->GetSizeInScreen().Y / MyWindow->GetDPIScaleFactor());

	GConfig->SetFloat(TEXT("DrawerSizes"), *(StatusBarName.ToString() + TEXT(".") + OpenedDrawerData.Key.ToString()), TargetDrawerHeightPct, GEditorSettingsIni);
}

void SStatusBar::RegisterStatusBarMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered(StatusBarToolBarName))
	{
		return;
	}

	UToolMenu* ToolBar = ToolMenus->RegisterMenu(StatusBarToolBarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
	ToolBar->StyleName = "StatusBarToolBar";
}

void SStatusBar::RegisterSourceControlStatus()
{
	// Source Control preferences
	FSourceControlMenuHelpers::CheckSourceControlStatus();
	{
		UToolMenu* SourceControlMenu = UToolMenus::Get()->ExtendMenu(StatusBarToolBarName);
		FToolMenuSection& Section = SourceControlMenu->FindOrAddSection("SourceControl");

		Section.AddEntry(
			FToolMenuEntry::InitWidget(
				"SourceControl",
				FSourceControlMenuHelpers::MakeSourceControlStatusWidget(),
				FText::GetEmpty(),
				true,
				false
			));
	}
}

void SStatusBar::RegisterDrawer(FStatusBarDrawer&& Drawer)
{
	const int32 NumDrawers = RegisteredDrawers.Num();
	RegisteredDrawers.AddUnique(Drawer);

	if (RegisteredDrawers.Num() > NumDrawers)
	{
		const FSlateBrush* StatusBarBackground = FAppStyle::Get().GetBrush("StatusBar.Background");

		DrawerBox->AddSlot()
		.Padding(1.0f, 0.0f)
		.AutoWidth()
		[
			SNew(SBorder)
			.Padding(FMargin(2.0f, 0.0f))
			.BorderImage(StatusBarBackground)
			.VAlign(VAlign_Center)
			[
				MakeStatusBarDrawerButton(Drawer)
			]
		];
/*
			/ *+ SHorizontalBox::Slot()
				.Padding(1.0f, 0.0f)
				.AutoWidth()
				[
					SNew(SBorder)
					.Padding(FMargin(2.0f, 0.0f))
					.BorderImage(StatusBarBackground)
					.VAlign(VAlign_Center)
					[
						MakeStatusBarDrawerButton(ContentBrowserDrawerButton)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage(StatusBarBackground)
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 0.0f))
					[
						MakeDebugConsoleWidget(InArgs._OnConsoleClosed)
					]
				]* /
		}
*/

	}
}

void SStatusBar::OpenDrawer(const FName DrawerId)
{
	// Close any other open drawer
	if (DrawerId != OpenedDrawerData.Key)
	{
		DismissDrawer(nullptr);

		FStatusBarDrawer* DrawerData = RegisteredDrawers.FindByKey(DrawerId);

		if(DrawerData)
		{
			TSharedRef<SStatusBar> ThisStatusBar = SharedThis(this);

			TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());

			const float MaxDrawerHeight = MyWindow->GetSizeInScreen().Y * 0.90f;

			float TargetDrawerHeightPct = .33f;
			GConfig->GetFloat(TEXT("DrawerSizes"), *(StatusBarName.ToString()+TEXT(".")+DrawerData->UniqueId.ToString()), TargetDrawerHeightPct, GEditorSettingsIni);

			float TargetDrawerHeight = (MyWindow->GetSizeInScreen().Y * TargetDrawerHeightPct) / MyWindow->GetDPIScaleFactor();

			const float MinDrawerHeight = GetTickSpaceGeometry().GetLocalSize().Y + MyWindow->GetWindowBorderSize().Bottom;
			DrawerEasingCurve.Play(ThisStatusBar, false, DrawerEasingCurve.IsPlaying() ? DrawerEasingCurve.GetSequenceTime() : 0.0f, false);

			MyWindow->AddOverlaySlot()
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(10.0f, 20.0f, 10.0f, MinDrawerHeight))
				[
					SAssignNew(OpenedDrawerData.Value, SDrawerOverlay)
					.MinDrawerHeight(MinDrawerHeight)
					.TargetDrawerHeight(TargetDrawerHeight)
					.MaxDrawerHeight(MaxDrawerHeight)
					.OnTargetHeightChanged(this, &SStatusBar::OnDrawerHeightChanged)
					[
						DrawerData->GetDrawerContentDelegate.Execute()
					]
				];

			WindowWithOverlayContent = MyWindow;

			if (!DrawerOpenCloseTimer.IsValid())
			{
				AnimationThrottle = FSlateThrottleManager::Get().EnterResponsiveMode();
				DrawerOpenCloseTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(ThisStatusBar, &SStatusBar::UpdateDrawerAnimation));
			}

			OpenedDrawerData.Key = DrawerId;
			DrawerData->OnDrawerOpenedDelegate.ExecuteIfBound(ThisStatusBar);
		}
	}
}

void SStatusBar::DismissDrawer(const TSharedPtr<SWidget>& NewlyFocusedWidget)
{
	if (!OpenedDrawerData.Key.IsNone())
	{
		FStatusBarDrawer* Drawer = RegisteredDrawers.FindByKey(OpenedDrawerData.Key);

		if (DrawerEasingCurve.IsForward())
		{
			DrawerEasingCurve.Reverse();
		}

		if (!DrawerOpenCloseTimer.IsValid())
		{
			AnimationThrottle = FSlateThrottleManager::Get().EnterResponsiveMode();
			DrawerOpenCloseTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SStatusBar::UpdateDrawerAnimation));
		}

		Drawer->OnDrawerDismissedDelegate.ExecuteIfBound(NewlyFocusedWidget);
	}
}

void SStatusBar::CloseDrawerImmediately()
{
	if (!OpenedDrawerData.Key.IsNone())
	{
		TSharedRef<SWidget> DrawerOverlayContent = OpenedDrawerData.Value.ToSharedRef();

		// Remove the content browser from the window
		if (TSharedPtr<SWindow> Window = WindowWithOverlayContent.Pin())
		{
			Window->RemoveOverlaySlot(DrawerOverlayContent);
		}

		OpenedDrawerData = TPair<FName, TSharedPtr<SDrawerOverlay>>();

		WindowWithOverlayContent.Reset();

		if (DrawerOpenCloseTimer.IsValid())
		{
			FSlateThrottleManager::Get().LeaveResponsiveMode(AnimationThrottle);
			UnRegisterActiveTimer(DrawerOpenCloseTimer.ToSharedRef());
		}

		DrawerOpenCloseTimer.Reset();
		DrawerEasingCurve.JumpToStart();
		DrawerEasingCurve.Pause();
	}
}

#undef LOCTEXT_NAMESPACE