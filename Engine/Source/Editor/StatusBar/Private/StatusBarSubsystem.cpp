// Copyright Epic Games, Inc. All Rights Reserved.


#include "StatusBarSubsystem.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "SourceControlMenuHelpers.h"
#include "SStatusBar.h"
#include "Widgets/Docking/SDockTab.h"
#include "Editor.h"


#define LOCTEXT_NAMESPACE "StatusBar"


int32 UStatusBarSubsystem::HandleCounter = 0;

static const FName StatusBarContentBrowserName = "StatusBarContentBrowser";

void UStatusBarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FSourceControlCommands::Register();
}

void UStatusBarSubsystem::Deinitialize()
{
	FSourceControlCommands::Unregister();
}

bool UStatusBarSubsystem::FocusDebugConsole(TSharedRef<SWindow> ParentWindow)
{
	bool bFocusedSuccessfully = false;

	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			TSharedPtr<SDockTab> ParentTab = StatusBarPinned->GetParentTab();
			if (ParentTab && ParentTab->IsForeground() && ParentTab->GetParentWindow() == ParentWindow)
			{
				// Cache off the previously focused widget so we can restore focus if the user hits the focus key again
				PreviousKeyboardFocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
				bFocusedSuccessfully = StatusBarPinned->FocusDebugConsole();
				break;
			}
		}
	}

	return bFocusedSuccessfully;
}

bool UStatusBarSubsystem::ToggleContentBrowser(TSharedRef<SWindow> ParentWindow)
{
	bool bWasDismissed = false;

	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if(StatusBarPinned->IsContentBrowserOpened())
			{
				TSharedPtr<SDockTab> ParentTab = StatusBarPinned->GetParentTab();
				if (ParentTab && ParentTab->IsForeground() && ParentTab->GetParentWindow() == ParentWindow)
				{
					StatusBarPinned->DismissContentBrowser(nullptr);
					bWasDismissed = true;
				}
			}
		}
	}

	if(!bWasDismissed)
	{
		CreateContentBrowserIfNeeded();

		TSharedPtr<SWindow> Window = ParentWindow;
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UStatusBarSubsystem::HandleDeferredOpenContentBrowser, Window));
	}

	return true;
}

TSharedRef<SWidget> UStatusBarSubsystem::MakeStatusBarWidget(FName StatusBarName, const TSharedRef<SDockTab>& InParentTab)
{
	TSharedRef<SStatusBar> StatusBar =
		SNew(SStatusBar, StatusBarName, InParentTab)
		.OnConsoleClosed_UObject(this, &UStatusBarSubsystem::OnDebugConsoleClosed)
		.OnGetContentBrowser_UObject(this, &UStatusBarSubsystem::OnGetContentBrowser)
		.OnContentBrowserOpened_UObject(this, &UStatusBarSubsystem::OnContentBrowserOpened)
		.OnContentBrowserDismissed_UObject(this, &UStatusBarSubsystem::OnContentBrowserDismissed);

	// Clean up stale status bars
	for (auto It = StatusBars.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	StatusBars.Add(StatusBarName, StatusBar);

	return StatusBar;
}

FStatusBarMessageHandle UStatusBarSubsystem::PushStatusBarMessage(FName StatusBarName, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText)
{
	if (TSharedPtr<SStatusBar> StatusBar = GetStatusBar(StatusBarName))
	{
		FStatusBarMessageHandle NewHandle(++HandleCounter);

		StatusBar->PushMessage(NewHandle, InMessage, InHintText);

		return NewHandle;
	}

	return FStatusBarMessageHandle();
}

FStatusBarMessageHandle UStatusBarSubsystem::PushStatusBarMessage(FName StatusBarName, const TAttribute<FText>& InMessage)
{
	return PushStatusBarMessage(StatusBarName, InMessage, TAttribute<FText>());
}

void UStatusBarSubsystem::PopStatusBarMessage(FName StatusBarName, FStatusBarMessageHandle InHandle)
{
	if (TSharedPtr<SStatusBar> StatusBar = GetStatusBar(StatusBarName))
	{
		StatusBar->PopMessage(InHandle);
	}
}

void UStatusBarSubsystem::ClearStatusBarMessages(FName StatusBarName)
{
	if (TSharedPtr<SStatusBar> StatusBar = GetStatusBar(StatusBarName))
	{
		StatusBar->ClearAllMessages();
	}
}

void UStatusBarSubsystem::OnDebugConsoleClosed()
{
	if (PreviousKeyboardFocusedWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(PreviousKeyboardFocusedWidget.Pin());
		PreviousKeyboardFocusedWidget.Reset();
	}
}

void UStatusBarSubsystem::CreateContentBrowserIfNeeded()
{
	if(!StatusBarContentBrowser.IsValid())
	{
		IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();;

		FContentBrowserConfig Config;
		Config.bCanSetAsPrimaryBrowser = false;

		StatusBarContentBrowser = ContentBrowserSingleton.CreateContentBrowser(StatusBarContentBrowserName, nullptr, &Config);
	}
}

TSharedPtr<SStatusBar> UStatusBarSubsystem::GetStatusBar(FName StatusBarName) const
{
	return StatusBars.FindRef(StatusBarName).Pin();
}

TSharedRef<SWidget> UStatusBarSubsystem::OnGetContentBrowser()
{
	CreateContentBrowserIfNeeded();

	return StatusBarContentBrowser.ToSharedRef();
}

void UStatusBarSubsystem::OnContentBrowserOpened(TSharedRef<SStatusBar>& StatusBarWithContentBrowser)
{
	// Dismiss any other content browser that is opened when one status bar opens it.  The content browser is a shared resource and shouldn't be in the layout twice
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if (StatusBarWithContentBrowser != StatusBarPinned && StatusBarPinned->IsContentBrowserOpened())
			{
				StatusBarPinned->RemoveContentBrowser();
			}
		}
	}
}

void UStatusBarSubsystem::OnContentBrowserDismissed(const TSharedPtr<SWidget>& NewlyFocusedWidget)
{
	if (PreviousKeyboardFocusedWidget.IsValid() && !NewlyFocusedWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(PreviousKeyboardFocusedWidget.Pin());
	}

	IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();;
	ContentBrowserSingleton.SaveContentBrowserSettings(StatusBarContentBrowserName);

	PreviousKeyboardFocusedWidget.Reset();
}

void UStatusBarSubsystem::HandleDeferredOpenContentBrowser(TSharedPtr<SWindow> ParentWindow)
{
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			TSharedPtr<SDockTab> ParentTab = StatusBarPinned->GetParentTab();
			if (ParentTab && ParentTab->IsForeground() && ParentTab->GetParentWindow() == ParentWindow)
			{
				StatusBarPinned->OpenContentBrowser();
				IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().GetModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

				// Cache off the previously focused widget so we can restore focus if the user hits the focus key again
				PreviousKeyboardFocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();

				ContentBrowserSingleton.FocusContentBrowserSearchField(StatusBarContentBrowserName);
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
