// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelEditorToolBox.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/InputBindingManager.h"
#include "EditorModeRegistry.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelEditorModesActions.h"
#include "Classes/EditorStyleSettings.h"
#include "EdMode.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SLevelEditorToolBox"

SLevelEditorToolBox::~SLevelEditorToolBox()
{
	if (UObjectInitialized())
	{
		GetMutableDefault<UEditorPerProjectUserSettings>()->OnUserSettingChanged().RemoveAll(this);
	}
}

void SLevelEditorToolBox::Construct( const FArguments& InArgs, const TSharedRef< class ILevelEditor >& OwningLevelEditor )
{
	TabIcon = FEditorStyle::Get().GetBrush("LevelEditor.Tabs.Modes");
	LevelEditor = OwningLevelEditor;

	// Important: We use a raw binding here because we are releasing our binding in our destructor (where a weak pointer would be invalid)
	// It's imperative that our delegate is removed in the destructor for the level editor module to play nicely with reloading.

	GetMutableDefault<UEditorPerProjectUserSettings>()->OnUserSettingChanged().AddRaw( this, &SLevelEditorToolBox::HandleUserSettingsChange );

	ChildSlot
	[
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign( HAlign_Left )
		.Padding(1.f)
		[
			SAssignNew( ModeToolBarContainer, SBorder )
			.BorderImage( FEditorStyle::GetBrush( "NoBorder" ) )
			.Padding( FMargin(4, 0, 0, 0) )
		]

		+ SVerticalBox::Slot()
		.FillHeight( 1.0f )
		.Padding( 2, 0, 0, 0 )
		[
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			[
				SAssignNew(InlineContentHolder, SBorder)
				.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
				.Padding(0.f)
				.Visibility( this, &SLevelEditorToolBox::GetInlineContentHolderVisibility )
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign( HAlign_Center )
			.Padding( 2.0f, 14.0f, 2.0f, 2.0f )
			[
				SNew( STextBlock )
				.Text( LOCTEXT("NoToolSelected", "Select a tool to display its options.") )
				.Visibility( this, &SLevelEditorToolBox::GetNoToolSelectedTextVisibility )
			]
		]
	];

	UpdateModeLegacyToolBar();
}

void SLevelEditorToolBox::HandleUserSettingsChange( FName PropertyName )
{
	UpdateModeLegacyToolBar();
}

void SLevelEditorToolBox::OnEditorModeCommandsChanged()
{
	UpdateModeLegacyToolBar();
}

void SLevelEditorToolBox::SetParentTab(TSharedRef<SDockTab>& InDockTab)
{
	ParentTab = InDockTab;
	InDockTab->SetLabel(TabName);
	InDockTab->SetTabIcon(TabIcon);
}

void SLevelEditorToolBox::UpdateModeLegacyToolBar()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( "LevelEditor");
	const TSharedPtr< const FUICommandList > CommandList = LevelEditorModule.GetGlobalLevelEditorActions();
	const TSharedPtr<FExtender> ModeBarExtenders = LevelEditorModule.GetModeBarExtensibilityManager()->GetAllExtenders();

	FToolBarBuilder EditorModeTools( CommandList, FMultiBoxCustomization::None, ModeBarExtenders );
	{
		EditorModeTools.SetStyle(&FEditorStyle::Get(), "EditorModesToolbar");
		EditorModeTools.SetLabelVisibility( EVisibility::Collapsed );

		const FLevelEditorModesCommands& Commands = LevelEditorModule.GetLevelEditorModesCommands();

		for ( const FEditorModeInfo& Mode : FEditorModeRegistry::Get().GetSortedModeInfo() )
		{
			// If the mode isn't visible don't create a menu option for it.
			if ( !Mode.bVisible )
			{
				continue;
			}

			FName EditorModeCommandName = FName( *( FString( "EditorMode." ) + Mode.ID.ToString() ) );

			TSharedPtr<FUICommandInfo> EditorModeCommand =
				FInputBindingManager::Get().FindCommandInContext( Commands.GetContextName(), EditorModeCommandName );

			// If a command isn't yet registered for this mode, we need to register one.
			if ( !EditorModeCommand.IsValid() )
			{
				continue;
			}

			const FUIAction* UIAction = EditorModeTools.GetTopCommandList()->GetActionForCommand( EditorModeCommand );
			if ( ensure( UIAction ) )
			{
				EditorModeTools.AddToolBarButton( EditorModeCommand, Mode.ID, Mode.Name, Mode.Name, Mode.IconBrush, Mode.ID );// , EUserInterfaceActionType::ToggleButton );
			}
		}
	}

	if (GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
	{
		ModeToolBarContainer->SetContent(EditorModeTools.MakeWidget());
	}
	else
	{
		ModeToolBarContainer->SetVisibility(EVisibility::Collapsed);
	}

	const TArray<TSharedPtr<IToolkit>>& HostedToolkits = LevelEditor.Pin()->GetHostedToolkits();
	for(const TSharedPtr<IToolkit>& HostedToolkitIt : HostedToolkits)
	{
		UpdateInlineContent(HostedToolkitIt, HostedToolkitIt->GetInlineContent());
		break;
	}
}

EVisibility SLevelEditorToolBox::GetInlineContentHolderVisibility() const
{
	return InlineContentHolder->GetContent() == SNullWidget::NullWidget ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SLevelEditorToolBox::GetNoToolSelectedTextVisibility() const
{
	return InlineContentHolder->GetContent() == SNullWidget::NullWidget ? EVisibility::Visible : EVisibility::Collapsed;
}

void SLevelEditorToolBox::UpdateInlineContent(const TSharedPtr<IToolkit>& Toolkit, TSharedPtr<SWidget> InlineContent) 
{
	TabName = NSLOCTEXT("LevelEditor", "ToolsTabTitle", "Toolbox");
	TabIcon = FEditorStyle::Get().GetBrush("LevelEditor.Tabs.Modes");

	if (Toolkit.IsValid())
	{
		TabName = Toolkit->GetEditorModeDisplayName();
		TabIcon = Toolkit->GetEditorModeIcon().GetSmallIcon();
	}

	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	if (InlineContent.IsValid() && InlineContentHolder.IsValid())
	{
		InlineContentHolder->SetContent(InlineContent.ToSharedRef());
	}

	if (ParentTabPinned.IsValid())
	{
		ParentTabPinned->SetLabel(TabName);
		ParentTabPinned->SetTabIcon(TabIcon);
	}
}

void SLevelEditorToolBox::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	UpdateInlineContent(Toolkit, Toolkit->GetInlineContent());
}

void SLevelEditorToolBox::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	bool FoundAnotherToolkit = false;
	const TArray<TSharedPtr<IToolkit>>& HostedToolkits = LevelEditor.Pin()->GetHostedToolkits();
	for (const TSharedPtr<IToolkit>& HostedToolkitIt : HostedToolkits)
	{
		if (HostedToolkitIt != Toolkit)
		{
			UpdateInlineContent(HostedToolkitIt, HostedToolkitIt->GetInlineContent());
			FoundAnotherToolkit = true;
			break;
		}
	}

	if (!FoundAnotherToolkit)
	{
		UpdateInlineContent(nullptr, SNullWidget::NullWidget );
	}
}

#undef LOCTEXT_NAMESPACE
