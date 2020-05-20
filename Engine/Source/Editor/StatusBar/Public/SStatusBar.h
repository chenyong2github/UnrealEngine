// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Animation/CurveSequence.h"

class SStatusBar;

DECLARE_DELEGATE_OneParam(FOnContentBrowserOpened, TSharedRef<SStatusBar>&)
DECLARE_DELEGATE_OneParam(FOnContentBrowserDismissed, const TSharedPtr<SWidget>&)

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
	virtual bool SupportsKeyboardFocus() const { return false; }

	void Construct(const FArguments& InArgs, FName InStatusBarName, const TSharedRef<SDockTab> InParentTab);

	void PushMessage(FStatusBarMessageHandle& InHandle, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText);
	void PopMessage(FStatusBarMessageHandle& InHandle);
	void ClearAllMessages();

	EVisibility GetHelpIconVisibility() const;

	TSharedPtr<SDockTab> GetParentTab() const;
	void FocusDebugConsole();
	bool IsDebugConsoleFocused() const;


	void OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

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
	const FSlateBrush* GetContentBrowserExpandArrowImage() const;

	FText GetStatusBarMessage() const;

	TSharedRef<SWidget> MakeContentBrowserWidget();

	TSharedRef<SWidget> MakeStatusBarToolBarWidget();
	TSharedRef<SWidget> MakeDebugConsoleWidget(FSimpleDelegate OnConsoleClosed);
	TSharedRef<SWidget> MakeStatusMessageWidget();

	FReply OnContentBrowserButtonClicked();

	EActiveTimerReturnType UpdateContentBrowserAnimation(double CurrentTime, float DeltaTime);

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
	TSharedPtr<FActiveTimerHandle> ContentBrowserOpenCloseTimer;
	FOnGetContent GetContentBrowserDelegate;
	FOnContentBrowserOpened OnContentBrowserOpenedDelegate;
	FOnContentBrowserDismissed OnContentBrowserDismissedDelegate;
	FCurveSequence ContentBrowserEasingCurve;
	float MaxContentBrowserHeight;
	float MinContentBrowserHeight;
	const FSlateBrush* UpArrow;
	const FSlateBrush* DownArrow;
	FName StatusBarName;
};

