// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Animation/CurveSequence.h"
#include "Application/ThrottleManager.h"

class SStatusBar;

DECLARE_DELEGATE_OneParam(FOnContentBrowserOpened, TSharedRef<SStatusBar>&)
DECLARE_DELEGATE_OneParam(FOnContentBrowserDismissed, const TSharedPtr<SWidget>&)
DECLARE_DELEGATE_OneParam(FOnContentBrowserTargetHeightChanged, float);


class SWindow;
class SDockTab;
class SWidget;
class SMultiLineEditableTextBox;
class SContentBrowserOverlay;

class SStatusBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SStatusBar)	
	{}
		SLATE_EVENT(FSimpleDelegate, OnConsoleClosed)

		SLATE_EVENT(FOnGetContent, OnGetContentBrowser)

		SLATE_EVENT(FOnContentBrowserOpened, OnContentBrowserOpened)

		SLATE_EVENT(FOnContentBrowserDismissed, OnContentBrowserDismissed)

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

	bool IsContentBrowserOpened() const;

	void OpenContentBrowser();

	/**
	 * Dismisses an open content browser with an animation.  The content browser is removed once the animation is complete
	 */
	void DismissContentBrowser(const TSharedPtr<SWidget>& NewlyFocusedWidget);

	/**
	 * Removes a content browser from the layout immediately
	 */
	void RemoveContentBrowser();
private:
	/** Called when global focus changes which is used to determine if we should close an opened content browser drawer */
	void OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	/** Called when active tab changes which is used to determine if we should close an opened content browser drawer */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	EVisibility GetHelpIconVisibility() const;

	const FSlateBrush* GetContentBrowserExpandArrowImage() const;

	FText GetStatusBarMessage() const;

	TSharedRef<SWidget> MakeContentBrowserWidget();

	TSharedRef<SWidget> MakeStatusBarToolBarWidget();
	TSharedRef<SWidget> MakeDebugConsoleWidget(FSimpleDelegate OnConsoleClosed);
	TSharedRef<SWidget> MakeStatusMessageWidget();

	FReply OnContentBrowserButtonClicked();

	void CreateAnimationTimerIfNeeded();

	EActiveTimerReturnType UpdateContentBrowserAnimation(double CurrentTime, float DeltaTime);
	void OnContentBrowserTargetHeightChanged(float TargetHeight);

	void RegisterStatusBarMenu();

	void RegisterSourceControlStatus();

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
private:
	TArray<FStatusBarMessage> MessageStack;
	TSharedPtr<SMultiLineEditableTextBox> ConsoleEditBox;
	TWeakPtr<SDockTab> ParentTab;
	TSharedPtr<SContentBrowserOverlay> ContentBrowserOverlayContent;
	TWeakPtr<SWindow> WindowWithOverlayContent;
	TSharedPtr<FActiveTimerHandle> ContentBrowserOpenCloseTimer;
	FOnGetContent GetContentBrowserDelegate;
	FOnContentBrowserOpened OnContentBrowserOpenedDelegate;
	FOnContentBrowserDismissed OnContentBrowserDismissedDelegate;
	FCurveSequence ContentBrowserEasingCurve;
	FThrottleRequest AnimationThrottle;
	float TargetContentBrowserHeight;
	const FSlateBrush* UpArrow;
	const FSlateBrush* DownArrow;
	FName StatusBarName;
	FName StatusBarToolBarName;
};

