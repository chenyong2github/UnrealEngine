// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "Action/SRCActionPanel.h"
#include "ActorEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Behaviour/SRCBehaviourPanel.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Commands/RemoteControlCommands.h"
#include "Controller/SRCControllerPanel.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "EditorFontGlyphs.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "FileHelpers.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Interfaces/IMainFrameModule.h"
#include "IRemoteControlModule.h"
#include "IRemoteControlProtocolWidgetsModule.h"
#include "ISettingsModule.h"
#include "IStructureDetailsView.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "PropertyEditorModule.h"
#include "RCPanelWidgetRegistry.h"
#include "RemoteControlActor.h"
#include "RemoteControlEntity.h"
#include "RemoteControlField.h"
#include "RemoteControlLogger.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "RemoteControlSettings.h"
#include "ScopedTransaction.h"
#include "SClassViewer.h"
#include "SRCLogger.h"
#include "SRCPanelExposedEntitiesList.h"
#include "SRCPanelFunctionPicker.h"
#include "SRCPanelExposedActor.h"
#include "SRCPanelExposedField.h"
#include "SRCPanelTreeNode.h"
#include "Styling/ToolBarStyle.h"
#include "Subsystems/Subsystem.h"
#include "SWarningOrErrorBox.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Templates/UnrealTypeTraits.h"
#include "Toolkits/IToolkitHost.h"
#include "ToolMenus.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UI/BaseLogicUI/SRCLogicPanelListBase.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

const FName SRemoteControlPanel::DefaultRemoteControlPanelToolBarName("RemoteControlPanel.DefaultToolBar");
const FName SRemoteControlPanel::AuxiliaryRemoteControlPanelToolBarName("RemoteControlPanel.AuxiliaryToolBar");
const float SRemoteControlPanel::MinimumPanelWidth = 640.f;

static TAutoConsoleVariable<bool> CVarRemoteControlEnableLogicUI(TEXT("RemoteControl.EnableLogicUI"), false, TEXT("Toggles visibility of Remote Control Logic User Interface (part of the Remote Control Preset asset)"));

namespace RemoteControlPanelUtils
{
	bool IsExposableActor(AActor* Actor)
	{
		return Actor->IsEditable()
            && Actor->IsListedInSceneOutliner()						// Only add actors that are allowed to be selected and drawn in editor
            && !Actor->IsTemplate()									// Should never happen, but we never want CDOs
            && !Actor->HasAnyFlags(RF_Transient)					// Don't add transient actors in non-play worlds
            && !FActorEditorUtils::IsABuilderBrush(Actor)			// Don't add the builder brush
            && !Actor->IsA(AWorldSettings::StaticClass());	// Don't add the WorldSettings actor, even though it is technically editable
	};

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	}

	TSharedPtr<FStructOnScope> GetEntityOnScope(const TSharedPtr<FRemoteControlEntity>& Entity, const UScriptStruct* EntityType)
	{
		if (ensure(Entity && EntityType))
		{
			check(EntityType->IsChildOf(FRemoteControlEntity::StaticStruct()));
			return MakeShared<FStructOnScope>(EntityType, reinterpret_cast<uint8*>(Entity.Get()));
		}

		return nullptr;
	}
}

/**
 * UI representation of a auto resizing button.
 */
class SAutoResizeButton : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SAutoResizeButton)
		: _ForceSmallIcons(false)
		, _UICommand(nullptr)
		, _IconOverride()
	{}

		SLATE_ATTRIBUTE(bool, ForceSmallIcons)

		/** UI Command must be mapped to MainFrame Command List. */
		SLATE_ARGUMENT(TSharedPtr<const FUICommandInfo>, UICommand)

		SLATE_ARGUMENT(TAttribute<FSlateIcon>, IconOverride)

	SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs)
	{
		bForceSmallIcons = InArgs._ForceSmallIcons;

		UICommand = InArgs._UICommand;

		IconOverride = InArgs._IconOverride;

		// Mimic Toolbar button style
		const FToolBarStyle& ToolBarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("AssetEditorToolbar");

		// Get the label & tooltip from the UI Command.
		const TAttribute<FText> ActualLabel = UICommand.IsValid() ? UICommand->GetLabel() : FText::GetEmpty();
		const TAttribute<FText> ActualToolTip = UICommand.IsValid() ? UICommand->GetDescription() : FText::GetEmpty();

		// If we were supplied an image than go ahead and use that, otherwise we use a null widget
		TSharedRef<SLayeredImage> IconWidget =
			SNew(SLayeredImage)
			.ColorAndOpacity(this, &SAutoResizeButton::GetIconForegroundColor)
			.Visibility(EVisibility::HitTestInvisible)
			.Image(this, &SAutoResizeButton::GetIconBrush);

		ChildSlot
		.Padding(ToolBarStyle.ButtonPadding.Left, 0.f, ToolBarStyle.ButtonPadding.Right, 0.f)
			[
				SNew(SButton)
				.ContentPadding(ToolBarStyle.ButtonPadding)
				.ButtonStyle(&ToolBarStyle.ButtonStyle)
				.OnClicked(this, &SAutoResizeButton::OnClicked)
				.ToolTipText(ActualToolTip)
				.Content()
				[
					SNew(SHorizontalBox)

					// Icon Widget
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						IconWidget
					]
					
					// Label Text
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(ToolBarStyle.LabelPadding)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Visibility(this, &SAutoResizeButton::GetLabelVisibility)
						.Text(ActualLabel)
						.TextStyle(&ToolBarStyle.LabelStyle)	// Smaller font for tool tip labels
					]
				]
			];
	}

