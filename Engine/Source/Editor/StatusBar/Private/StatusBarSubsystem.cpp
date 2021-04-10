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
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/Notifications/SNotificationBackground.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "StatusBar"

int32 UStatusBarSubsystem::MessageHandleCounter = 0;

class SNewUserTipNotification : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNewUserTipNotification)
	{}
	SLATE_END_ARGS()

	SNewUserTipNotification()
		: NewBadgeBrush(FStyleColors::Success)
		, KeybindBackgroundBrush(FLinearColor::Transparent, 6.0f, FStyleColors::ForegroundHover, 1.5f)
	{}

	static void Show(TSharedPtr<SWindow> InParentWindow)
	{
		if(!ActiveNotification.IsValid())
		{
			TSharedRef<SNewUserTipNotification> ActiveNotificationRef = 
				SNew(SNewUserTipNotification);

			ActiveNotification = ActiveNotificationRef;
			ParentWindow = InParentWindow;
			InParentWindow->AddOverlaySlot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Left)
				.Padding(FMargin(20.0f, 20.0f, 10.0f, 50.f))
				[
					ActiveNotificationRef
				];
		}

	}

	static void Dismiss()
	{
		TSharedPtr<SNewUserTipNotification> ActiveNotificationPin = ActiveNotification.Pin();
		if (ParentWindow.IsValid() && ActiveNotificationPin.IsValid())
		{
			ParentWindow.Pin()->RemoveOverlaySlot(ActiveNotificationPin.ToSharedRef());
		}

		ParentWindow.Reset();

		ActiveNotification.Reset();
	}

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(350.0f)
			.HeightOverride(128.0f)
			[
				SNew(SNotificationBackground)
				.Padding(FMargin(16, 8))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Top)
					.AutoWidth()
					[
						SNew(SBorder)
						.Padding(FMargin(11,4))
						.BorderImage(&NewBadgeBrush)
						.ForegroundColor(FStyleColors::ForegroundInverted)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NewBadge", "New"))
							.TextStyle(FAppStyle::Get(), "SmallButtonText")
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(16.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Font(FAppStyle::Get().GetFontStyle("NotificationList.FontBold"))
							.Text(LOCTEXT("ContentDrawerTipTitle", "Content Drawer"))
							.ColorAndOpacity(FStyleColors::ForegroundHover)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 12.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBorder)
								.Padding(FMargin(20, 4))
								.BorderImage(&KeybindBackgroundBrush)
								[
									SNew(STextBlock)
									.TextStyle(FAppStyle::Get(), "DialogButtonText")
									.Text(FText::FromString("CTRL"))
									.ColorAndOpacity(FStyleColors::ForegroundHover)
								]
							]
							+ SHorizontalBox::Slot()
							.Padding(8.0f, 0.0f)
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
								.ColorAndOpacity(FStyleColors::ForegroundHover)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBorder)
								.Padding(FMargin(20, 4))
								.BorderImage(&KeybindBackgroundBrush)
								[
									SNew(STextBlock)
									.TextStyle(FAppStyle::Get(), "DialogButtonText")
									.Text(FText::FromString("SPACE"))
									.ColorAndOpacity(FStyleColors::ForegroundHover)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 12.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ContentDrawerTipDesc", "Summon the content browser in\ncollapsable drawer."))
							.ColorAndOpacity(FStyleColors::Foreground)
						]
					]
					+ SHorizontalBox::Slot()
					.Padding(0.0f, 0.0f, 0.0f, 0.0f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked_Lambda([]() { SNewUserTipNotification::Dismiss(); return FReply::Handled(); })
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.X"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			]
		];
	}

private:
	FSlateRoundedBoxBrush NewBadgeBrush;
	FSlateRoundedBoxBrush KeybindBackgroundBrush;
	static TWeakPtr<SNewUserTipNotification> ActiveNotification;
	static TWeakPtr<SWindow> ParentWindow;
};

TWeakPtr<SNewUserTipNotification> SNewUserTipNotification::ActiveNotification;
TWeakPtr<SWindow> SNewUserTipNotification::ParentWindow;


void UStatusBarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FSourceControlCommands::Register();

	IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	if (MainFrameModule.IsWindowInitialized())
	{
		CreateAndShowNewUserTipIfNeeded(MainFrameModule.GetParentWindow(), false);
	}
	else
	{
		MainFrameModule.OnMainFrameCreationFinished().AddUObject(this, &UStatusBarSubsystem::CreateAndShowNewUserTipIfNeeded);
	}


	FSlateNotificationManager::Get().SetProgressNotificationHandler(this);
}

