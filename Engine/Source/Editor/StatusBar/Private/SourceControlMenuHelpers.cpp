// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlMenuHelpers.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "ISourceControlWindowsModule.h"
#include "SourceControlMenuHelpers.h"
#include "SourceControlWindows.h"
#include "FileHelpers.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "LevelEditorActions.h"

#define LOCTEXT_NAMESPACE "SourceControlCommands"

TSharedRef<FUICommandList> FSourceControlCommands::ActionList(new FUICommandList());

FSourceControlCommands::FSourceControlCommands() 
	: TCommands<FSourceControlCommands>
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
void FSourceControlCommands::RegisterCommands()
{
	UI_COMMAND(ConnectToSourceControl, "Connect to Source Control...", "Connect to source control to allow source control operations to be performed on content and levels.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ChangeSourceControlSettings, "Change Source Control Settings...", "Opens a dialog to change source control settings.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ViewChangelists, "View Changelists", "Opens a dialog displaying current changelists.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SubmitContent, "Submit Content", "Opens a dialog with check in options for content and levels.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CheckOutModifiedFiles, "Check Out Modified Files", "Opens a dialog to check out any assets which have been modified.", EUserInterfaceActionType::Button, FInputChord());

	ActionList->MapAction(
		ConnectToSourceControl,
		FExecuteAction::CreateStatic(&FSourceControlCommands::ConnectToSourceControl_Clicked)
	);

	ActionList->MapAction(
		ChangeSourceControlSettings,
		FExecuteAction::CreateStatic(&FSourceControlCommands::ConnectToSourceControl_Clicked)
	);

	ActionList->MapAction(
		ViewChangelists,
		FExecuteAction::CreateStatic(&FSourceControlCommands::ViewChangelists_Clicked),
		FCanExecuteAction::CreateStatic(&FSourceControlCommands::ViewChangelists_CanExecute)
	);

	ActionList->MapAction(
		SubmitContent,
		FExecuteAction::CreateLambda([]() { FSourceControlWindows::ChoosePackagesToCheckIn(); }),
		FCanExecuteAction::CreateStatic(&FSourceControlWindows::CanChoosePackagesToCheckIn)
	);

	ActionList->MapAction(
		CheckOutModifiedFiles,
		FExecuteAction::CreateStatic(&FSourceControlCommands::CheckOutModifiedFiles_Clicked),
		FCanExecuteAction::CreateStatic(&FSourceControlCommands::CheckOutModifiedFiles_CanExecute)
	);
}

void FSourceControlCommands::ConnectToSourceControl_Clicked()
{
	// Show login window regardless of current status - its useful as a shortcut to change settings.
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	SourceControlModule.ShowLoginDialog(FSourceControlLoginClosed(), ELoginWindowMode::Modeless, EOnLoginWindowStartup::PreserveProvider);
}

bool FSourceControlCommands::ViewChangelists_CanExecute()
{
	return ISourceControlWindowsModule::Get().CanShowChangelistsTab();
}

void FSourceControlCommands::ViewChangelists_Clicked()
{
	ISourceControlWindowsModule::Get().ShowChangelistsTab();
}

bool FSourceControlCommands::CheckOutModifiedFiles_CanExecute()
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

void FSourceControlCommands::CheckOutModifiedFiles_Clicked()
{
	TArray<UPackage*> PackagesToSave;
	FEditorFileUtils::GetDirtyWorldPackages(PackagesToSave);
	FEditorFileUtils::GetDirtyContentPackages(PackagesToSave);

	const bool bCheckDirty = true;
	const bool bPromptUserToSave = false;
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptUserToSave);
}

FSourceControlMenuHelpers::EQueryState FSourceControlMenuHelpers::QueryState = FSourceControlMenuHelpers::EQueryState::NotQueried;

void FSourceControlMenuHelpers::CheckSourceControlStatus()
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

void FSourceControlMenuHelpers::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	QueryState = EQueryState::Queried;
}

TSharedRef<SWidget> FSourceControlMenuHelpers::GenerateSourceControlMenuContent()
{
	UToolMenu* SourceControlMenu = UToolMenus::Get()->RegisterMenu("StatusBar.ToolBar.SourceControl", NAME_None, EMultiBoxType::Menu, false);

	FToolMenuSection& Section = SourceControlMenu->AddSection("SourceControlActions", LOCTEXT("SourceControlMenuHeadingActions", "Actions"));

	Section.AddMenuEntry(
		FSourceControlCommands::Get().ViewChangelists,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.ChangelistsTab")
	);

	Section.AddMenuEntry(
		FSourceControlCommands::Get().SubmitContent,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Submit")
	);

	Section.AddMenuEntry(
		FSourceControlCommands::Get().CheckOutModifiedFiles,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.CheckOut")
	);

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

	return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar.SourceControl", FToolMenuContext(FSourceControlCommands::ActionList));
}

FText FSourceControlMenuHelpers::GetSourceControlStatusText()
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
				return LOCTEXT("SourceControlStatus_Error_ServerUnavailable", "Server Unavailable");
			}
			else
			{
				return LOCTEXT("SourceControlStatus_Available", "Source Control");
			}
		}
		else
		{
			return LOCTEXT("SourceControlStatus_Error_Off", "Source Control Off");
		}
	}
}
FText FSourceControlMenuHelpers::GetSourceControlTooltip()
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

const FSlateBrush* FSourceControlMenuHelpers::GetSourceControlIcon()
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

TSharedRef<SWidget> FSourceControlMenuHelpers::MakeSourceControlStatusWidget()
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
			.Padding(FMargin(5, 0, 0, 0))
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Text_Static(&FSourceControlMenuHelpers::GetSourceControlStatusText)
			]
		]
		.OnGetMenuContent(FOnGetContent::CreateStatic(&FSourceControlMenuHelpers::GenerateSourceControlMenuContent))
		;
}

#undef LOCTEXT_NAMESPACE