private:
	/** Called by Slate to determine whether labels are visible */
	EVisibility GetLabelVisibility() const
	{
		const bool bUseSmallIcons = bForceSmallIcons.IsSet() ? bForceSmallIcons.Get() : false;

		return bUseSmallIcons ? EVisibility::Collapsed : EVisibility::Visible;
	}

	/** Gets the icon brush for the toolbar block widget */
	const FSlateBrush* GetIconBrush() const
	{
		const FSlateIcon ActionIcon = UICommand.IsValid() ?  UICommand->GetIcon() : FSlateIcon();
		const FSlateIcon& ActualIcon = IconOverride.IsSet() ? IconOverride.Get() : ActionIcon;

		return ActualIcon.GetIcon();
	}

	FSlateColor GetIconForegroundColor() const
	{
		// If any brush has a tint, don't assume it should be subdued
		const FSlateBrush* Brush = GetIconBrush();
		if (Brush && Brush->TintColor != FLinearColor::White)
		{
			return FLinearColor::White;
		}

		return FSlateColor::UseForeground();
	}

	/**
	 * Called by Slate when this tool bar button's button is clicked
	 */
	FReply OnClicked()
	{
		IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

		TSharedPtr<FUICommandList> ActionList = MainFrame.GetMainFrameCommandBindings();

		if (ActionList.IsValid() && UICommand.IsValid())
		{
			ActionList->ExecuteAction(UICommand.ToSharedRef());
		}

		return FReply::Handled();
	}

	TAttribute<bool> bForceSmallIcons;

	TSharedPtr<const FUICommandInfo> UICommand;

	TAttribute<FSlateIcon> IconOverride;
};

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TSharedPtr<IToolkitHost> InToolkitHost)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	WidgetRegistry = MakeShared<FRCPanelWidgetRegistry>();
	ToolkitHost = InToolkitHost;

	TArray<TSharedRef<SWidget>> ExtensionWidgets;
	FRemoteControlUIModule::Get().GetExtensionGenerators().Broadcast(ExtensionWidgets);

	BindRemoteControlCommands();

	// Rebind All
	AddToolbarWidget(SNew(SButton)
		.Visibility_Lambda([this]() { return bShowRebindButton ? EVisibility::Visible : EVisibility::Collapsed; })
		.OnClicked_Raw(this, &SRemoteControlPanel::OnClickRebindAllButton)
		[
			SNew(STextBlock)
			.ToolTipText(LOCTEXT("RebindButtonToolTip", "Attempt to rebind all unbound entites of the preset."))
			.Text(LOCTEXT("RebindButtonText", "Rebind All"))
		]);

	// Show Log
	AddToolbarWidget(SNew(SCheckBox)
		.Style(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Switch")
		.ToolTipText(LOCTEXT("ShowLogTooltip", "Show/Hide remote control log."))
		.IsChecked_Lambda([]() { return FRemoteControlLogger::Get().IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged(this, &SRemoteControlPanel::OnLogCheckboxToggle)
		.Padding(4.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ShowLogLabel", "Log"))
			.Justification(ETextJustify::Center)
		]);

	// Edit Mode
	AddToolbarWidget(SNew(SCheckBox)
		.Style(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Switch")
		.ToolTipText(LOCTEXT("EditModeTooltip", "Toggle Editing (Ctrl + E)"))
		.IsChecked_Lambda([this]() { return this->bIsInEditMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.Padding(4.f)
		.OnCheckStateChanged(this, &SRemoteControlPanel::OnEditModeCheckboxToggle)
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EditLabel", "Edit"))
		]
	);

	// Extension Generators
	for (const TSharedRef<SWidget>& Widget : ExtensionWidgets)
	{
		AddToolbarWidget(Widget);
	}

	GenerateToolbar();
	GenerateAuxiliaryToolbar();

	UpdateRebindButtonVisibility();

	TSharedPtr<SHorizontalBox> TopExtensions;

	EntityProtocolDetails = SNew(SBox);
	
	EntityList = SNew(SRCPanelExposedEntitiesList, Preset.Get(), WidgetRegistry)
		.OnEntityListUpdated_Lambda([this] ()
		{
			UpdateEntityDetailsView(EntityList->GetSelectedEntity());
			UpdateRebindButtonVisibility();
			CachedExposedPropertyArgs.Reset();
		})
		.EditMode_Lambda([this](){ return bIsInEditMode; });
	
	EntityList->OnSelectionChange().AddSP(this, &SRemoteControlPanel::UpdateEntityDetailsView);

	const TAttribute<float> TreeBindingSplitRatioTop = TAttribute<float>::Create(
		TAttribute<float>::FGetter::CreateLambda([]()
		{
			URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
			return Settings->TreeBindingSplitRatio;
		}));

	const TAttribute<float> TreeBindingSplitRatioBottom = TAttribute<float>::Create(
		TAttribute<float>::FGetter::CreateLambda([]()
		{
			URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
			return 1.0f - Settings->TreeBindingSplitRatio;
		}));

	TSharedRef<SWidget> ExposedPropertyPanel =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(3.f)
		.AutoHeight()
		[
			ToolbarWidgetContent.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			// Separator
			SNew(SSeparator)
			.SeparatorImage(FAppStyle::Get().GetBrush("Separator"))
			.Thickness(5.f)
			.Orientation(EOrientation::Orient_Horizontal)
		]
		+ SVerticalBox::Slot()
		.Padding(3.f)
		.AutoHeight()
		[
			// Auxiliary Toolbar
			SNew(SHorizontalBox)

			// Search Box
			+ SHorizontalBox::Slot()
			.Padding(5.f, 3.f)
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SAssignNew(SearchBoxPtr, SSearchBox)
			]

			// Filters
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(5.f, 3.f, 3.f, 3.f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButtonWithIcon"))
				.ForegroundColor(FSlateColor::UseStyle())
				.ToolTipText(LOCTEXT("Filters_Tooltip", "Filter options for the RC Preset."))
				.HasDownArrow(true)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RemoteControlFiltersCombo")))
				.ContentPadding(FMargin(1.f, 0.f))
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			
			// Settings
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(3.f, 3.f, 3.f, 3.f)
			[
				SNew(SButton)
				.ContentPadding(2.0f)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
				.OnClicked_Raw(this, &SRemoteControlPanel::OnClickSettingsButton)
				.ToolTipText(LOCTEXT("OpenRemoteControlSettings", "Open Remote Control settings."))
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Toolbar.Settings"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			// Separator
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Fill)
			.AutoWidth()
			.Padding(3.f, 3.f, 0.f, 3.f)
			[
				SNew(SSeparator)
				.SeparatorImage(FAppStyle::Get().GetBrush("Separator"))
				.Thickness(2.f)
				.Orientation(EOrientation::Orient_Vertical)
			]

			// Mini Toolbar Widget
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(3.f)
			[
				AuxiliaryToolbarWidgetContent.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			// Top tool bar
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 0, 2.0f, 0.0f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SRemoteControlPanel::OnSavePreset)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0,0,5, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Save"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Save", "Save"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 0, 5.0f, 0.0f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SRemoteControlPanel::OnCreateGroup)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("SceneOutliner.NewFolderIcon"))
				]
			]
			// Function library picker
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				CreateExposeButton()
			]
			// Right aligned widgets
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.FillWidth(1.0f)
			.Padding(0, 7.0f)
			[
				SAssignNew(TopExtensions, SHorizontalBox)
				// Rebind button
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.Visibility_Lambda([this]() { return bShowRebindButton ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked_Raw(this, &SRemoteControlPanel::OnClickRebindAllButton)
					[
						SNew(STextBlock)
						.ToolTipText(LOCTEXT("RebindButtonToolTip", "Attempt to rebind all unbound entites of the preset."))
						.Text(LOCTEXT("RebindButtonText", "Rebind All"))
					]
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
					.ToolTipText(LOCTEXT("EntityDetailsToolTip", "Open the details panel for the selected exposed entity."))
					.OnClicked_Lambda([this](){ ToggleDetailsView(); return FReply::Handled(); })
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("LevelEditor.Tabs.Details"))
					]
				]
				// Edit Mode
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditModeLabel_Editor", "Edit Mode : "))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Switch")
					.ToolTipText(LOCTEXT("EditModeTooltip", "Toggle Editing (Ctrl + E)"))
					.IsChecked_Lambda([this]() { return this->bIsInEditMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.Padding(2.f)
					.OnCheckStateChanged(this, &SRemoteControlPanel::OnEditModeCheckboxToggle)
				]
				// Enable Log
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableLogLabel", "Enable Log: "))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([]() { return FRemoteControlLogger::Get().IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &SRemoteControlPanel::OnLogCheckboxToggle)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.f))
				[
					SNew(SSeparator)
					.Orientation(Orient_Vertical)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SAssignNew(PresetNameTextBlock, STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
					.Text(FText::FromName(Preset->GetFName()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.f))
				[
					SNew(SSeparator)
					.Orientation(Orient_Vertical)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(2.0f)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
					.OnClicked_Raw(this, &SRemoteControlPanel::OnClickSettingsButton)
					[
						SNew(STextBlock)
						.ColorAndOpacity(FColor::White)
						.ToolTipText(LOCTEXT("OpenRemoteControlSettings", "Open Remote Control settings."))
						.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FEditorFontGlyphs::Cogs)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(.8f)
			[
				SNew(SBorder)
				.Padding(FMargin(0.f, 5.f, 0.f, 0.f))
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this](){ return !bIsInEditMode ? 0 : 1; })
					+ SWidgetSwitcher::Slot()
					[
						// Exposed entities List
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
						[
							EntityList.ToSharedRef()
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SSplitter)
						.Orientation(EOrientation::Orient_Vertical)						
						+ SSplitter::Slot()
						.Value(TreeBindingSplitRatioTop)
						.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([](float InNewSize)
						{
							URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
							Settings->TreeBindingSplitRatio = InNewSize;
							Settings->PostEditChange();
							Settings->SaveConfig();
						}))
						[
							// Exposed entities List
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
							[
								EntityList.ToSharedRef()
							]
						]
						+ SSplitter::Slot()
						.Value(TreeBindingSplitRatioBottom)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
							[
								EntityProtocolDetails.ToSharedRef()
							]
						]
					]
				]
			]
			+ SSplitter::Slot()
			.Value(.2f)
			[
				SNew(SRCLogger)
			]
		]
		// Use less CPU Warning
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(5.f, 8.f, 5.f, 5.f))
		[
			CreateCPUThrottleWarning()
		];

	TabManager = FGlobalTabmanager::New();
	const TSharedPtr<FTabManager::FStack> ControlPanelStackManager = FTabManager::NewStack();
	const constexpr TCHAR* ExposedPropertyTabName = TEXT("Expose Property Tab");
	const constexpr TCHAR* ControllerTabName = TEXT("Controller Tab");
	const FOnSpawnTab OnExposedPropertySpawnTab = FOnSpawnTab::CreateLambda([ExposedPropertyPanel] (const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
		[
			ExposedPropertyPanel
		];
	});
	const FOnSpawnTab OnControllerSpawnTab = FOnSpawnTab::CreateLambda([this] (const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
		[
			SAssignNew(ControllerPanel, SRCControllerPanel, SharedThis(this))
		];
	});
	const FCanSpawnTab CanSpawnPanelTab = FCanSpawnTab::CreateLambda([this](const FSpawnTabArgs& InArgs)
	{
		return true;
	});
	if (!TabManager->HasTabSpawner(ExposedPropertyTabName))
	{
		TabManager->RegisterTabSpawner(ExposedPropertyTabName, OnExposedPropertySpawnTab, CanSpawnPanelTab);
	}
	if (!TabManager->HasTabSpawner(ControllerTabName))
	{
		TabManager->RegisterTabSpawner(ControllerTabName, OnControllerSpawnTab, CanSpawnPanelTab);
	}
	
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("RemoteControlPanelLayout1.0") 
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.5f)
			->AddTab(ControllerTabName, ETabState::OpenedTab)
		)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.5f)
			->AddTab(ExposedPropertyTabName, ETabState::OpenedTab)
		)
	);
		
	// Make 3 Columns with Exposed Properties + Controllers / Behaviours / Actions
	TSharedRef<SSplitter> SplitterBox = SNew(SSplitter)
	.Orientation(Orient_Horizontal)
	+ SSplitter::Slot()
	.Value(0.33)
	[
		TabManager->RestoreFrom(Layout, nullptr).ToSharedRef()
	]
	+ SSplitter::Slot()
	.Value(0.33)
	[
		SAssignNew(BehaviourPanel, SRCBehaviourPanel, SharedThis(this))
	]
	+ SSplitter::Slot()
	.Value(0.33)
	[
		SAssignNew(ActionPanel, SRCActionPanel, SharedThis(this))
	];

	if (CVarRemoteControlEnableLogicUI.GetValueOnAnyThread())
	{
		ChildSlot
		[
			SplitterBox
		];
	}
	else
	{
		ChildSlot
			[
				ExposedPropertyPanel
			];
	}

	RegisterEvents();
	CacheLevelClasses();
	Refresh();
}

