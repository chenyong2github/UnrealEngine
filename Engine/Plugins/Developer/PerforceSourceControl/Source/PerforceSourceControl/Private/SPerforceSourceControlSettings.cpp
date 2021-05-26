// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPerforceSourceControlSettings.h"
#include "PerforceSourceControlPrivate.h"
#include "Widgets/Views/STableRow.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"
#include "EditorStyleSet.h"
#include "ISourceControlModule.h"
#include "PerforceSourceControlModule.h"
#include "Widgets/Images/SThrobber.h"

TWeakPtr<SEditableTextBox> SPerforceSourceControlSettings::PasswordTextBox;

#define LOCTEXT_NAMESPACE "SPerforceSourceControlSettings"

static bool bAllowP4NonTicketBasedLogins = false;
FAutoConsoleVariableRef CVarAllowP4NonTicketBasedLogins(
	TEXT("SourceControl.P4.AllowNonTicketLogins"),
	bAllowP4NonTicketBasedLogins,
	TEXT("Whether or not to allow logging in with a password directly from the perforce dialog. This is off by default because it is not a secure option. Perforce often your password as plain text in their enviroment variables")
);

void SPerforceSourceControlSettings::Construct(const FArguments& InArgs)
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );

	bAreAdvancedSettingsExpanded = false;

	// check our settings & query if we don't already have any
	FString PortName = PerforceSourceControl.AccessSettings().GetPort();
	FString UserName = PerforceSourceControl.AccessSettings().GetUserName();

	if (PortName.IsEmpty() && UserName.IsEmpty())
	{
		ClientApi TestP4;
		TestP4.SetProg("UE");
		Error P4Error;
		TestP4.Init(&P4Error);
		PortName = ANSI_TO_TCHAR(TestP4.GetPort().Text());
		UserName = ANSI_TO_TCHAR(TestP4.GetUser().Text());
		TestP4.Final(&P4Error);

		PerforceSourceControl.AccessSettings().SetPort(PortName);
		PerforceSourceControl.AccessSettings().SetUserName(UserName);
		PerforceSourceControl.SaveSettings();
	}

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PortLabel", "Server"))
				.ToolTipText( LOCTEXT("PortLabel_Tooltip", "The server and port for your Perforce server. Usage ServerName:1234.") )
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UserNameLabel", "User Name"))
				.ToolTipText( LOCTEXT("UserNameLabel_Tooltip", "Perforce username.") )
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WorkspaceLabel", "Workspace"))
				.ToolTipText( LOCTEXT("WorkspaceLabel_Tooltip", "Perforce workspace.") )
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AutoWorkspaces", "Available Workspaces"))
				.ToolTipText( LOCTEXT("AutoWorkspaces_Tooltip", "Choose from a list of available workspaces. Requires a server and username before use.") )
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Visibility(bAllowP4NonTicketBasedLogins ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(LOCTEXT("HostLabel", "Host"))
				.ToolTipText(LOCTEXT("HostLabel_Tooltip", "If you wish to impersonate a particular host, enter this here. This is not normally needed."))
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Visibility(bAllowP4NonTicketBasedLogins ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(LOCTEXT("PasswordLabel", "Password"))
				.ToolTipText(LOCTEXT("PasswordLabel_Tooltip", "Perforce password. This normally only needs to be entered if your ticket has expired."))
			]
		]
		+SHorizontalBox::Slot()
		.FillWidth(2.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			[
				SNew(SEditableTextBox)
				.Text(this, &SPerforceSourceControlSettings::GetPortText)
				.ToolTipText( LOCTEXT("PortLabel_Tooltip", "The server and port for your Perforce server. Usage ServerName:1234.") )
				.OnTextCommitted(this, &SPerforceSourceControlSettings::OnPortTextCommited)
				.OnTextChanged(this, &SPerforceSourceControlSettings::OnPortTextCommited, ETextCommit::Default)
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &SPerforceSourceControlSettings::GetUserNameText)
				.ToolTipText( LOCTEXT("UserNameLabel_Tooltip", "Perforce username.") )
				.OnTextCommitted(this, &SPerforceSourceControlSettings::OnUserNameTextCommited)
				.OnTextChanged(this, &SPerforceSourceControlSettings::OnUserNameTextCommited, ETextCommit::Default)
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &SPerforceSourceControlSettings::GetWorkspaceText)
				.ToolTipText( LOCTEXT("WorkspaceLabel_Tooltip", "Perforce workspace.") )
				.OnTextCommitted(this, &SPerforceSourceControlSettings::OnWorkspaceTextCommited)
				.OnTextChanged(this, &SPerforceSourceControlSettings::OnWorkspaceTextCommited, ETextCommit::Default)
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			[
				SAssignNew(WorkspaceCombo, SComboButton)
				.OnGetMenuContent(this, &SPerforceSourceControlSettings::OnGetMenuContent)
				.ContentPadding(1)
				.ToolTipText( LOCTEXT("AutoWorkspaces_Tooltip", "Choose from a list of available workspaces. Requires a server and username before use.") )
				.ButtonContent()
				[
					SNew( STextBlock )
					.Text( this, &SPerforceSourceControlSettings::OnGetButtonText )
				]
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Visibility(bAllowP4NonTicketBasedLogins ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(this, &SPerforceSourceControlSettings::GetHostText)
				.ToolTipText(LOCTEXT("HostLabel_Tooltip", "If you wish to impersonate a particular host, enter this here. This is not normally needed."))
				.OnTextCommitted(this, &SPerforceSourceControlSettings::OnHostTextCommited)
				.OnTextChanged(this, &SPerforceSourceControlSettings::OnHostTextCommited, ETextCommit::Default)
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			.VAlign(VAlign_Center)
			[
				SAssignNew(PasswordTextBox, SEditableTextBox)
				.Visibility(bAllowP4NonTicketBasedLogins ? EVisibility::Visible : EVisibility::Collapsed)
				.ToolTipText(LOCTEXT("PasswordLabel_Tooltip", "Perforce password. This normally only needs to be entered if your ticket has expired."))
				.IsPassword(true)
			]
		]	
	];

	// fire off the workspace query
	State = ESourceControlOperationState::NotQueried;
	QueryWorkspaces();
}

