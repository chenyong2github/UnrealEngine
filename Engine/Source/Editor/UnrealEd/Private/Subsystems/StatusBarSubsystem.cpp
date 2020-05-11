// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/StatusBarSubsystem.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlWindows.h"
#include "Framework/Commands/Commands.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "FileHelpers.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "OutputLogModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "StatusBar"

class FSourceControlCommands : public TCommands<FSourceControlCommands>
{
public:
	FSourceControlCommands() : TCommands<FSourceControlCommands>
	(
		"SourceControl",
		NSLOCTEXT("Contexts", "SourceControl", "Source Control"),
		"LevelEditor",
		FEditorStyle::GetStyleSetName()
	)
	{}

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ConnectToSourceControl, "Connect to Source Control...", "Opens a dialog to connect to source control.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ChangeSourceControlSettings, "Change Source Control Settings...", "Opens a dialog to change source control settings.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(CheckOutModifiedFiles, "Check Out Modified Files...", "Opens a dialog to check out any assets which have been modified.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SubmitToSourceControl, "Submit to Source Control...", "Opens a dialog with check in options for content and levels.", EUserInterfaceActionType::Button, FInputChord());

		ActionList->MapAction(
			ConnectToSourceControl,
			FExecuteAction::CreateStatic(&FSourceControlCommands::ConnectToSourceControl_Clicked)
		);

		ActionList->MapAction(
			ChangeSourceControlSettings,
			FExecuteAction::CreateStatic(&FSourceControlCommands::ConnectToSourceControl_Clicked)
		);

		ActionList->MapAction(
			CheckOutModifiedFiles,
			FExecuteAction::CreateStatic(&FSourceControlCommands::CheckOutModifiedFiles_Clicked),
			FCanExecuteAction::CreateStatic(&FSourceControlCommands::CheckOutModifiedFiles_CanExecute)
		);

		ActionList->MapAction(
			SubmitToSourceControl,
			FExecuteAction::CreateStatic(&FSourceControlCommands::SubmitToSourceControl_Clicked),
			FCanExecuteAction::CreateStatic(&FSourceControlCommands::SubmitToSourceControl_CanExecute)
		);

	}

private:

	static void ConnectToSourceControl_Clicked()
	{
		// Show login window regardless of current status - its useful as a shortcut to change settings.
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		SourceControlModule.ShowLoginDialog(FSourceControlLoginClosed(), ELoginWindowMode::Modeless, EOnLoginWindowStartup::PreserveProvider);
	}

	static bool CheckOutModifiedFiles_CanExecute()
	{
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if (ISourceControlModule::Get().IsEnabled() &&
			ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			TArray<UPackage*> PackagesToSave;
			FEditorFileUtils::GetDirtyWorldPackages(PackagesToSave);
			FEditorFileUtils::GetDirtyContentPackages(PackagesToSave);

			return PackagesToSave.Num() > 0;
		}

		return false;
	}

	static void CheckOutModifiedFiles_Clicked()
	{
		TArray<UPackage*> PackagesToSave;
		FEditorFileUtils::GetDirtyWorldPackages(PackagesToSave);
		FEditorFileUtils::GetDirtyContentPackages(PackagesToSave);

		const bool bCheckDirty = true;
		const bool bPromptUserToSave = false;
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptUserToSave);
	}

	static bool SubmitToSourceControl_CanExecute()
	{
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		return ISourceControlModule::Get().IsEnabled() &&
			ISourceControlModule::Get().GetProvider().IsAvailable() &&
			FSourceControlWindows::CanChoosePackagesToCheckIn();
	}

	static void SubmitToSourceControl_Clicked()
	{
		FSourceControlWindows::ChoosePackagesToCheckIn();
	}

	
public:
	/**
	 * Source Control Commands
	 */
	TSharedPtr< FUICommandInfo > ConnectToSourceControl;
	TSharedPtr< FUICommandInfo > ChangeSourceControlSettings;
	TSharedPtr< FUICommandInfo > CheckOutModifiedFiles;
	TSharedPtr< FUICommandInfo > SubmitToSourceControl;

	static TSharedRef<FUICommandList> ActionList;
};