SRemoteControlPanel::~SRemoteControlPanel()
{
	UnbindRemoteControlCommands();
	UnregisterEvents();

	// Clear the log
	FRemoteControlLogger::Get().ClearLog();

	// Remove protocol bindings
	IRemoteControlProtocolWidgetsModule& ProtocolWidgetsModule = FModuleManager::LoadModuleChecked<IRemoteControlProtocolWidgetsModule>("RemoteControlProtocolWidgets");
	ProtocolWidgetsModule.ResetProtocolBindingList();	
}

void SRemoteControlPanel::PostUndo(bool bSuccess)
{
	Refresh();
}

void SRemoteControlPanel::PostRedo(bool bSuccess)
{
	Refresh();
}

bool SRemoteControlPanel::IsExposed(const FRCExposesPropertyArgs& InPropertyArgs)
{
	if (!ensure(InPropertyArgs.IsValid()))
	{
		return false;
	}

	const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();

	auto CheckCachedExposedArgs = [this, InPropertyArgs](const TArray<UObject*> InOwnerObjects, const FString& InPath, bool bIsCheckIsBoundByFullPath)
	{
		if (CachedExposedPropertyArgs.Contains(InPropertyArgs))
		{
			return true;
		}

		const bool bAllObjectsExposed = IsAllObjectsExposed(InOwnerObjects, InPath, bIsCheckIsBoundByFullPath);

		if (bAllObjectsExposed)
		{
			CachedExposedPropertyArgs.Emplace(InPropertyArgs);
		}

		return bAllObjectsExposed;
	};

	if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		TArray<UObject*> OuterObjects;
		InPropertyArgs.PropertyHandle->GetOuterObjects(OuterObjects);
		const FString Path = InPropertyArgs.PropertyHandle->GeneratePathToProperty();

		constexpr bool bIsCheckIsBoundByFullPath = true;
		return CheckCachedExposedArgs({ OuterObjects }, Path, bIsCheckIsBoundByFullPath);
	}
	else if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		constexpr bool bIsCheckIsBoundByFullPath = false;
		return CheckCachedExposedArgs({ InPropertyArgs.OwnerObject }, InPropertyArgs.PropertyPath, bIsCheckIsBoundByFullPath);
	}

	// It never should hit this point
	ensure(false);

	return false;
}


bool SRemoteControlPanel::IsAllObjectsExposed(TArray<UObject*> InOuterObjects, const FString& InPath, bool bUsingDuplicatesInPath)
{
	TArray<TSharedPtr<FRemoteControlProperty>, TInlineAllocator<1>> PotentialMatches;
	for (const TWeakPtr<FRemoteControlProperty>& WeakProperty : Preset->GetExposedEntities<FRemoteControlProperty>())
	{
		if (TSharedPtr<FRemoteControlProperty> Property = WeakProperty.Pin())
		{
			// If that was exposed by property path it should be checked by the full path with duplicated like propertypath.propertypath[0]
			// If that was exposed by the owner object it should be without duplicated in the path, just propertypath[0]
			const bool Isbound = bUsingDuplicatesInPath ? Property->CheckIsBoundToPropertyPath(InPath) : Property->CheckIsBoundToString(InPath);

			if (Isbound)
			{
				PotentialMatches.Add(Property);
			}
		}
	}

	bool bAllObjectsExposed = true;

	for (UObject* OuterObject : InOuterObjects)
	{
		bool bFoundPropForObject = false;

		for (const TSharedPtr<FRemoteControlProperty>& Property : PotentialMatches)
		{
			if (Property->ContainsBoundObjects({ InOuterObjects } ))
			{
				bFoundPropForObject = true;
				break;
			}
		}

		bAllObjectsExposed &= bFoundPropForObject;
	}

	return bAllObjectsExposed;
}