FString SPerforceSourceControlSettings::GetPassword()
{
	if(PasswordTextBox.IsValid())
	{
		return PasswordTextBox.Pin()->GetText().ToString();
	}
	return FString();
}

void SPerforceSourceControlSettings::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	// When a dialog is up, the editor stops ticking, and we take over:
	if( FSlateApplication::Get().GetActiveModalWindow().IsValid())
	{
		ISourceControlModule::Get().Tick();
	}
	return SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime );
}

FText SPerforceSourceControlSettings::GetPortText() const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	return FText::FromString(PerforceSourceControl.AccessSettings().GetPort());
}

void SPerforceSourceControlSettings::OnPortTextCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	PerforceSourceControl.AccessSettings().SetPort(InText.ToString());
	PerforceSourceControl.SaveSettings();
}

FText SPerforceSourceControlSettings::GetUserNameText() const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	return FText::FromString(PerforceSourceControl.AccessSettings().GetUserName());
}

void SPerforceSourceControlSettings::OnUserNameTextCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	PerforceSourceControl.AccessSettings().SetUserName(InText.ToString());
	PerforceSourceControl.SaveSettings();
}

FText SPerforceSourceControlSettings::GetWorkspaceText() const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	return FText::FromString(PerforceSourceControl.AccessSettings().GetWorkspace());
}

void SPerforceSourceControlSettings::OnWorkspaceTextCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	PerforceSourceControl.AccessSettings().SetWorkspace(InText.ToString());
	PerforceSourceControl.SaveSettings();
}

FText SPerforceSourceControlSettings::GetHostText() const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	return FText::FromString(PerforceSourceControl.AccessSettings().GetHostOverride());
}

void SPerforceSourceControlSettings::OnHostTextCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	PerforceSourceControl.AccessSettings().SetHostOverride(InText.ToString());
	PerforceSourceControl.SaveSettings();
}

void SPerforceSourceControlSettings::QueryWorkspaces()
{
	if(State != ESourceControlOperationState::Querying)
	{
		Workspaces.Empty();
		CurrentWorkspace = FString();

		// fire off the workspace query
		ISourceControlModule& SourceControl = FModuleManager::LoadModuleChecked<ISourceControlModule>( "SourceControl" );
		ISourceControlProvider& Provider = SourceControl.GetProvider();
		GetWorkspacesOperation = ISourceControlOperation::Create<FGetWorkspaces>();
		Provider.Execute(GetWorkspacesOperation.ToSharedRef(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SPerforceSourceControlSettings::OnSourceControlOperationComplete) );

		State = ESourceControlOperationState::Querying;
	}
}

