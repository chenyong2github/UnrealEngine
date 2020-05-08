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
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "FileHelpers.h"

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

	static FReply OnSourceControlStatusButtonClicked(TWeakPtr<SMenuAnchor> MenuAnchor)
	{
		if (TSharedPtr<SMenuAnchor> MenuAnchorPinned = MenuAnchor.Pin())
		{
			MenuAnchorPinned->SetIsOpen(!MenuAnchorPinned->IsOpen());
		}

		return FReply::Handled();
	}

	static TSharedRef<SWidget> MakeSourceControlStatusWidget()
	{
		TSharedPtr<SMenuAnchor> MenuAnchor;

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
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs)
	{
		UpArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserUp");
		DownArrow = FAppStyle::Get().GetBrush("StatusBar.ContentBrowserDown");

		const FSlateBrush* StatusBarBackground = FAppStyle::Get().GetBrush("StatusBar.Background");
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
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("StatusBar.StatusBarButton"))
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
						]
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
					[
						SNullWidget::NullWidget
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

private:
	const FSlateBrush* GetContentBrowserExpandArrowImage() const
	{
		return DownArrow;
	}

	TSharedRef<SWidget> MakeStatusBarToolBarWidget()
	{
		RegisterStatusBarMenu();

		FToolMenuContext MenuContext;
		RegisterSourceControlStatus();


		return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar", MenuContext);
	}

	void RegisterStatusBarMenu()
	{
		static const FName StatusBarName("StatusBar.ToolBar");
		UToolMenus* ToolMenus = UToolMenus::Get();
		if (ToolMenus->IsMenuRegistered(StatusBarName))
		{
			return;
		}

		UToolMenu* ToolBar = ToolMenus->RegisterMenu(StatusBarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
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
private:
	const FSlateBrush* UpArrow;
	const FSlateBrush* DownArrow;
};


void UStatusBarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FSourceControlCommands::Register();
}

void UStatusBarSubsystem::Deinitialize()
{
	FSourceControlCommands::Unregister();
}

TSharedRef<SWidget> UStatusBarSubsystem::MakeStatusBarWidget(FName StatusBarName)
{
	TSharedRef<SStatusBar> StatusBar = SNew(SStatusBar);

	StatusBars.Add(StatusBarName, StatusBar);

	return StatusBar;
}

#undef LOCTEXT_NAMESPACE