void SRemoteControlPanel::ToggleProperty(const FRCExposesPropertyArgs& InPropertyArgs)
{
	if (!ensure(InPropertyArgs.IsValid()))
	{
		return;
	}

	if (IsExposed(InPropertyArgs))
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeProperty", "Unexpose Property"));
		Preset->Modify();
		Unexpose(InPropertyArgs);
		return;
	}


	auto PreExpose = [this]()
	{
		FScopedTransaction Transaction(LOCTEXT("ExposeProperty", "Expose Property"));
		Preset->Modify();
	};

	auto PostExpose = [this, InPropertyArgs]()
	{
		CachedExposedPropertyArgs.Emplace(InPropertyArgs);
	};

	const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();
	if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		TSet<UObject*> UniqueOuterObjects;
		{
			// Make sure properties are only being exposed once per object.
			TArray<UObject*> OuterObjects;
			InPropertyArgs.PropertyHandle->GetOuterObjects(OuterObjects);
			UniqueOuterObjects.Append(MoveTemp(OuterObjects));
		}

		if (UniqueOuterObjects.Num())
		{
			PreExpose();

			for (UObject* Object : UniqueOuterObjects)
			{
				if (Object)
				{
					constexpr bool bCleanDuplicates = true; // GeneratePathToProperty duplicates container name (Array.Array[1], Set.Set[1], etc...)
					ExposeProperty(Object, FRCFieldPathInfo{ InPropertyArgs.PropertyHandle->GeneratePathToProperty(), bCleanDuplicates });
				}
			}

			PostExpose();
		}
	}
	else if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		PreExpose();

		constexpr bool bCleanDuplicates = true; // GeneratePathToProperty duplicates container name (Array.Array[1], Set.Set[1], etc...)
		ExposeProperty(InPropertyArgs.OwnerObject, FRCFieldPathInfo{ InPropertyArgs.PropertyPath, bCleanDuplicates });

		PostExpose();
	}
}

FGuid SRemoteControlPanel::GetSelectedGroup() const
{
	if (TSharedPtr<SRCPanelTreeNode> Node = EntityList->GetSelectedGroup())
	{
		return Node->GetRCId();
	}
	return FGuid();
}

FReply SRemoteControlPanel::OnClickDisableUseLessCPU() const
{
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = false;
	Settings->PostEditChange();
	Settings->SaveConfig();
	return FReply::Handled();
}

TSharedRef<SWidget> SRemoteControlPanel::CreateCPUThrottleWarning() const
{
	FProperty* PerformanceThrottlingProperty = FindFieldChecked<FProperty>(UEditorPerformanceSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UEditorPerformanceSettings, bThrottleCPUWhenNotForeground));
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("PropertyName"), PerformanceThrottlingProperty->GetDisplayNameText());
	FText PerformanceWarningText = FText::Format(LOCTEXT("RemoteControlPerformanceWarning", "Warning: The editor setting '{PropertyName}' is currently enabled\nThis will stop editor windows from updating in realtime while the editor is not in focus"), Arguments);

	return SNew(SWarningOrErrorBox)
		.Visibility_Lambda([]() { return GetDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground ? EVisibility::Visible : EVisibility::Collapsed; })
		.MessageStyle(EMessageStyle::Warning)
		.Message(PerformanceWarningText)
		[
			SNew(SButton)
			.OnClicked(this, &SRemoteControlPanel::OnClickDisableUseLessCPU)
			.TextStyle(FAppStyle::Get(), "DialogButtonText")
			.Text(LOCTEXT("RemoteControlPerformanceWarningDisable", "Disable"))
		];
}

TSharedRef<SWidget> SRemoteControlPanel::CreateExposeButton()
{	
	FMenuBuilder MenuBuilder(true, nullptr);
	
	SAssignNew(BlueprintPicker, SRCPanelFunctionPicker)
		.AllowDefaultObjects(true)
		.Label(LOCTEXT("FunctionLibrariesLabel", "Function Libraries"))
		.ObjectClass(UBlueprintFunctionLibrary::StaticClass())
		.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction);

	SAssignNew(SubsystemFunctionPicker, SRCPanelFunctionPicker)
		.Label(LOCTEXT("SubsystemFunctionLabel", "Subsystem Functions"))
		.ObjectClass(USubsystem::StaticClass())
		.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction);

	SAssignNew(ActorFunctionPicker, SRCPanelFunctionPicker)
		.Label(LOCTEXT("ActorFunctionsLabel", "Actor Functions"))
		.ObjectClass(AActor::StaticClass())
		.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction);
	
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ExposeHeader", "Expose"));
	{
		constexpr bool bNoIndent = true;
		constexpr bool bSearchable = false;

		auto CreatePickerSubMenu = [this, bNoIndent, bSearchable, &MenuBuilder] (const FText& Label, const FText& ToolTip, const TSharedRef<SWidget>& Widget)
		{
			MenuBuilder.AddSubMenu(
				Label,
				ToolTip,
				FNewMenuDelegate::CreateLambda(
					[this, bNoIndent, bSearchable, Widget](FMenuBuilder& MenuBuilder)
					{
						MenuBuilder.AddWidget(Widget, FText::GetEmpty(), bNoIndent, bSearchable);
						FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::Navigation);
					}
				)
			);
		};

		CreatePickerSubMenu(
			LOCTEXT("BlueprintFunctionLibraryFunctionSubMenu", "Blueprint Function Library Function"),
			LOCTEXT("FunctionLibraryFunctionSubMenuToolTip", "Expose a function from a blueprint function library."),
			BlueprintPicker.ToSharedRef()
		);
		
		CreatePickerSubMenu(
			LOCTEXT("SubsystemFunctionSubMenu", "Subsystem Function"),
			LOCTEXT("SubsystemFunctionSubMenuToolTip", "Expose a function from a subsytem."),
			SubsystemFunctionPicker.ToSharedRef()
		);
		
		CreatePickerSubMenu(
			LOCTEXT("ActorFunctionSubMenu", "Actor Function"),
			LOCTEXT("ActorFunctionSubMenuToolTip", "Expose an actor's function."),
			ActorFunctionPicker.ToSharedRef()
		);
		
		MenuBuilder.AddWidget(
			SNew(SObjectPropertyEntryBox)
				.AllowedClass(AActor::StaticClass())
				.OnObjectChanged(this, &SRemoteControlPanel::OnExposeActor)
				.AllowClear(false)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.NewAssetFactories(TArray<UFactory*>()),
			LOCTEXT("ActorEntry", "Actor"));

		CreatePickerSubMenu(
			LOCTEXT("ClassPickerEntry", "Actors By Class"),
			LOCTEXT("ClassPickerEntrySubMenuToolTip", "Expose all actors of the chosen class."),
			CreateExposeByClassWidget()
		);
	}
	MenuBuilder.EndSection();
	
	return SAssignNew(ExposeComboButton, SComboButton)
		.Visibility_Lambda([this]() { return this->bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonStyle(FAppStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FSlateColor::UseForeground())
		.CollapseMenuOnParentFocus(true)
		.ContentPadding(FMargin(10.0f, 0.f))
		.ButtonContent()
		[
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "ContentBrowser.TopBar.Font")
			.Text(LOCTEXT("ExposeButtonLabel", "Expose"))
		]
		.MenuContent()
		[
			MenuBuilder.MakeWidget()
		];
}