void SPerforceSourceControlSettings::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	if(InResult == ECommandResult::Succeeded)
	{
		check(InOperation->GetName() == "GetWorkspaces");
		check(GetWorkspacesOperation == StaticCastSharedRef<FGetWorkspaces>(InOperation));

		// refresh workspaces list from operation results
		Workspaces.Empty();
		for(auto Iter(GetWorkspacesOperation->Results.CreateConstIterator()); Iter; Iter++)
		{
			Workspaces.Add(MakeShareable(new FString(*Iter)));
		}
	}

	GetWorkspacesOperation.Reset();
	State = ESourceControlOperationState::Queried;
}

TSharedRef<SWidget> SPerforceSourceControlSettings::OnGetMenuContent()
{
	// fire off the workspace query - we may have just edited the settings
	QueryWorkspaces();
	
	return
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SPerforceSourceControlSettings::GetThrobberVisibility)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SThrobber)
			]
			+SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WorkspacesOperationInProgress", "Looking for Perforce workspaces..."))
				.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))	
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.OnClicked(this, &SPerforceSourceControlSettings::OnCancelWorkspacesRequest)
				.Content()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
				]
			]
		]
		+SHorizontalBox::Slot()
		.FillWidth(1)
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoWorkspaces", "No Workspaces found!"))
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))	
			.Visibility(this, &SPerforceSourceControlSettings::GetNoWorkspacesVisibility)
		]
		+SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SListView< TSharedRef<FString> >)
			.ListItemsSource(&Workspaces)
			.OnGenerateRow(this, &SPerforceSourceControlSettings::OnGenerateWorkspaceRow)
			.Visibility(this, &SPerforceSourceControlSettings::GetWorkspaceListVisibility)
			.OnSelectionChanged(this, &SPerforceSourceControlSettings::OnWorkspaceSelected)
		];
}

EVisibility SPerforceSourceControlSettings::GetThrobberVisibility() const
{
	return State == ESourceControlOperationState::Querying ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPerforceSourceControlSettings::GetNoWorkspacesVisibility() const
{
	return State == ESourceControlOperationState::Queried && Workspaces.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPerforceSourceControlSettings::GetWorkspaceListVisibility() const
{
	return State == ESourceControlOperationState::Queried && Workspaces.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<ITableRow> SPerforceSourceControlSettings::OnGenerateWorkspaceRow(TSharedRef<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(SComboRow< TSharedRef<FString> >, OwnerTable)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1)
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(*InItem))
				.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		];
}

void SPerforceSourceControlSettings::OnWorkspaceSelected(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectInfo)
{
	CurrentWorkspace = *InItem;
	FPerforceSourceControlModule& PerforceSourceControl = FModuleManager::LoadModuleChecked<FPerforceSourceControlModule>( "PerforceSourceControl" );
	PerforceSourceControl.AccessSettings().SetWorkspace(CurrentWorkspace);
	PerforceSourceControl.SaveSettings();
	WorkspaceCombo->SetIsOpen(false);
}

FText SPerforceSourceControlSettings::OnGetButtonText() const
{
	return FText::FromString(CurrentWorkspace);
}

FReply SPerforceSourceControlSettings::OnCancelWorkspacesRequest()
{
	if(GetWorkspacesOperation.IsValid())
	{
		ISourceControlModule& SourceControl = FModuleManager::LoadModuleChecked<ISourceControlModule>( "SourceControl" );
		SourceControl.GetProvider().CancelOperation(GetWorkspacesOperation.ToSharedRef());
	}
	return FReply::Handled();
}

const FSlateBrush* SPerforceSourceControlSettings::GetAdvancedPulldownImage() const
{
	if( ExpanderButton->IsHovered() )
	{
		return bAreAdvancedSettingsExpanded ? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up.Hovered") : FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down.Hovered");
	}
	else
	{
		return bAreAdvancedSettingsExpanded ? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up") : FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down");
	}
}

EVisibility SPerforceSourceControlSettings::GetAdvancedSettingsVisibility() const
{
	return bAreAdvancedSettingsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SPerforceSourceControlSettings::OnAdvancedSettingsClicked()
{
	bAreAdvancedSettingsExpanded = !bAreAdvancedSettingsExpanded;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
