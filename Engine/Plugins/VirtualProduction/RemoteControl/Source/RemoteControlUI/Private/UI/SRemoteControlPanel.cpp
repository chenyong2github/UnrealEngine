// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "EditorFontGlyphs.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
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
#include "Subsystems/Subsystem.h"
#include "SWarningOrErrorBox.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Templates/UnrealTypeTraits.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
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

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TSharedPtr<IToolkitHost> InToolkitHost)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	WidgetRegistry = MakeShared<FRCPanelWidgetRegistry>();
	ToolkitHost = InToolkitHost;

	UpdateRebindButtonVisibility();

	TArray<TSharedRef<SWidget>> ExtensionWidgets;
	FRemoteControlUIModule::Get().GetExtensionGenerators().Broadcast(ExtensionWidgets);

	TSharedPtr<SHorizontalBox> TopExtensions;

	EntityProtocolDetails = SNew(SBox);
	
	EntityList = SNew(SRCPanelExposedEntitiesList, Preset.Get(), WidgetRegistry)
		.OnEntityListUpdated_Lambda([this] ()
		{
			UpdateEntityDetailsView(EntityList->GetSelection());
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

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			// Top tool bar
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
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
					.ButtonStyle(FEditorStyle::Get(), "FlatButton")
					.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
					.ToolTipText(LOCTEXT("EntityDetailsToolTip", "Open the details panel for the selected exposed entity."))
					.OnClicked_Lambda([this](){ ToggleDetailsView(); return FReply::Handled(); })
					[
						SNew(SImage)
						.Image(FEditorStyle::Get().GetBrush("LevelEditor.Tabs.Details"))
					]
				]
				// Edit Mode
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditModeLabel", "Edit Mode : "))
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
		]
	];

	for (const TSharedRef<SWidget>& Widget : ExtensionWidgets)
	{
		// We want to insert the widgets before the edit mode buttons.
		constexpr int32 NumEditModeWidgets = 2;
		const int32 ExtensionsPosition = ExtensionWidgets.Num() - NumEditModeWidgets;
		TopExtensions->InsertSlot(ExtensionsPosition)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				Widget
			];
	}

	RegisterEvents();
	CacheLevelClasses();
	Refresh();
}

SRemoteControlPanel::~SRemoteControlPanel()
{
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
	if (TSharedPtr<SRCPanelTreeNode> Node = EntityList->GetSelection())
	{
		if (Node->GetRCType() == SRCPanelTreeNode::Group)
		{
			return Node->GetRCId();		
		}
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

	UpdateEntityDetailsView(EntityList->GetSelection());
	if (ensure(EntityDetailsView && EntityDetailsView->GetWidget()))
	{
		return EntityDetailsView->GetWidget().ToSharedRef();
	}

	return SNullWidget::NullWidget;
}

void SRemoteControlPanel::UpdateEntityDetailsView(const TSharedPtr<SRCPanelTreeNode>& SelectedNode)
{
	TSharedPtr<FStructOnScope> SelectedEntityPtr;

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



#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