TSharedRef<SWidget> SRemoteControlPanel::CreateExposeByClassWidget()
{
	class FActorClassInLevelFilter : public IClassViewerFilter
	{
	public:
		FActorClassInLevelFilter(const TSet<TWeakObjectPtr<const UClass>>& InClasses)
			: Classes(InClasses)
		{
		}
		
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return Classes.Contains(TWeakObjectPtr<const UClass>{InClass});
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const class IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return false;
		}

	public:		
		const TSet<TWeakObjectPtr<const UClass>>& Classes;
	};

	TSharedPtr<FActorClassInLevelFilter> Filter = MakeShared<FActorClassInLevelFilter>(CachedClassesInLevel);
	
	FClassViewerInitializationOptions Options;
	{
		Options.ClassFilters.Add(Filter.ToSharedRef());
		Options.bIsPlaceableOnly = true;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.DisplayMode = EClassViewerDisplayMode::ListView;
		Options.bShowObjectRootClass = true;
		Options.bShowNoneOption = false;
		Options.bShowUnloadedBlueprints = false;
	}
	
	TSharedRef<SWidget> Widget = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, FOnClassPicked::CreateLambda(
		[this](UClass* ChosenClass)
		{
			if (UWorld* World = RemoteControlPanelUtils::GetEditorWorld())
			{
				for (TActorIterator<AActor> It(World, ChosenClass, EActorIteratorFlags::SkipPendingKill); It; ++It)
				{
					if (RemoteControlPanelUtils::IsExposableActor(*It))
					{
						ExposeActor(*It);
					}
				}
			}

			if (ExposeComboButton)
			{
				ExposeComboButton->SetIsOpen(false);
			}
		}));

	ClassPicker = StaticCastSharedRef<SClassViewer>(Widget);

	return SNew(SBox)
		.MinDesiredWidth(200.f)
		[
			Widget
		];
}

void SRemoteControlPanel::CacheLevelClasses()
{
	CachedClassesInLevel.Empty();	
	if (UWorld* World = RemoteControlPanelUtils::GetEditorWorld())
	{
		for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
		{
			CacheActorClass(*It);
		}
		
		if (ClassPicker)
		{
			ClassPicker->Refresh();
		}
	}
}

void SRemoteControlPanel::OnActorAddedToLevel(AActor* Actor)
{
	if (Actor)
	{
		CacheActorClass(Actor);
		if (ClassPicker)
		{
			ClassPicker->Refresh();
		}

		UpdateActorFunctionPicker();
	}
}

void SRemoteControlPanel::OnLevelActorsRemoved(AActor* Actor)
{
	if (Actor)
	{
		if (ClassPicker)
		{
			ClassPicker->Refresh();
		}

		UpdateActorFunctionPicker();
	}
}

void SRemoteControlPanel::OnLevelActorListChanged()
{
	UpdateActorFunctionPicker();
}

void SRemoteControlPanel::CacheActorClass(AActor* Actor)
{
	if (RemoteControlPanelUtils::IsExposableActor(Actor))
	{
		UClass* Class = Actor->GetClass();
		do
		{
			CachedClassesInLevel.Emplace(Class);
			Class = Class->GetSuperClass();
		}
		while(Class != UObject::StaticClass() && Class != nullptr);
	}
}

void SRemoteControlPanel::OnMapChange(uint32)
{
	CacheLevelClasses();
	
	if (ClassPicker)
	{
		ClassPicker->Refresh();	
	}

	UpdateRebindButtonVisibility();

	// Clear the widget cache on map change to make sure we don't keep widgets around pointing to potentially stale objects.
	WidgetRegistry->Clear();
	Refresh();
}

void SRemoteControlPanel::BindRemoteControlCommands()
{
	const FRemoteControlCommands& Commands = FRemoteControlCommands::Get();

	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	FUICommandList& ActionList = *MainFrame.GetMainFrameCommandBindings();

	ActionList.MapAction(
		Commands.SavePreset,
		FExecuteAction::CreateSP(this, &SRemoteControlPanel::SaveAsset_Execute),
		FCanExecuteAction::CreateSP(this, &SRemoteControlPanel::CanSaveAsset));

	ActionList.MapAction(
		Commands.FindPresetInContentBrowser,
		FExecuteAction::CreateSP(this, &SRemoteControlPanel::FindInContentBrowser_Execute),
		FCanExecuteAction::CreateSP(this, &SRemoteControlPanel::CanFindInContentBrowser));

	ActionList.MapAction(
		Commands.ToggleExposeFunctions,
		FExecuteAction::CreateLambda([this] { OnToggleExposeFunctions(); }),
		FCanExecuteAction::CreateLambda([] { return true; }));

	ActionList.MapAction(
		Commands.ToggleProtocolMappings,
		FExecuteAction::CreateLambda([this] { OnToggleProtocolMappings(); }),
		FCanExecuteAction::CreateLambda([] { return true; }));

	ActionList.MapAction(
		Commands.ToggleLogicEditor,
		FExecuteAction::CreateLambda([this] { OnToggleLogicEditor(); }),
		FCanExecuteAction::CreateLambda([] { return true; }));

	ActionList.MapAction(
		Commands.DeleteEntity,
		FExecuteAction::CreateSP(this, &SRemoteControlPanel::DeleteEntity_Execute),
		FCanExecuteAction::CreateSP(this, &SRemoteControlPanel::CanDeleteEntity));

	ActionList.MapAction(
		Commands.RenameEntity,
		FExecuteAction::CreateSP(this, &SRemoteControlPanel::RenameEntity_Execute),
		FCanExecuteAction::CreateSP(this, &SRemoteControlPanel::CanRenameEntity));
}

void SRemoteControlPanel::UnbindRemoteControlCommands()
{
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		const FRemoteControlCommands& Commands = FRemoteControlCommands::Get();

		IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

		FUICommandList& ActionList = *MainFrame.GetMainFrameCommandBindings();

		ActionList.UnmapAction(Commands.SavePreset);
		ActionList.UnmapAction(Commands.FindPresetInContentBrowser);
		ActionList.UnmapAction(Commands.ToggleExposeFunctions);
		ActionList.UnmapAction(Commands.ToggleProtocolMappings);
		ActionList.UnmapAction(Commands.ToggleLogicEditor);
		ActionList.UnmapAction(Commands.DeleteEntity);
		ActionList.UnmapAction(Commands.RenameEntity);
	}
}

void SRemoteControlPanel::RegisterEvents()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetRenamed().AddSP(this, &SRemoteControlPanel::OnAssetRenamed);
	FEditorDelegates::MapChange.AddSP(this, &SRemoteControlPanel::OnMapChange);
	
	if (GEditor)
	{
		GEditor->OnBlueprintReinstanced().AddSP(this, &SRemoteControlPanel::OnBlueprintReinstanced);
	}

	if (GEngine)
	{
		GEngine->OnLevelActorAdded().AddSP(this, &SRemoteControlPanel::OnActorAddedToLevel);
		GEngine->OnLevelActorListChanged().AddSP(this, &SRemoteControlPanel::OnLevelActorListChanged);
		GEngine->OnLevelActorDeleted().AddSP(this, &SRemoteControlPanel::OnLevelActorsRemoved);
	}

	Preset->OnEntityExposed().AddSP(this, &SRemoteControlPanel::OnEntityExposed);
	Preset->OnEntityUnexposed().AddSP(this, &SRemoteControlPanel::OnEntityUnexposed);

	UMaterial::OnMaterialCompilationFinished().AddSP(this, &SRemoteControlPanel::OnMaterialCompiled);
}