void UStatusBarSubsystem::Deinitialize()
{
	FSourceControlCommands::Unregister();

	FSlateNotificationManager::Get().SetProgressNotificationHandler(nullptr);
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

bool UStatusBarSubsystem::ForceDismissDrawer()
{
	bool bWasDismissed = false;
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if (StatusBarPinned->IsDrawerOpened(StatusBarDrawerIds::ContentBrowser))
			{
				StatusBarPinned->DismissDrawer(nullptr);
				bWasDismissed = true;
			}
		}
	}

	return bWasDismissed;
}

bool UStatusBarSubsystem::ToggleContentBrowser(TSharedRef<SWindow> ParentWindow)
{
	bool bWasDismissed = false;

	SNewUserTipNotification::Dismiss();

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
	ContentBrowserDrawer.ButtonText = LOCTEXT("StatusBar_ContentBrowserButton", "Content Drawer");
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
		FStatusBarMessageHandle NewHandle(++MessageHandleCounter);

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

void UStatusBarSubsystem::StartProgressNotification(FProgressNotificationHandle Handle, FText DisplayText, int32 TotalWorkToDo)
{
	// Get the active window, if one is not active a notification was started when the application was deactivated so use the focus path to find a window or just use the root window if there is no keyboard focus
	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
	if (!ActiveWindow)
	{
		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		ActiveWindow = FocusedWidget ? FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef()) : FGlobalTabmanager::Get()->GetRootWindow();
	}

	// Find the active status bar to display the progress in
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			TSharedPtr<SDockTab> ParentTab = StatusBarPinned->GetParentTab();
			if (ParentTab && ParentTab->IsForeground() && ParentTab->GetParentWindow() == ActiveWindow)
			{
				StatusBarPinned->StartProgressNotification(Handle, DisplayText, TotalWorkToDo);
				break;
			}
		}
	}
}

void UStatusBarSubsystem::UpdateProgressNotification(FProgressNotificationHandle Handle, int32 TotalWorkDone, int32 UpdatedTotalWorkToDo, FText UpdatedDisplayText)
{
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if (StatusBarPinned->UpdateProgressNotification(Handle, TotalWorkDone, UpdatedTotalWorkToDo, UpdatedDisplayText))
			{
				break;
			}
		}
	}

}

void UStatusBarSubsystem::CancelProgressNotification(FProgressNotificationHandle Handle)
{
	for (auto StatusBar : StatusBars)
	{
		if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
		{
			if (StatusBarPinned->CancelProgressNotification(Handle))
			{
				break;
			}
		}
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
		Config.bCanSetAsPrimaryBrowser = true;

		TFunction<TSharedPtr<SDockTab>()> GetTab(
			[this]() -> TSharedPtr<SDockTab>
			{
				for (auto StatusBar : StatusBars)
				{
					if (TSharedPtr<SStatusBar> StatusBarPinned = StatusBar.Value.Pin())
					{
						if (StatusBarPinned->IsDrawerOpened(StatusBarDrawerIds::ContentBrowser))
						{
							return StatusBarPinned->GetParentTab();
						}
					}
				}

				checkf(false, TEXT("If we get here somehow a content browser drawer is opened but no status bar claims it"));
				return TSharedPtr<SDockTab>();
			}
		);
		StatusBarContentBrowser = ContentBrowserSingleton.CreateContentBrowserDrawer(Config, GetTab);
	}
}

void UStatusBarSubsystem::CreateAndShowNewUserTipIfNeeded(TSharedPtr<SWindow> ParentWindow, bool bIsNewProjectDialog)
{
	if(!bIsNewProjectDialog)
	{
		const FString StoreId = TEXT("Epic Games");
		const FString SectionName = TEXT("Unreal Engine/Editor");
		const FString KeyName = TEXT("LaunchTipShown");

		const FString FallbackIniLocation = TEXT("/Script/UnrealEd.EditorSettings");
		const FString FallbackIniKey = TEXT("LaunchTipShownFallback");

		// Its important that this new user message does not appear after the first launch so we store it in a more permanent place
		FString CurrentState = TEXT("0");
		if (!FPlatformMisc::GetStoredValue(StoreId, SectionName, KeyName, CurrentState))
		{
			// As a fallback where the registry was not readable or writable, save a flag in the editor ini. This will be less permanent as the registry but will prevent 
			// the notification from appearing on every launch
			GConfig->GetString(*FallbackIniLocation, *FallbackIniKey, CurrentState, GEditorSettingsIni);


		}

		if(CurrentState != TEXT("1"))
		{
			SNewUserTipNotification::Show(ParentWindow);

			// Write that we've shown the notification
			if (!FPlatformMisc::SetStoredValue(StoreId, SectionName, KeyName, TEXT("1")))
			{
				// Use fallback
				GConfig->SetString(*FallbackIniLocation, *FallbackIniKey, TEXT("1"), GEditorSettingsIni);
			}
		}
	}

	// Ignore the if the main frame gets recreated this session
	IMainFrameModule::Get().OnMainFrameCreationFinished().RemoveAll(this);
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
	SNewUserTipNotification::Dismiss();

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