TSharedRef<FUICommandList> FSourceControlCommands::ActionList(new FUICommandList());

struct FSourceControlMenuHelpers
{
	enum EQueryState
	{
		NotQueried,
		Querying,
		Queried,
	};


	static EQueryState QueryState;

	static void CheckSourceControlStatus()
	{
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if (SourceControlModule.IsEnabled())
		{
			SourceControlModule.GetProvider().Execute(ISourceControlOperation::Create<FConnect>(),
				EConcurrency::Asynchronous,
				FSourceControlOperationComplete::CreateStatic(&FSourceControlMenuHelpers::OnSourceControlOperationComplete));
			QueryState = EQueryState::Querying;
		}
	}

	static void OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
	{
		QueryState = EQueryState::Queried;
	}

	static TSharedRef<SWidget> GenerateSourceControlMenuContent()
	{
		UToolMenu* SourceControlMenu = UToolMenus::Get()->RegisterMenu("StatusBar.ToolBar.SourceControl", NAME_None, EMultiBoxType::Menu, false);

		FToolMenuSection& Section = SourceControlMenu->AddSection("SourceControlActions", LOCTEXT("SourceControlMenuHeadingActions", "Actions"));

		Section.AddDynamicEntry("ConnectToSourceControl", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
				if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
				{
					InSection.AddMenuEntry(
						FSourceControlCommands::Get().ChangeSourceControlSettings,
						TAttribute<FText>(),
						TAttribute<FText>(),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.ChangeSettings")
					);
				}
				else
				{
					InSection.AddMenuEntry(
						FSourceControlCommands::Get().ConnectToSourceControl,
						TAttribute<FText>(),
						TAttribute<FText>(),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Connect")
					);
				}
			}));

		Section.AddSeparator("SourceControlConnectionSeparator");

		Section.AddMenuEntry(
			FSourceControlCommands::Get().CheckOutModifiedFiles,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.CheckOut")
		);


		Section.AddMenuEntry(
			FSourceControlCommands::Get().SubmitToSourceControl,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Submit")
		);

		return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar.SourceControl", FToolMenuContext(FSourceControlCommands::ActionList));
	}

	static FText GetSourceControlStatusText()
	{
		if (QueryState == EQueryState::Querying)
		{
			return LOCTEXT("SourceControlStatus_Querying", "Contacting Server....");
		}
		else
		{
			ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
			if (SourceControlModule.IsEnabled())
			{
				if (!SourceControlModule.GetProvider().IsAvailable())
				{
					return LOCTEXT("SourceControlStatus_Error", "Server Unavailable");
				}
				else
				{
					return LOCTEXT("SourceControlStatus_Available", "Source Control");
				}
			}
			else
			{
				return LOCTEXT("SourceControlStatus_Error", "Source Control Off");
			}
		}
	}
	static FText GetSourceControlTooltip()
	{
		if (QueryState == EQueryState::Querying)
		{
			return LOCTEXT("SourceControlUnknown", "Source control status is unknown");
		}
		else
		{
			return ISourceControlModule::Get().GetProvider().GetStatusText();
		}
	}

	static const FSlateBrush* GetSourceControlIcon()
	{

		if (QueryState == EQueryState::Querying)
		{
			static const FSlateBrush* QueryBrush = FAppStyle::Get().GetBrush("SourceControl.StatusIcon.Unknown");
			return QueryBrush;
		}
		else
		{
			ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
			if (SourceControlModule.IsEnabled())
			{
				if (!SourceControlModule.GetProvider().IsAvailable())
				{
					static const FSlateBrush* ErrorBrush = FAppStyle::Get().GetBrush("SourceControl.StatusIcon.Error");
					return ErrorBrush;
				}
				else
				{
					static const FSlateBrush* OnBrush = FAppStyle::Get().GetBrush("SourceControl.StatusIcon.On");
					return OnBrush;
				}
			}
			else
			{
				static const FSlateBrush* OffBrush = FAppStyle::Get().GetBrush("SourceControl.StatusIcon.Off");
				return OffBrush;
			}
		}
	}

	static TSharedRef<SWidget> MakeSourceControlStatusWidget()
	{
		return
			SNew(SComboButton)
			.ContentPadding(FMargin(6.0f,0.0f))
			.ToolTipText_Static(&FSourceControlMenuHelpers::GetSourceControlTooltip)
			.MenuPlacement(MenuPlacement_AboveAnchor)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("StatusBar.StatusBarComboButton"))
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image_Static(&FSourceControlMenuHelpers::GetSourceControlIcon)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(5, 5, 0, 5))
				[
					SNew(STextBlock)
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
					.Text_Static(&FSourceControlMenuHelpers::GetSourceControlStatusText)
				]
			]
			.MenuContent()
			[
				GenerateSourceControlMenuContent()
			];
	}
};