void SRemoteControlPanel::UnregisterEvents()
{
	Preset->OnEntityExposed().RemoveAll(this);
	Preset->OnEntityUnexposed().RemoveAll(this);
	
	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().RemoveAll(this);
		GEngine->OnLevelActorListChanged().RemoveAll(this);
		GEngine->OnLevelActorAdded().RemoveAll(this);
	}
	
	if (GEditor)
	{
		GEditor->OnBlueprintReinstanced().RemoveAll(this);
	}

	FEditorDelegates::MapChange.RemoveAll(this);
	
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetRenamed().RemoveAll(this);
	
	UMaterial::OnMaterialCompilationFinished().RemoveAll(this);
}

void SRemoteControlPanel::Refresh()
{
	BlueprintPicker->Refresh();
	ActorFunctionPicker->Refresh();
	SubsystemFunctionPicker->Refresh();
	EntityList->Refresh();
}

void SRemoteControlPanel::AddToolbarWidget(TSharedRef<SWidget> Widget)
{
	ToolbarWidgets.AddUnique(Widget);
}

void SRemoteControlPanel::RemoveAllToolbarWidgets()
{
	ToolbarWidgets.Empty();
}

void SRemoteControlPanel::Unexpose(const FRCExposesPropertyArgs& InPropertyArgs)
{
	if (!InPropertyArgs.IsValid())
	{
		return;
	}

	auto CheckAndExpose = [&](TArray<UObject*> InOuterObjects, const FString& InPath)
	{
		// Find an exposed property with the same path.
		TArray<TSharedPtr<FRemoteControlProperty>, TInlineAllocator<1>> PotentialMatches;
		for (const TWeakPtr<FRemoteControlProperty>& WeakProperty : Preset->GetExposedEntities<FRemoteControlProperty>())
		{
			if (TSharedPtr<FRemoteControlProperty> Property = WeakProperty.Pin())
			{
				if (Property->CheckIsBoundToString(InPath))
				{
					PotentialMatches.Add(Property);
				}
			}
		}

		for (const TSharedPtr<FRemoteControlProperty>& Property : PotentialMatches)
		{
			if (Property->ContainsBoundObjects(InOuterObjects))
			{
				Preset->Unexpose(Property->GetId());
				break;
			}
		}
	};

	const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();

	if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		TArray<UObject*> OuterObjects;
		InPropertyArgs.PropertyHandle->GetOuterObjects(OuterObjects);

		CheckAndExpose(OuterObjects, InPropertyArgs.PropertyHandle->GeneratePathToProperty());
	}
	else if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		CheckAndExpose({ InPropertyArgs.OwnerObject }, InPropertyArgs.PropertyPath);
	}
}


void SRemoteControlPanel::OnEditModeCheckboxToggle(ECheckBoxState State)
{
	bIsInEditMode = (State == ECheckBoxState::Checked) ? true : false;
	OnEditModeChange.ExecuteIfBound(SharedThis(this), bIsInEditMode);
}

void SRemoteControlPanel::OnLogCheckboxToggle(ECheckBoxState State)
{
	const bool bIsLogEnabled = (State == ECheckBoxState::Checked) ? true : false;
	FRemoteControlLogger::Get().EnableLog(bIsLogEnabled);
}
 
void SRemoteControlPanel::OnBlueprintReinstanced()
{
	Refresh();
}

FReply SRemoteControlPanel::OnCreateGroup()
{
	FScopedTransaction Transaction(LOCTEXT("CreateGroup", "Create Group"));
	Preset->Modify();
	Preset->Layout.CreateGroup();
	return FReply::Handled();
}

FReply SRemoteControlPanel::OnSavePreset()
{
	if (!Preset.IsValid() || !Preset->IsAsset())
	{
		UE_LOG(LogRemoteControl, Log, TEXT("Invalid object to save: %s"), (Preset.IsValid()) ? *Preset->GetFullName() : TEXT("Null Object"));
		
		return FReply::Handled();
	}

	TArray<UPackage*> PackagesToSave;
	constexpr bool bCheckDirtyOnAssetSave = false;
	constexpr bool bPromptToSave = false;
	
	PackagesToSave.Add(Preset->GetOutermost());
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirtyOnAssetSave, bPromptToSave);
	
	return FReply::Handled();
}

void SRemoteControlPanel::ExposeProperty(UObject* Object, FRCFieldPathInfo Path)
{
	if (Path.Resolve(Object))
	{
		FRemoteControlPresetExposeArgs Args;
		Args.GroupId = GetSelectedGroup();
		Preset->ExposeProperty(Object, MoveTemp(Path), MoveTemp(Args));
	}
}

void SRemoteControlPanel::ExposeFunction(UObject* Object, UFunction* Function)
{
	if (ExposeComboButton)
	{
		ExposeComboButton->SetIsOpen(false);
	}
	
	FScopedTransaction Transaction(LOCTEXT("ExposeFunction", "ExposeFunction"));
	Preset->Modify();

	FRemoteControlPresetExposeArgs Args;
	Args.GroupId = GetSelectedGroup();
	Preset->ExposeFunction(Object, Function, MoveTemp(Args));
}

void SRemoteControlPanel::OnExposeActor(const FAssetData& AssetData)
{
	ExposeActor(Cast<AActor>(AssetData.GetAsset()));
}

void SRemoteControlPanel::ExposeActor(AActor* Actor)
{
	if (Actor)
	{
		FScopedTransaction Transaction(LOCTEXT("ExposeActor", "Expose Actor"));
		Preset->Modify();
		
		FRemoteControlPresetExposeArgs Args;
		Args.GroupId = GetSelectedGroup();
		
		Preset->ExposeActor(Actor, Args);
	}
}

void SRemoteControlPanel::ToggleDetailsView()
{
	const FTabId TabId = FTabId(FRemoteControlUIModule::EntityDetailsTabName);
	
	if (TSharedPtr<IToolkitHost> PinnedToolkit = ToolkitHost.Pin())
	{
		if (TSharedPtr<SDockTab> ExistingTab = PinnedToolkit->GetTabManager()->FindExistingLiveTab(TabId))
		{
			ExistingTab->RequestCloseTab();
		}
		else
		{
			// Request the Tab Manager to invoke the tab. This will spawn the tab if needed, otherwise pull it to focus. This assumes
			// that the Toolkit Host's Tab Manager has already registered a tab with a NullWidget for content.
			if (TSharedPtr<SDockTab> EntityDetailsTab = PinnedToolkit->GetTabManager()->TryInvokeTab(TabId))
			{
				EntityDetailsTab->SetContent(CreateEntityDetailsView());
			}
		}
	}
}

TSharedRef<SWidget> SRemoteControlPanel::CreateEntityDetailsView()
{
	FDetailsViewArgs Args;
	Args.bShowOptions = false;
	Args.bAllowFavoriteSystem = false;
	Args.bAllowSearch = false;
	Args.bShowScrollBar = false;

	EntityDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateStructureDetailView(MoveTemp(Args), FStructureDetailsViewArgs(), nullptr);

	UpdateEntityDetailsView(EntityList->GetSelectedEntity());
	if (ensure(EntityDetailsView && EntityDetailsView->GetWidget()))
	{
		return EntityDetailsView->GetWidget().ToSharedRef();
	}

	return SNullWidget::NullWidget;
}

