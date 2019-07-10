// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Interfaces/IPluginManager.h"

#include "Brushes/SlateBoxBrush.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "LiveLinkClient.h"
#include "LiveLinkClientPanel.h"
#include "LiveLinkClientCommands.h"
#include "LiveLinkEditorPrivate.h"
#include "LiveLinkGraphPanelPinFactory.h"
#include "LiveLinkSettings.h"
#include "LiveLinkSubjectNameDetailCustomization.h"
#include "LiveLinkSubjectRepresentationDetailCustomization.h"
#include "LiveLinkTypes.h"
#include "LiveLinkVirtualSubject.h"
#include "LiveLinkVirtualSubjectDetailCustomization.h"

/**
 * Implements the LiveLinkEditor module.
 */


DEFINE_LOG_CATEGORY(LogLiveLinkEditor);


#define LOCTEXT_NAMESPACE "LiveLinkModule"

static const FName LiveLinkClientTabName(TEXT("LiveLink"));
static const FName LevelEditorModuleName(TEXT("LevelEditor"));


namespace LiveLinkEditorModuleUtils
{
	FString InPluginContent(const FString& RelativePath, const ANSICHAR* Extension)
	{
		static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("LiveLink"))->GetContentDir();
		return (ContentDir / RelativePath) + Extension;
	}
}

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( LiveLinkEditorModuleUtils::InPluginContent( RelativePath, ".png" ), __VA_ARGS__ )


class FLiveLinkEditorModule : public IModuleInterface
{
public:
	static TSharedPtr<FSlateStyleSet> StyleSet;

	// IModuleInterface interface

	virtual void StartupModule() override
	{
		static FName LiveLinkStyle(TEXT("LiveLinkStyle"));
		StyleSet = MakeShared<FSlateStyleSet>(LiveLinkStyle);

		bHasRegisteredTabSpawners = false;

		if (FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
		{
			RegisterTabSpawner();
		}

		ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FLiveLinkEditorModule::ModulesChangesCallback);

		FLiveLinkClientCommands::Register();

		const FVector2D Icon8x8(8.0f, 8.0f);
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);

		StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		StyleSet->Set("LiveLinkClient.Common.Icon", new IMAGE_PLUGIN_BRUSH(TEXT("LiveLink_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.Icon.Small", new IMAGE_PLUGIN_BRUSH(TEXT("LiveLink_16x"), Icon16x16));

		StyleSet->Set("ClassIcon.LiveLinkPreset", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkFrameInterpolationProcessor", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkFramePreProcessor", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkFrameTranslator", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkPreset", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkRole", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));
		StyleSet->Set("ClassIcon.LiveLinkVirtualSubject", new IMAGE_PLUGIN_BRUSH("LiveLink_16x", Icon16x16));

		StyleSet->Set("ClassThumbnail.LiveLinkPreset", new IMAGE_PLUGIN_BRUSH("LiveLink_40x", Icon40x40));

		StyleSet->Set("LiveLinkClient.Common.AddSource", new IMAGE_PLUGIN_BRUSH(TEXT("icon_AddSource_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.RemoveSource", new IMAGE_PLUGIN_BRUSH(TEXT("icon_RemoveSource_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.RemoveAllSources", new IMAGE_PLUGIN_BRUSH(TEXT("icon_RemoveSource_40x"), Icon40x40));

		FButtonStyle Button = FButtonStyle()
			.SetNormal(FSlateBoxBrush(StyleSet->RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.15f)))
			.SetHovered(FSlateBoxBrush(StyleSet->RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.25f)))
			.SetPressed(FSlateBoxBrush(StyleSet->RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.30f)))
			.SetNormalPadding(FMargin(0, 0, 0, 1))
			.SetPressedPadding(FMargin(0, 1, 0, 0));
		FComboButtonStyle ComboButton = FComboButtonStyle()
			.SetButtonStyle(Button.SetNormal(FSlateNoResource()))
			.SetDownArrowImage(FSlateImageBrush(StyleSet->RootToCoreContentDir(TEXT("Common/ComboArrow.png")), Icon8x8))
			.SetMenuBorderBrush(FSlateBoxBrush(StyleSet->RootToCoreContentDir(TEXT("Old/Menu_Background.png")), FMargin(8.0f / 64.0f)))
			.SetMenuBorderPadding(FMargin(0.0f));
		StyleSet->Set("ComboButton", ComboButton);

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

		RegisterSettings();
		RegisterCustomizations();
	}

	void ModulesChangesCallback(FName ModuleName, EModuleChangeReason ReasonForChange)
	{
		if (ReasonForChange == EModuleChangeReason::ModuleLoaded && ModuleName == LevelEditorModuleName)
		{
			RegisterTabSpawner();
		}
	}

	virtual void ShutdownModule() override
	{
		UnregisterCustomizations();

		UnregisterTabSpawner();

		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);

		if (LevelEditorTabManagerChangedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
			LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		StyleSet.Reset();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	static TSharedRef<SDockTab> SpawnLiveLinkTab(const FSpawnTabArgs& SpawnTabArgs, TSharedPtr<FSlateStyleSet> InStyleSet)
	{
		FLiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<FLiveLinkClient>(FLiveLinkClient::ModularFeatureName);

		const FSlateBrush* IconBrush = InStyleSet->GetBrush("LiveLinkClient.Common.Icon.Small");

		const TSharedRef<SDockTab> MajorTab =
			SNew(SDockTab)
			.Icon(IconBrush)
			.TabRole(ETabRole::NomadTab);

		MajorTab->SetContent(SNew(SLiveLinkClientPanel, Client));

		return MajorTab;
	}

private:

	void RegisterTabSpawner()
	{
		if (bHasRegisteredTabSpawners)
		{
			UnregisterTabSpawner();
		}

		FTabSpawnerEntry& SpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LiveLinkClientTabName, FOnSpawnTab::CreateStatic(&FLiveLinkEditorModule::SpawnLiveLinkTab, StyleSet))
			.SetDisplayName(LOCTEXT("LiveLinkTabTitle", "Live Link"))
			.SetTooltipText(LOCTEXT("LiveLinkTabTooltipText", "Open the Live Link streaming manager tab."))
			.SetIcon(FSlateIcon(StyleSet->GetStyleSetName(), "LiveLinkClient.Common.Icon.Small"));

		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		SpawnerEntry.SetGroup(MenuStructure.GetLevelEditorCategory());

		bHasRegisteredTabSpawners = true;
	}

	void UnregisterTabSpawner()
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LiveLinkClientTabName);
		bHasRegisteredTabSpawners = false;
	}

	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "LiveLink",
				LOCTEXT("LiveLinkSettingsName", "Live Link"),
				LOCTEXT("LiveLinkDescription", "Configure the Live Link plugin."),
				GetMutableDefault<ULiveLinkSettings>()
			);
		}
	}

	void UnregisterSettings()
	{
		// unregister settings
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Media", "MediaProfile");
		}
	}

	void RegisterCustomizations()
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.RegisterCustomClassLayout(ULiveLinkVirtualSubject::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLiveLinkVirtualSubjectDetailCustomization::MakeInstance));
		PropertyEditorModule.RegisterCustomPropertyTypeLayout(FLiveLinkSubjectRepresentation::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLiveLinkSubjectRepresentationDetailCustomization::MakeInstance));
		PropertyEditorModule.RegisterCustomPropertyTypeLayout(FLiveLinkSubjectName::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLiveLinkSubjectNameDetailCustomization::MakeInstance));

		LiveLinkGraphPanelPinFactory = MakeShared<FLiveLinkGraphPanelPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(LiveLinkGraphPanelPinFactory);
	}

	void UnregisterCustomizations()
	{
		if (UObjectInitialized() && !GIsRequestingExit)
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(LiveLinkGraphPanelPinFactory);
			FPropertyEditorModule* PropertyEditorModule = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor");
			if (PropertyEditorModule)
			{
				PropertyEditorModule->UnregisterCustomPropertyTypeLayout(FLiveLinkSubjectName::StaticStruct()->GetFName());
				PropertyEditorModule->UnregisterCustomPropertyTypeLayout(FLiveLinkSubjectRepresentation::StaticStruct()->GetFName());
				PropertyEditorModule->UnregisterCustomClassLayout(ULiveLinkVirtualSubject::StaticClass()->GetFName());
			}
		}
	}

private:
	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle ModulesChangedHandle;

	TSharedPtr<FLiveLinkGraphPanelPinFactory> LiveLinkGraphPanelPinFactory;

	// Track if we have registered
	bool bHasRegisteredTabSpawners;
};

TSharedPtr<FSlateStyleSet> FLiveLinkEditorModule::StyleSet;

IMPLEMENT_MODULE(FLiveLinkEditorModule, LiveLinkEditor);


TSharedPtr< class ISlateStyle > FLiveLinkEditorPrivate::GetStyleSet()
{
	return FLiveLinkEditorModule::StyleSet;
}

#undef LOCTEXT_NAMESPACE