FSourceControlMenuHelpers::EQueryState FSourceControlMenuHelpers::QueryState = FSourceControlMenuHelpers::EQueryState::NotQueried;


class SStatusBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SStatusBar)	
	{}
		SLATE_EVENT(FSimpleDelegate, OnConsoleClosed)

		SLATE_EVENT(FOnGetContent, OnGetContentBrowser)

	SLATE_END_ARGS()

public:
	virtual bool SupportsKeyboardFocus() const { return false; }

	void Construct(const FArguments& InArgs, FName InStatusBarName, const TSharedRef<SDockTab> InParentTab)
	{
		StatusBarName = InStatusBarName;
		ParentTab = InParentTab;

		UpArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserUp");
		DownArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserDown");

		const FSlateBrush* StatusBarBackground = FAppStyle::Get().GetBrush("StatusBar.Background");

		GetContentBrowserDelegate = InArgs._OnGetContentBrowser;

		FSlateApplication::Get().OnFocusChanging().AddSP(this, &SStatusBar::OnGlobalFocusChanging);

		ChildSlot
		[
			SNew(SBox)
			.HeightOverride(FAppStyle::Get().GetFloat("StatusBar.Height"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.0f, 0.0f)
				.AutoWidth()
				[
					SNew(SBorder)
					.Padding(FMargin(2.0f, 0.0f))
					.BorderImage(StatusBarBackground)
					.VAlign(VAlign_Center)
					[			
						MakeContentBrowserWidget()
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
				]
				+ SHorizontalBox::Slot()
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
				+ SHorizontalBox::Slot()
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
		];
	}

	void PushMessage(FStatusBarMessageHandle InHandle, const TAttribute<FText>& InMessage, const TAttribute<FText>& InHintText)
	{
		MessageStack.Emplace(InMessage, InHintText, InHandle);
	}

	void PopMessage(FStatusBarMessageHandle& InHandle)
	{
		if (InHandle.IsValid() && MessageStack.Num() > 0)
		{
			MessageStack.RemoveAll([InHandle](const FStatusBarMessage& Message)
				{
					return Message.Handle == InHandle;
				});
		}
	}

	void ClearAllMessages()
	{
		MessageStack.Empty();
	}

	EVisibility GetHelpIconVisibility() const
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

	TSharedPtr<SDockTab> GetParentTab() const { return ParentTab.Pin(); }

	void FocusDebugConsole()
	{
		FSlateApplication::Get().SetKeyboardFocus(ConsoleEditBox, EFocusCause::SetDirectly);
	}

	bool IsDebugConsoleFocused() const
	{
		return ConsoleEditBox->HasKeyboardFocus();
	}

	void OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
	{
		//if (ContentBrowserOverlayContent.IsValid() && !NewFocusedWidgetPath.ContainsWidget(ContentBrowserOverlayContent.ToSharedRef()))
		//{
		//	DismissContentBrowser();
		//}
	}

private:
	const FSlateBrush* GetContentBrowserExpandArrowImage() const
	{
		return DownArrow;
	}

	FText GetStatusBarMessage() const
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

	TSharedRef<SWidget> MakeContentBrowserWidget()
	{
		return
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("StatusBar.StatusBarButton"))
			.OnClicked(this, &SStatusBar::OnContentBrowserButtonClicked)
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
					.Image(this, &SStatusBar::GetContentBrowserExpandArrowImage)
				]
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("StatusBar.ContentBrowserIcon"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				[
					SNew(STextBlock)
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
					.Text(LOCTEXT("StatusBar_ContentBrowserButton", "Content Browser"))
				]
			];	
	}

	TSharedRef<SWidget> MakeStatusBarToolBarWidget()
	{
		RegisterStatusBarMenu();

		FToolMenuContext MenuContext;
		RegisterSourceControlStatus();

		return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar", MenuContext);
	}

	TSharedRef<SWidget> MakeDebugConsoleWidget(FSimpleDelegate OnConsoleClosed)
	{
		FOutputLogModule& OutputLogModule = FModuleManager::LoadModuleChecked<FOutputLogModule>(TEXT("OutputLog"));

		return
			SNew(SBox)
			.WidthOverride(350.f)
			[
				OutputLogModule.MakeConsoleInputBox(ConsoleEditBox, OnConsoleClosed)
			];
	}

	TSharedRef<SWidget> MakeStatusMessageWidget()
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

	FReply OnContentBrowserButtonClicked()
	{
		/*if(!ContentBrowserOverlayContent.IsValid())
		{
			TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

			Window->AddOverlaySlot()
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(0, 0, 0, GetTickSpaceGeometry().GetLocalSize().Y + 1))
				[
					SAssignNew(ContentBrowserOverlayContent, SBox)
					.HeightOverride(Window->GetSizeInScreen().Y * 0.3f)
					[
						GetContentBrowserDelegate.Execute()
					]
				];
		}*/

		return FReply::Handled();
	}

	void DismissContentBrowser()
	{
		if (ContentBrowserOverlayContent.IsValid())
		{
			TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

			Window->RemoveOverlaySlot(ContentBrowserOverlayContent.ToSharedRef());

			ContentBrowserOverlayContent.Reset();
		}
	}

	void RegisterStatusBarMenu()
	{
		static const FName StatusBarToolBarName("StatusBar.ToolBar");
		UToolMenus* ToolMenus = UToolMenus::Get();
		if (ToolMenus->IsMenuRegistered(StatusBarToolBarName))
		{
			return;
		}

		UToolMenu* ToolBar = ToolMenus->RegisterMenu(StatusBarToolBarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
		ToolBar->StyleName = "StatusBarToolBar";
	}

	void RegisterSourceControlStatus()
	{
		// Source Control preferences
		FSourceControlMenuHelpers::CheckSourceControlStatus();
		{
			UToolMenu* SourceControlMenu = UToolMenus::Get()->ExtendMenu("StatusBar.ToolBar");
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
	TSharedPtr<SWidget> ContentBrowserOverlayContent;
	FOnGetContent GetContentBrowserDelegate;
	const FSlateBrush* UpArrow;
	const FSlateBrush* DownArrow;
	FName StatusBarName;
};

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
				StatusBarPinned->FocusDebugConsole();

				bFocusedSuccessfully = true;
				break;
			}
		}
	}

	return bFocusedSuccessfully;
}

TSharedRef<SWidget> UStatusBarSubsystem::MakeStatusBarWidget(FName StatusBarName, const TSharedRef<SDockTab>& InParentTab)
{
	TSharedRef<SStatusBar> StatusBar =
		SNew(SStatusBar, StatusBarName, InParentTab)
		.OnConsoleClosed_UObject(this, &UStatusBarSubsystem::OnDebugConsoleClosed)
		.OnGetContentBrowser_UObject(this, &UStatusBarSubsystem::OnGetContentBrowser);

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

		StatusBarContentBrowser = ContentBrowserSingleton.CreateContentBrowser("StatusBarContentBrowser", nullptr, &Config);
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

#undef LOCTEXT_NAMESPACE