void SRemoteControlPanel::UpdateEntityDetailsView(const TSharedPtr<SRCPanelTreeNode>& SelectedNode)
{
	TSharedPtr<FStructOnScope> SelectedEntityPtr;

	SelectedEntity = SelectedNode;

	if (SelectedNode)
	{
		if (SelectedNode->GetRCType() != SRCPanelTreeNode::Group &&
			SelectedNode->GetRCType() != SRCPanelTreeNode::FieldChild) // Field Child does not contain entity ID, that is why it should not be processed
		{
			TSharedPtr<FRemoteControlEntity> Entity = Preset->GetExposedEntity<FRemoteControlEntity>(SelectedNode->GetRCId()).Pin();
			SelectedEntityPtr = RemoteControlPanelUtils::GetEntityOnScope(Entity, Preset->GetExposedEntityType(SelectedNode->GetRCId()));
		}
	}

	if (EntityDetailsView)
	{
		EntityDetailsView->SetStructureData(SelectedEntityPtr);
	}

	static const FName ProtocolWidgetsModuleName = "RemoteControlProtocolWidgets";	
	if(SelectedNode.IsValid() && FModuleManager::Get().IsModuleLoaded(ProtocolWidgetsModuleName) && ensure(Preset.IsValid()))
	{
		if (TSharedPtr<FRemoteControlEntity> RCEntity = Preset->GetExposedEntity(SelectedNode->GetRCId()).Pin())
		{
			if(RCEntity->IsBound())
			{
				IRemoteControlProtocolWidgetsModule& ProtocolWidgetsModule = FModuleManager::LoadModuleChecked<IRemoteControlProtocolWidgetsModule>(ProtocolWidgetsModuleName);
				EntityProtocolDetails->SetContent(ProtocolWidgetsModule.GenerateDetailsForEntity(Preset.Get(), RCEntity->GetId()));
			}
			else
			{
				EntityProtocolDetails->SetContent(SNullWidget::NullWidget);
			}
		}
	}
}

void SRemoteControlPanel::UpdateRebindButtonVisibility()
{
	if (URemoteControlPreset* PresetPtr = Preset.Get())
	{
		for (TWeakPtr<FRemoteControlEntity> WeakEntity : PresetPtr->GetExposedEntities<FRemoteControlEntity>())
		{
			if (TSharedPtr<FRemoteControlEntity> Entity = WeakEntity.Pin())
			{
				if (!Entity->IsBound())
				{
					bShowRebindButton = true;
					return;
				}
			}
		}
	}

	bShowRebindButton = false;
}

FReply SRemoteControlPanel::OnClickRebindAllButton()
{
	if (URemoteControlPreset* PresetPtr = Preset.Get())
	{
		PresetPtr->RebindUnboundEntities();

		UpdateRebindButtonVisibility();
	}
	return FReply::Handled();
}

void SRemoteControlPanel::UpdateActorFunctionPicker()
{
	if (GEditor && ActorFunctionPicker && !NextTickTimerHandle.IsValid())
	{
		NextTickTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakPanelPtr = TWeakPtr<SRemoteControlPanel>(StaticCastSharedRef<SRemoteControlPanel>(AsShared()))]()
		{
			if (TSharedPtr<SRemoteControlPanel> PanelPtr = WeakPanelPtr.Pin())
			{
				PanelPtr->ActorFunctionPicker->Refresh();
				PanelPtr->NextTickTimerHandle.Invalidate();
			}
		}));
	}
}

void SRemoteControlPanel::OnAssetRenamed(const FAssetData& Asset, const FString&)
{
	if (Asset.GetAsset() == Preset.Get())
	{
		if (PresetNameTextBlock)
		{
			PresetNameTextBlock->SetText(FText::FromName(Asset.AssetName));	
		}
	}
}

void SRemoteControlPanel::OnEntityExposed(URemoteControlPreset* InPreset, const FGuid& InEntityId)
{
	CachedExposedPropertyArgs.Empty();
}

void SRemoteControlPanel::OnEntityUnexposed(URemoteControlPreset* InPreset, const FGuid& InEntityId)
{
	CachedExposedPropertyArgs.Empty();
}

FReply SRemoteControlPanel::OnClickSettingsButton()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Remote Control");
	return FReply::Handled();
}

void SRemoteControlPanel::OnMaterialCompiled(UMaterialInterface* MaterialInterface)
{
	// Clear the widget cache on material compiled to make sure we have valid property nodes for IPropertyRowGenerator
	WidgetRegistry->Clear();
	Refresh();
}

