// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Animation/CurveSequence.h"
#include "Application/ThrottleManager.h"

class SStatusBar;

DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerOpened, TSharedRef<SStatusBar>&)
DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerDismissed, const TSharedPtr<SWidget>&)
DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerTargetHeightChanged, float);

class SWindow;
class SDockTab;
class SWidget;
class SMultiLineEditableTextBox;
class SDrawerOverlay;
class SHorizontalBox;

namespace StatusBarDrawerIds
{
	const FName ContentBrowser("ContentBrowser");
}

struct FStatusBarMessage
{
	FStatusBarMessage(const TAttribute<FText>& InMessageText, const TAttribute<FText>& InHintText, FStatusBarMessageHandle InHandle)
		: MessageText(InMessageText)
		, HintText(InHintText)
		, Handle(InHandle)
	{}

	TAttribute<FText> MessageText;
	TAttribute<FText> HintText;
	FStatusBarMessageHandle Handle;
};

struct FStatusBarDrawer
{
	FStatusBarDrawer(FName InUniqueId)
		: UniqueId(InUniqueId)
	{}

	FName UniqueId;
	FOnGetContent GetDrawerContentDelegate;
	FOnStatusBarDrawerOpened OnDrawerOpenedDelegate;
	FOnStatusBarDrawerDismissed OnDrawerDismissedDelegate;

	FText ButtonText;
	FText ToolTipText;
	const FSlateBrush* Icon;

	bool operator==(const FName& OtherId) const
	{
		return UniqueId == OtherId;
	}

	bool operator==(const FStatusBarDrawer& Other) const
	{
		return UniqueId == Other.UniqueId;
	}
};

class SStatusBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SStatusBar)	
	{}
		SLATE_EVENT(FSimpleDelegate, OnConsoleClosed)
	SLATE_END_ARGS()

public:
	~SStatusBar();

	/** SWidget interface */
	virtual bool SupportsKeyboardFocus() const { return false; }
	void Construct(const FArguments& InArgs, FName InStatusBarName, const TSharedRef<SDockTab> InParentTab);
	/** 
	 * Pushes a new status bar message
	 *
	 * @param InHandle	A handle to the message for clearing it later
	 * @param InMessage	The message to display
	 * @param InHintText	Optional hint text message.  This message will be highlighted to make it stand out
	 */
	void PushMessage(FStatusBarMessageHandle& InHandle, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText);

	/**
	 * Removes a message from the status bar.  When messages are removed the previous message on the stack (if any) is displayed
	 *
	 * @param InHandle	Handle to the status bar message to remove
	 */
	void PopMessage(FStatusBarMessageHandle& InHandle);

	/**
	 * Removes all messages from the status bar
	 */
	void ClearAllMessages();


	TSharedPtr<SDockTab> GetParentTab() const;
	bool FocusDebugConsole();
	bool IsDebugConsoleFocused() const;

	void RegisterDrawer(FStatusBarDrawer&& Drawer);

	void OpenDrawer(const FName DrawerId);

	/**
	 * Dismisses an open drawer with an animation.  The drawer contents are removed once the animation is complete
	 */
	void DismissDrawer(const TSharedPtr<SWidget>& NewlyFocusedWidget);

	void CloseDrawerImmediately();

	bool IsDrawerOpened(const FName DrawerId) const;
private:
	/** Called when global focus changes which is used to determine if we should close an opened content browser drawer */
	void OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	/** Called when active tab changes which is used to determine if we should close an opened content browser drawer */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	void OnDrawerHeightChanged(float TargetHeight);

	EVisibility GetHelpIconVisibility() const;

	bool GetContentBrowserExpanded() const;

	FText GetStatusBarMessage() const;

	TSharedRef<SWidget> MakeStatusBarDrawerButton(const FStatusBarDrawer& Drawer);

	TSharedRef<SWidget> MakeStatusBarToolBarWidget();
	TSharedRef<SWidget> MakeDebugConsoleWidget(FSimpleDelegate OnConsoleClosed);
	TSharedRef<SWidget> MakeStatusMessageWidget();
	void MakeDrawerButtons(TSharedRef<SHorizontalBox> DrawerBox);


	FReply OnDrawerButtonClicked(const FName DrawerId);

	void CreateAnimationTimerIfNeeded();

	EActiveTimerReturnType UpdateDrawerAnimation(double CurrentTime, float DeltaTime);

	void RegisterStatusBarMenu();

	void RegisterSourceControlStatus();
private:
	TArray<FStatusBarMessage> MessageStack;
	TSharedPtr<SMultiLineEditableTextBox> ConsoleEditBox;
	TWeakPtr<SDockTab> ParentTab;

	TArray<FStatusBarDrawer> RegisteredDrawers;

	TWeakPtr<SWindow> WindowWithOverlayContent;

	TPair<FName,TSharedPtr<SDrawerOverlay>> OpenedDrawerData;

	TSharedPtr<SHorizontalBox> DrawerBox;

	TSharedPtr<FActiveTimerHandle> DrawerOpenCloseTimer;
	FCurveSequence DrawerEasingCurve;
	FThrottleRequest AnimationThrottle;
	const FSlateBrush* UpArrow;
	const FSlateBrush* DownArrow;
	FName StatusBarName;
	FName StatusBarToolBarName;
};

