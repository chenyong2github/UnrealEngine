// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelEditorToolBox.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SUniformWrapPanel.h"
#include "StatusBarSubsystem.h"

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
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
		.Padding(0.0f)
		[
			SNew( SVerticalBox )
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign( HAlign_Left )
			[
				SAssignNew( ModeToolBarContainer, SBorder )
				.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
				.Padding( FMargin(4, 0, 0, 0) )
			]

			+ SVerticalBox::Slot()
			.FillHeight( 1.0f )
			[
				SNew( SVerticalBox )

				+SVerticalBox::Slot()
				.Padding(0.0, 8.0, 0.0, 0.0)
				.AutoHeight()
				[
					SAssignNew(ModeToolHeader, SBorder)
					.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
				]

				+ SVerticalBox::Slot()
				.FillHeight(1)
				[
					SAssignNew(InlineContentHolder, SBorder)
					.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
					.Visibility( this, &SLevelEditorToolBox::GetInlineContentHolderVisibility )
				]
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
		EditorModeTools.SetLabelVisibility( EVisibility::Collapsed );

		const FLevelEditorModesCommands& Commands = LevelEditorModule.GetLevelEditorModesCommands();

		for ( const FEditorModeInfo& Mode : GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetEditorModeInfoOrderedByPriority())
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

	ModeToolBarContainer->SetVisibility(EVisibility::Collapsed);

	const TArray<TSharedPtr<IToolkit>>& HostedToolkits = LevelEditor.Pin()->GetHostedToolkits();
	for(const TSharedPtr<IToolkit>& HostedToolkitIt : HostedToolkits)
	{
		UpdateInlineContent(HostedToolkitIt, HostedToolkitIt->GetInlineContent());
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
	static const FName LevelEditorStatusBarName = "LevelEditor.StatusBar";

	if (Toolkit.IsValid())
	{
		if(Toolkit->GetEditorMode() || Toolkit->GetScriptableEditorMode().IsValid())
		{
			TabName = Toolkit->GetEditorModeDisplayName();
			TabIcon = Toolkit->GetEditorModeIcon().GetSmallIcon();

			TWeakPtr<FModeToolkit> ModeToolkit = StaticCastSharedPtr<FModeToolkit>(Toolkit);

			TSharedRef< SUniformWrapPanel> PaletteTabBox = SNew(SUniformWrapPanel)
				.SlotPadding( FMargin(1.f, 2.f) )
				.HAlign(HAlign_Center);

			// Only show if there is more than one child in the switcher
			PaletteTabBox->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([PaletteTabBox]() -> EVisibility { return PaletteTabBox->GetChildren()->Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed; }) ));

			// Also build the toolkit here 
			TArray<FName> PaletteNames;
			ModeToolkit.Pin()->GetToolPaletteNames(PaletteNames);

			TSharedPtr<FUICommandList> CommandList;
			CommandList = ModeToolkit.Pin()->GetToolkitCommands();

			TSharedRef< SWidgetSwitcher > PaletteSwitcher = SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda( [PaletteNames, ModeToolkit] () -> int32 { 
				int32 FoundIndex;
				TSharedPtr<FModeToolkit> PinnedToolkit = ModeToolkit.Pin();
				if (PinnedToolkit.IsValid() && PaletteNames.Find(PinnedToolkit->GetCurrentPalette(), FoundIndex))
				{
					return FoundIndex;	
				}
				return 0;
			} );
			
			for(auto Palette : PaletteNames)
			{
				FName ToolbarCustomizationName = ModeToolkit.Pin()->GetEditorMode() ? ModeToolkit.Pin()->GetEditorMode()->GetModeInfo().ToolbarCustomizationName : ModeToolkit.Pin()->GetScriptableEditorMode()->GetModeInfo().ToolbarCustomizationName;
				FUniformToolBarBuilder ModeToolbarBuilder(CommandList, FMultiBoxCustomization(ToolbarCustomizationName));
				ModeToolbarBuilder.SetStyle(&FEditorStyle::Get(), "PaletteToolBar");

				ModeToolkit.Pin()->BuildToolPalette(Palette, ModeToolbarBuilder);

				TSharedRef<SWidget> PaletteWidget = ModeToolbarBuilder.MakeWidget();

				PaletteTabBox->AddSlot()
				[
					SNew(SCheckBox)
					.Padding(FMargin(8.f, 4.f, 8.f, 5.f))
					.Style( FEditorStyle::Get(),  "PaletteToolBar.Tab" )
					.OnCheckStateChanged_Lambda([/*PaletteSwitcher, PaletteWidget, */ModeToolkit, Palette](const ECheckBoxState) {
							TSharedPtr<FModeToolkit> PinnedToolkit = ModeToolkit.Pin();
							if (PinnedToolkit.IsValid())
							{
								PinnedToolkit->SetCurrentPalette(Palette);
							}
						} 
					)
					// .IsChecked_Lambda( [PaletteSwitcher, PaletteWidget] () -> ECheckBoxState { return PaletteSwitcher->GetActiveWidget() == PaletteWidget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.IsChecked_Lambda([ModeToolkit, Palette]() -> ECheckBoxState {
						TSharedPtr<FModeToolkit> PinnedToolkit = ModeToolkit.Pin();
						if (PinnedToolkit.IsValid() && (PinnedToolkit->GetCurrentPalette() == Palette))
						{
							return ECheckBoxState::Checked;
						}
						return ECheckBoxState::Unchecked;
					})
					[
						SNew(STextBlock)
						.TextStyle(FAppStyle::Get(), "NormalText")
						.Text(ModeToolkit.Pin()->GetToolPaletteDisplayName(Palette))
						.Justification(ETextJustify::Center)
					]
				];


				PaletteSwitcher->AddSlot()
				[
					PaletteWidget
				]; 
			}


			ModeToolHeader->SetContent(
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.Padding(8.f, 0.f, 0.f, 8.f)
				.AutoHeight()
				[
					PaletteTabBox
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					PaletteSwitcher
				]
			);

			if (StatusBarMessageHandle.IsValid())
			{
				GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(LevelEditorStatusBarName, StatusBarMessageHandle);
			}

			StatusBarMessageHandle = GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PushStatusBarMessage(LevelEditorStatusBarName, TAttribute<FText>::Create(
				TAttribute<FText>::FGetter::CreateSP(ModeToolkit.Pin().ToSharedRef(), &FModeToolkit::GetActiveToolDisplayName))
			);
		}
	}
	else
	{
		GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(LevelEditorStatusBarName, StatusBarMessageHandle);
		StatusBarMessageHandle.Reset();

		TabName = NSLOCTEXT("LevelEditor", "ToolsTabTitle", "Toolbox");
		TabIcon = FEditorStyle::Get().GetBrush("LevelEditor.Tabs.Modes");

		ModeToolHeader->SetContent(SNullWidget::NullWidget);
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