void SRemoteControlPanel::RegisterDefaultToolBar()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(DefaultRemoteControlPanelToolBarName))
	{
		UToolMenu* ToolbarBuilder = ToolMenus->RegisterMenu(DefaultRemoteControlPanelToolBarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);

#if 0
		ToolbarBuilder->StyleName = "AssetEditorToolbar";
#endif
		{
			FToolMenuSection& AssetSection = ToolbarBuilder->AddSection("Asset");
			AssetSection.AddEntry(FToolMenuEntry::InitToolBarButton(FRemoteControlCommands::Get().SavePreset, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Save"))));
			AssetSection.AddEntry(FToolMenuEntry::InitToolBarButton(FRemoteControlCommands::Get().FindPresetInContentBrowser, LOCTEXT("FindInContentBrowserButton", "Browse"), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.OpenContentBrowser"))));
			AssetSection.AddSeparator("Common");
		}
	}
}

void SRemoteControlPanel::GenerateToolbar()
{
	RegisterDefaultToolBar();

	ToolbarWidgetContent = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		[
			SNullWidget::NullWidget
		];

	UToolMenus* ToolMenus = UToolMenus::Get();
	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	UToolMenu* GeneratedToolbar = ToolMenus->FindMenu(DefaultRemoteControlPanelToolBarName);
	
	GeneratedToolbar->Context = FToolMenuContext(MainFrame.GetMainFrameCommandBindings());
	
	TSharedRef<class SWidget> ToolBarWidget = ToolMenus->GenerateWidget(GeneratedToolbar);

	TSharedRef<SWidget> MiscWidgets = SNullWidget::NullWidget;

	if (ToolbarWidgets.Num() > 0)
	{
		TSharedRef<SHorizontalBox> MiscWidgetsHBox = SNew(SHorizontalBox);

		for (int32 WidgetIdx = 0; WidgetIdx < ToolbarWidgets.Num(); ++WidgetIdx)
		{
			MiscWidgetsHBox->AddSlot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SSeparator)
					.SeparatorImage(FAppStyle::Get().GetBrush("Separator"))
					.Thickness(1.5f)
					.Orientation(EOrientation::Orient_Vertical)
				];

			MiscWidgetsHBox->AddSlot()
				.Padding(5.f, 0.f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					ToolbarWidgets[WidgetIdx]
				];
		}

		MiscWidgets = MiscWidgetsHBox;
	}

	Toolbar =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ToolBarWidget
		]
		+ SHorizontalBox::Slot()
		.Padding(5.f, 0.f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.FillWidth(1.f)
		[
			SNew(STextBlock)
			.Text(this, &SRemoteControlPanel::HandlePresetName)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoWidth()
		[
			MiscWidgets
		];

	if (ToolbarWidgetContent.IsValid())
	{
		ToolbarWidgetContent->SetContent(Toolbar.ToSharedRef());
	}
}

void SRemoteControlPanel::RegisterAuxiliaryToolBar()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(AuxiliaryRemoteControlPanelToolBarName))
	{
		UToolMenu* ToolbarBuilder = ToolMenus->RegisterMenu(AuxiliaryRemoteControlPanelToolBarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);

#if 0
		ToolbarBuilder->StyleName = "AssetEditorToolbar";
#endif
		{
			FToolMenuSection& ToolsSection = ToolbarBuilder->AddSection("Tools");

			ToolsSection.AddEntry(FToolMenuEntry::InitWidget("Functions"
				, SNew(SAutoResizeButton)
					.UICommand(FRemoteControlCommands::Get().ToggleExposeFunctions)
					.ForceSmallIcons_Static(SRemoteControlPanel::ShouldForceSmallIcons)
					.IconOverride(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GraphEditor.Function_24x")))
				, LOCTEXT("FunctionsLabel", "Functions")
			)
			);
			
			ToolsSection.AddEntry(FToolMenuEntry::InitWidget("Mappings"
				, SNew(SAutoResizeButton)
					.UICommand(FRemoteControlCommands::Get().ToggleProtocolMappings)
					.ForceSmallIcons_Static(SRemoteControlPanel::ShouldForceSmallIcons)
					.IconOverride(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"))
				, LOCTEXT("MappingsLabel", "Mappings")
			)
			);
			
			ToolsSection.AddEntry(FToolMenuEntry::InitWidget("Logic"
				, SNew(SAutoResizeButton)
					.UICommand(FRemoteControlCommands::Get().ToggleLogicEditor)
					.ForceSmallIcons_Static(SRemoteControlPanel::ShouldForceSmallIcons)
					.IconOverride(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GraphEditor.StateMachine_24x")))
				, LOCTEXT("LogicLabel", "Logic")
			)
			);
		}
	}
}

void SRemoteControlPanel::GenerateAuxiliaryToolbar()
{
	RegisterAuxiliaryToolBar();

	AuxiliaryToolbarWidgetContent = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		[
			SNullWidget::NullWidget
		];

	UToolMenus* ToolMenus = UToolMenus::Get();
	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	UToolMenu* GeneratedToolbar = ToolMenus->FindMenu(AuxiliaryRemoteControlPanelToolBarName);

	GeneratedToolbar->Context = FToolMenuContext(MainFrame.GetMainFrameCommandBindings());

	TSharedRef<class SWidget> ToolBarWidget = ToolMenus->GenerateWidget(GeneratedToolbar);

	AuxiliaryToolbar =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ToolBarWidget
		];

	if (AuxiliaryToolbarWidgetContent.IsValid())
	{
		AuxiliaryToolbarWidgetContent->SetContent(AuxiliaryToolbar.ToSharedRef());
	}
}

FText SRemoteControlPanel::HandlePresetName() const
{
	if (Preset)
	{
		return FText::FromString(Preset->GetName());
	}
	
	return FText::GetEmpty();
}

bool SRemoteControlPanel::CanSaveAsset() const
{
	return Preset.IsValid();
}

void SRemoteControlPanel::SaveAsset_Execute() const
{
	if (Preset.IsValid())
	{
		TArray<UPackage*> PackagesToSave;

		if (!Preset->IsAsset())
		{
			// Log an invalid object but don't try to save it
			UE_LOG(LogRemoteControl, Log, TEXT("Invalid object to save: %s"), (Preset.IsValid()) ? *Preset->GetFullName() : TEXT("Null Object"));
		}
		else
		{
			PackagesToSave.Add(Preset->GetOutermost());
		}

		constexpr bool bCheckDirtyOnAssetSave = false;
		constexpr bool bPromptToSave = false;

		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirtyOnAssetSave, bPromptToSave);
	}
}

bool SRemoteControlPanel::CanFindInContentBrowser() const
{
	return Preset.IsValid();
}

void SRemoteControlPanel::FindInContentBrowser_Execute() const
{
	if (Preset.IsValid())
	{
		TArray<UObject*> ObjectsToSyncTo;

		ObjectsToSyncTo.Add(Preset.Get());

		GEditor->SyncBrowserToObjects(ObjectsToSyncTo);
	}
}

bool SRemoteControlPanel::ShouldForceSmallIcons()
{
	// Find the DockTab that houses this RemoteControlPreset widget in it.
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FTabManager> EditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	if (TSharedPtr<SDockTab> Tab = EditorTabManager->FindExistingLiveTab(FRemoteControlUIModule::RemoteControlPanelTabName))
	{
		if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
		{
			const FVector2D& ActualWindowSize = Window->GetSizeInScreen() / Window->GetDPIScaleFactor();

			// Need not to check for less than the minimum value as user can never go beyond that limit while resizing the parent window.
			return ActualWindowSize.X == SRemoteControlPanel::MinimumPanelWidth ? true : false;
		}
	}

	return false;
}

void SRemoteControlPanel::OnToggleExposeFunctions() const
{
}

void SRemoteControlPanel::OnToggleProtocolMappings() const
{
}

void SRemoteControlPanel::OnToggleLogicEditor() const
{
}

TSharedPtr<class SRCLogicPanelBase> SRemoteControlPanel::GetActiveLogicPanel() const
{
	if (ControllerPanel->IsListFocused())
	{
		return ControllerPanel;
	}
	else if (BehaviourPanel->IsListFocused())
	{
		return BehaviourPanel;
	}
	else if (ActionPanel->IsListFocused())
	{
		return ActionPanel;
	}
	
	return nullptr;
}

void SRemoteControlPanel::DeleteEntity_Execute()
{
	// Currently used  as common entry point of Delete UI command for both RC Entity and Logic Items.
	// This could potentially be moved out if the Logic panels are moved to a separate tab.
	
	// ~ Delete Logic Item ~
	// 
	// If the user focus is currently active on a Logic panel then route the Delete command to it and return.
	if (TSharedPtr<class SRCLogicPanelBase> ActivePanel = GetActiveLogicPanel())
	{
		ActivePanel->DeleteSelectedPanelItem();

		return; // handled
	}

	// ~ Delete Entity ~

	if (SelectedEntity->GetRCType() == SRCPanelTreeNode::FieldChild) // Field Child does not contain entity ID, that is why it should not be processed
	{
		return;
	}

	if (SelectedEntity->GetRCType() == SRCPanelTreeNode::Group)
	{
		FScopedTransaction Transaction(LOCTEXT("DeleteGroup", "Delete Group"));
		Preset->Modify();
		Preset->Layout.DeleteGroup(SelectedEntity->GetRCId());
	}
	else
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeFunction", "Unexpose remote control entity"));
		Preset->Modify();
		Preset->Unexpose(SelectedEntity->GetRCId());
	}

	EntityList->Refresh();
}

bool SRemoteControlPanel::CanDeleteEntity() const
{
	if (TSharedPtr<class SRCLogicPanelBase> ActivePanel = GetActiveLogicPanel())
	{
		return true; // User has focus on a logic panel
	}

	if (SelectedEntity.IsValid() && Preset.IsValid() && bIsInEditMode)
	{
		// Do not allow default group to be deleted.
		return !Preset->Layout.IsDefaultGroup(SelectedEntity->GetRCId());
	}

	return false;
}

void SRemoteControlPanel::RenameEntity_Execute() const
{
	if (SelectedEntity->GetRCType() == SRCPanelTreeNode::FieldChild) // Field Child does not contain entity ID, that is why it should not be processed
	{
		return;
	}

	SelectedEntity->EnterRenameMode();
}

bool SRemoteControlPanel::CanRenameEntity() const
{
	if (SelectedEntity.IsValid() && Preset.IsValid() && bIsInEditMode)
	{
		// Do not allow default group to be renamed.
		return !Preset->Layout.IsDefaultGroup(SelectedEntity->GetRCId());
	}

	return false;
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
