// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Framework/Notifications/NotificationManager.h"

#include "StatusBarSubsystem.generated.h"


class SStatusBar;
class SWindow;
class SWidget;
class SDockTab;

template<typename ObjectType> 
class TAttribute;

struct FStatusBarMessageHandle
{
	friend class UStatusBarSubsystem;

	FStatusBarMessageHandle()
		: Id(INDEX_NONE)
	{}

	bool IsValid()
	{
		return Id != INDEX_NONE;
	}

	void Reset()
	{
		Id = INDEX_NONE;
	}

	bool operator==(const FStatusBarMessageHandle& OtherHandle) const
	{
		return Id == OtherHandle.Id;
	}
private:
	FStatusBarMessageHandle(int32 InId)
		: Id(InId)
	{}

	int32 Id;
};

UCLASS()
class STATUSBAR_API UStatusBarSubsystem : public UEditorSubsystem, public IProgressNotificationHandler
{
	GENERATED_BODY()

public:

	/**
	 *	Prepares for use
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;

	/**
	 *	Internal cleanup
	 */
	virtual void Deinitialize() override final;

	/**
	 * Focuses the debug console on the status bar for status bar residing in the passed in parent window 
	 *
	 * @param ParentWindow The parent window of the status bar 
	 * @return true of a status bar debug console was found and focused 
	 */
	bool FocusDebugConsole(TSharedRef<SWindow> ParentWindow);

	/**
	 * Opens the content browser drawer for a status bar residing in the active window 
	 * 
	 * @return true if the content browser was opened or false if no status bar in the active window was found
	 */
	bool OpenContentBrowserDrawer();

	/**
	 * Forces the drawer to dismiss. Usually it dismisses with focus. Only call this if there is some reason an open drawer would be invalid for the current state of the editor.
	 */
	bool ForceDismissDrawer();

	/** 
	 * Creates a new instance of a status bar widget
	 *
	 * @param StatusBarName	The name of the status bar for updating it later.
	 */
	TSharedRef<SWidget> MakeStatusBarWidget(FName StatusBarName, const TSharedRef<SDockTab>& InParentTab);

	/** 
	 * Pushes a new status bar message
	 *
	 * @param StatusBarName	The name of the status bar to push messages to
	 * @param InMessage		The message to display
	 * @param InHintText	Optional hint text message.  This message will be highlighted to make it stand out
	 * @return	A handle to the message for clearing it later
	 */
	FStatusBarMessageHandle PushStatusBarMessage(FName StatusBarName, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText);
	FStatusBarMessageHandle PushStatusBarMessage(FName StatusBarName, const TAttribute<FText>& InMessage);

	/**
	 * Removes a message from the status bar.  When messages are removed the previous message on the stack (if any) is displayed
	 *
	 * @param StatusBarName	The name of the status bar to remove from
	 * @param InHandle		Handle to the status bar message to remove
	 */
	void PopStatusBarMessage(FName StatusBarName, FStatusBarMessageHandle InHandle);

	/**
	 * Removes all messages from the status bar
	 *
	 * @param StatusBarName	The name of the status bar to remove from
	 */
	void ClearStatusBarMessages(FName StatusBarName);

private:
	/** IProgressNotificationHandler interface */
	virtual void StartProgressNotification(FProgressNotificationHandle Handle, FText DisplayText, int32 TotalWorkToDo) override;
	virtual void UpdateProgressNotification(FProgressNotificationHandle Handle, int32 TotalWorkDone, int32 UpdatedTotalWorkToDo, FText UpdatedDisplayText) override;
	virtual void CancelProgressNotification(FProgressNotificationHandle Handle) override;

	bool ToggleContentBrowser(TSharedRef<SWindow> ParentWindow);
	void OnDebugConsoleClosed();
	void CreateContentBrowserIfNeeded();
	void CreateAndShowNewUserTipIfNeeded(TSharedPtr<SWindow> ParentWindow, bool bIsNewProjectDialog);

	TSharedPtr<SStatusBar> GetStatusBar(FName StatusBarName) const;
	TSharedRef<SWidget> OnGetContentBrowser();
	void OnContentBrowserOpened(TSharedRef<SStatusBar>& StatusBarWithContentBrowser);
	void OnContentBrowserDismissed(const TSharedPtr<SWidget>& NewlyFocusedWidget);
	void HandleDeferredOpenContentBrowser(TSharedPtr<SWindow> ParentWindow);
private:
	TMap<FName, TWeakPtr<SStatusBar>> StatusBars;
	TWeakPtr<SWidget> PreviousKeyboardFocusedWidget;
	/** The floating content browser that is opened via the content browser button in the status bar */
	TSharedPtr<SWidget> StatusBarContentBrowser;
	static int32 MessageHandleCounter;
};
