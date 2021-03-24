// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Animation/CurveSequence.h"
#include "Application/ThrottleManager.h"


DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerOpened, TSharedRef<SStatusBar>&)
DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerDismissed, const TSharedPtr<SWidget>&)
DECLARE_DELEGATE_OneParam(FOnStatusBarDrawerTargetHeightChanged, float);

class SWindow;
class SDockTab;
class SWidget;
class SMultiLineEditableTextBox;
class SDrawerOverlay;
class SHorizontalBox;
class SStatusBarProgressArea;
class SStatusBarProgressWidget;

namespace StatusBarDrawerIds
{
	const FName ContentBrowser("ContentBrowser");
}

/** Data payload for messages in the status bar */
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

/** Data payload for progress bars in the status bar */
struct FStatusBarProgress
{
	FStatusBarProgress(FText InDisplayText, double InStartTime, FProgressNotificationHandle InHandle, int32 InTotalWorkToDo)
		: DisplayText(InDisplayText)
		, StartTime(InStartTime)
		, TotalWorkToDo(InTotalWorkToDo)
		, TotalWorkDone(0)
		, Handle(InHandle)
	{}

	FText DisplayText;
	double StartTime;
	int32 TotalWorkToDo;
	int32 TotalWorkDone;
	FProgressNotificationHandle Handle;
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
	void PushMessage(FStatusBarMessageHandle InHandle, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText);

	/**
	 * Removes a message from the status bar.  When messages are removed the previous message on the stack (if any) is displayed
	 *
	 * @param InHandle	Handle to the status bar message to remove
	 */
	void PopMessage(FStatusBarMessageHandle InHandle);

	/**
	 * Removes all messages from the status bar
	 */
	void ClearAllMessages();

	/**
	 * Called when a progress notification begins
	 * @param Handle			Handle to the notification
	 * @param DisplayText		Display text used to describe the type of work to the user
	 * @param TotalWorkToDo		Arbitrary number of work units to perform.
	 */
	void StartProgressNotification(FProgressNotificationHandle InHandle, FText DisplayText, int32 TotalWorkToDo);

	/**
	 * Called when a notification should be updated.
	 * @param InHandle				Handle to the notification that was previously created with StartProgressNotification
	 * @param TotalWorkDone			The total number of work units done for the notification.
	 * @param UpdatedTotalWorkToDo	UpdatedTotalWorkToDo. This value will be 0 if the total work did not change
	 * @param UpdatedDisplayText	Updated display text of the notification. This value will be empty if the text did not change
	 */
	bool UpdateProgressNotification(FProgressNotificationHandle InHandle, int32 TotalWorkDone, int32 UpdatedTotalWorkToDo, FText UpdatedDisplayText);

	/**
	 * Called when a notification should be canceled
	 */
	bool CancelProgressNotification(FProgressNotificationHandle InHandle);

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
	TSharedRef<SWidget> MakeProgressBar();

	void MakeDrawerButtons(TSharedRef<SHorizontalBox> DrawerBox);

	FReply OnDrawerButtonClicked(const FName DrawerId);

	void CreateAnimationTimerIfNeeded();

	EActiveTimerReturnType UpdateDrawerAnimation(double CurrentTime, float DeltaTime);

	void RegisterStatusBarMenu();

	void RegisterSourceControlStatus();

	FStatusBarProgress* FindProgressNotification(FProgressNotificationHandle InHandle);

	void UpdateProgressStatus();
	void OpenProgressBar();
	void DismissProgressBar();
	
	TSharedRef<SWidget> OnGetProgressBarMenuContent();

private:
	TArray<FStatusBarMessage> MessageStack;
	TArray<FStatusBarProgress> ProgressNotifications;

	TSharedPtr<SMultiLineEditableTextBox> ConsoleEditBox;
	TWeakPtr<SDockTab> ParentTab;

	TArray<FStatusBarDrawer> RegisteredDrawers;

	TWeakPtr<SWindow> WindowWithOverlayContent;

	TPair<FName,TSharedPtr<SDrawerOverlay>> OpenedDrawerData;

	TSharedPtr<SHorizontalBox> DrawerBox;

	TSharedPtr<FActiveTimerHandle> DrawerOpenCloseTimer;

	TSharedPtr<SStatusBarProgressArea> ProgressBar;

	TWeakPtr<SNotificationItem> ActiveProgressNotification;

	TWeakPtr<SStatusBarProgressWidget> ActiveNotificationProgressWidget;


	FCurveSequence DrawerEasingCurve;
	FThrottleRequest AnimationThrottle;
	const FSlateBrush* UpArrow = nullptr;
	const FSlateBrush* DownArrow = nullptr;
	FName StatusBarName;
	FName StatusBarToolBarName;

	bool bAllowedToRefreshProgressNotification = false;

};

