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
#include "Toolkits/GlobalEditorCommonCommands.h"


#define LOCTEXT_NAMESPACE "StatusBar"


int32 UStatusBarSubsystem::HandleCounter = 0;

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

bool UStatusBarSubsystem::OpenContentBrowserDrawer()
{
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ParentWindow.IsValid())
	{
		if (TSharedPtr<SDockTab> ActiveTab = FGlobalTabmanager::Get()->GetActiveTab())
		{
			if (TSharedPtr<SDockTab> ActiveMajorTab = FGlobalTabmanager::Get()->GetMajorTabForTabManager(ActiveTab->GetTabManager()))
			{
				ParentWindow = ActiveMajorTab->GetParentWindow();
			}
		}
	}

	if (ParentWindow.IsValid() && ParentWindow->GetType() == EWindowType::Normal)
	{
		TSharedRef<SWindow> WindowRef = ParentWindow.ToSharedRef();
		return ToggleContentBrowser(ParentWindow.ToSharedRef());
	}

	return false;
}

bool UStatusBarSubsystem::ToggleContentBrowser(TSharedRef<SWindow> ParentWindow)
{
	bool bWasDismissed = false;

	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if(StatusBarPinned->IsDrawerOpened(StatusBarDrawerIds::ContentBrowser))
			{
				TSharedPtr<SDockTab> ParentTab = StatusBarPinned->GetParentTab();
				if (ParentTab && ParentTab->IsForeground() && ParentTab->GetParentWindow() == ParentWindow)
				{
					StatusBarPinned->DismissDrawer(nullptr);
					bWasDismissed = true;
				}
			}
		}
	}

	if(!bWasDismissed)
	{
		TSharedPtr<SWindow> Window = ParentWindow;
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UStatusBarSubsystem::HandleDeferredOpenContentBrowser, Window));
	}

	return true;
}

TSharedRef<SWidget> UStatusBarSubsystem::MakeStatusBarWidget(FName StatusBarName, const TSharedRef<SDockTab>& InParentTab)
{
	CreateContentBrowserIfNeeded();

	TSharedRef<SStatusBar> StatusBar =
		SNew(SStatusBar, StatusBarName, InParentTab)
		.OnConsoleClosed_UObject(this, &UStatusBarSubsystem::OnDebugConsoleClosed);

	FStatusBarDrawer ContentBrowserDrawer(StatusBarDrawerIds::ContentBrowser);
	ContentBrowserDrawer.GetDrawerContentDelegate.BindUObject(this, &UStatusBarSubsystem::OnGetContentBrowser);
	ContentBrowserDrawer.OnDrawerOpenedDelegate.BindUObject(this, &UStatusBarSubsystem::OnContentBrowserOpened);
	ContentBrowserDrawer.OnDrawerDismissedDelegate.BindUObject(this, &UStatusBarSubsystem::OnContentBrowserDismissed);
	ContentBrowserDrawer.ButtonText = LOCTEXT("StatusBar_ContentBrowserButton", "Content Browser");
	ContentBrowserDrawer.ToolTipText = FText::Format(LOCTEXT("StatusBar_ContentBrowserDrawerToolTip", "Opens a temporary content browser above this status which will dismiss when it loses focus ({0})"), FGlobalEditorCommonCommands::Get().OpenContentBrowserDrawer->GetInputText());
	ContentBrowserDrawer.Icon = FAppStyle::Get().GetBrush("ContentBrowser.TabIcon");

	StatusBar->RegisterDrawer(MoveTemp(ContentBrowserDrawer));

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

		StatusBarContentBrowser = ContentBrowserSingleton.CreateContentBrowserDrawer(Config);
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
			if (StatusBarWithContentBrowser != StatusBarPinned && StatusBarPinned->IsDrawerOpened(StatusBarDrawerIds::ContentBrowser))
			{
				StatusBarPinned->CloseDrawerImmediately();
			}
		}
	}

	IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();;

	// Cache off the previously focused widget so we can restore focus if the user hits the focus key again
	PreviousKeyboardFocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();

	ContentBrowserSingleton.FocusContentBrowserSearchField(StatusBarContentBrowser);
}

void UStatusBarSubsystem::OnContentBrowserDismissed(const TSharedPtr<SWidget>& NewlyFocusedWidget)
{
	if (PreviousKeyboardFocusedWidget.IsValid() && !NewlyFocusedWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(PreviousKeyboardFocusedWidget.Pin());
	}

	IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();;
	ContentBrowserSingleton.SaveContentBrowserSettings(StatusBarContentBrowser);

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
				StatusBarPinned->OpenDrawer(StatusBarDrawerIds::ContentBrowser);
				IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::Get().GetModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
