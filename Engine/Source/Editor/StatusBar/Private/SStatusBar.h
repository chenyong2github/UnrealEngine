// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Animation/CurveSequence.h"
#include "Application/ThrottleManager.h"

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
	const FName OutputLog("OutputLog");
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

struct FOpenDrawerData
{
	FName DrawerId;
	TSharedPtr<SDrawerOverlay> DrawerOverlay;
	TWeakPtr<SWindow> WindowWithOverlayContent;

	bool IsValid() const
	{
		return !DrawerId.IsNone();
	}

	bool operator==(const FName InDrawerId) const
	{
		return DrawerId == InDrawerId;
	}
};

class SStatusBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SStatusBar)	
	{}
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

	/**
	 * Gets the owning major tab that this status bar is in
	 */
	TSharedPtr<SDockTab> GetParentTab() const;

	/**
	 * Registers a new drawer with this status bar. Registering will add a button to open and close the drawer
	 */
	void RegisterDrawer(FStatusBarDrawer&& Drawer, int32 SlotIndex = INDEX_NONE);

	/**
	 * Opens a drawer
	 * 
	 * @param DrawerId Name of the registered drawer to open 
	 */
	void OpenDrawer(const FName DrawerId);

	/**
	 * Dismisses an open drawer with an animation.  The drawer contents are removed once the animation is complete
	 * 
	 * @param NewlyFocusedWidget Optional widget to focus 
	 * @return true if any open drawer was dismissed
	 */
	bool DismissDrawer(const TSharedPtr<SWidget>& NewlyFocusedWidget);

	/**
	 * Closes a drawer immediately with no closing animation. Needed for when UI is shutting down or to prevent conflicts where a drawer is open in two places at once. 
	 * @param DrawerId The name of the drawer to close. If no name is specified all drawers are closed immediately
	 */
	void CloseDrawerImmediately(FName DrawerId = NAME_None);

	/** Is a specific registered drawer currently open */
	bool IsDrawerOpened(const FName DrawerId) const;

	/** Is any drawer other than the one specified by DrawerId opened */
	bool IsAnyOtherDrawerOpened(const FName DrawerId) const;

	/** Get unique name of this status bar */
	FName GetStatusBarName() const;

private:
	/** Called when global focus changes which is used to determine if we should close an opened content browser drawer */
	void OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	/** Called when active tab changes which is used to determine if we should close an opened content browser drawer */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	void OnDrawerHeightChanged(float TargetHeight);

	EVisibility GetHelpIconVisibility() const;

	FText GetStatusBarMessage() const;

	TSharedRef<SWidget> MakeStatusBarDrawerButton(const FStatusBarDrawer& Drawer);
	TSharedRef<SWidget> MakeStatusBarToolBarWidget();
	TSharedRef<SWidget> MakeStatusMessageWidget();
	TSharedRef<SWidget> MakeProgressBar();

	FReply OnDrawerButtonClicked(const FName DrawerId);

	void RegisterStatusBarMenu();

	void RegisterSourceControlStatus();

	FStatusBarProgress* FindProgressNotification(FProgressNotificationHandle InHandle);

	void UpdateProgressStatus();
	void OpenProgressBar();
	void DismissProgressBar();
	
	TSharedRef<SWidget> OnGetProgressBarMenuContent();

	void CloseDrawerImmediatelyInternal(const FOpenDrawerData& Data);

	FString GetStatusBarSerializableName() const;
private:
	TArray<FStatusBarMessage> MessageStack;
	TArray<FStatusBarProgress> ProgressNotifications;

	TWeakPtr<SDockTab> ParentTab;

	TArray<FStatusBarDrawer> RegisteredDrawers;

	FOpenDrawerData OpenedDrawer;

	TArray<FOpenDrawerData> DismissingDrawers;

	TSharedPtr<SHorizontalBox> DrawerBox;

	TSharedPtr<SStatusBarProgressArea> ProgressBar;

	TWeakPtr<SNotificationItem> ActiveProgressNotification;

	TWeakPtr<SStatusBarProgressWidget> ActiveNotificationProgressWidget;

	const FSlateBrush* UpArrow = nullptr;
	const FSlateBrush* DownArrow = nullptr;
	FName StatusBarName;
	FName StatusBarToolBarName;

	bool bAllowedToRefreshProgressNotification = false;

};

