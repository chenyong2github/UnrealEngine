// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "GameFeatureData.h"
#include "GameFeaturesSubsystemSettings.h"
#include "Features/IPluginsEditorFeature.h"
#include "Features/EditorFeatures.h"

#include "Interfaces/IPluginManager.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "GameFeatureDataDetailsCustomization.h"
#include "Logging/MessageLog.h"
#include "SSettingsEditorCheckoutNotice.h"
#include "Engine/AssetManagerSettings.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Engine/AssetManager.h"
#include "HAL/FileManager.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////

struct FGameFeaturePluginTemplateDescription : public FPluginTemplateDescription
{
	FGameFeaturePluginTemplateDescription(FText InName, FText InDescription, FString InOnDiskPath)
		: FPluginTemplateDescription(InName, InDescription, InOnDiskPath, /*bCanContainContent=*/ true, EHostType::Runtime)
	{
		SortPriority = 10;
		bCanBePlacedInEngine = false;
	}

	virtual bool ValidatePathForPlugin(const FString& ProposedAbsolutePluginPath, FText& OutErrorMessage) override
	{
		if (!IsRootedInGameFeaturesRoot(ProposedAbsolutePluginPath))
		{
			OutErrorMessage = LOCTEXT("InvalidPathForGameFeaturePlugin", "Game features must be inside the Plugins/GameFeatures folder");
			return false;
		}

		OutErrorMessage = FText::GetEmpty();
		return true;
	}

	virtual void UpdatePathWhenTemplateSelected(FString& InOutPath) override
	{
		if (!IsRootedInGameFeaturesRoot(InOutPath))
		{
			InOutPath = GetGameFeatureRoot();
		}
	}

	virtual void UpdatePathWhenTemplateUnselected(FString& InOutPath) override
	{
		InOutPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*FPaths::ProjectPluginsDir());
		FPaths::MakePlatformFilename(InOutPath);
	}

	virtual void OnPluginCreated(TSharedPtr<IPlugin> NewPlugin) override
	{
		TSubclassOf<UGameFeatureData> DefaultGameFeatureDataClass = GetDefault<UGameFeaturesSubsystemSettings>()->DefaultGameFeatureDataClass;
		if (DefaultGameFeatureDataClass == nullptr)
		{
			DefaultGameFeatureDataClass = UGameFeatureData::StaticClass();
		}

		// Create the game feature data asset
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(NewPlugin->GetName(), NewPlugin->GetMountedAssetPath(), DefaultGameFeatureDataClass, /*Factory=*/ nullptr);

		// Activate the new game feature plugin
		auto AdditionalFilter = [](const FString&, const FGameFeaturePluginDetails&, FBuiltInGameFeaturePluginBehaviorOptions&) -> bool { return true; };
		UGameFeaturesSubsystem::Get().LoadBuiltInGameFeaturePlugin(NewPlugin.ToSharedRef(), AdditionalFilter);

		// Edit the new game feature data
		if (NewAsset != nullptr)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
		}
	}

	FString GetGameFeatureRoot() const
	{
		FString Result = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(FPaths::ProjectPluginsDir() / TEXT("GameFeatures/")));
		FPaths::MakePlatformFilename(Result);
		return Result;
	}

	bool IsRootedInGameFeaturesRoot(const FString& InStr)
	{
		const FString DesiredRoot = GetGameFeatureRoot();

		FString TestStr = InStr / TEXT("");
		FPaths::MakePlatformFilename(TestStr);

		return TestStr.StartsWith(DesiredRoot);
	}
};

//////////////////////////////////////////////////////////////////////

class FGameFeaturesEditorModule : public FDefaultModuleImpl
{
	virtual void StartupModule() override
	{
		// Register the details customizations
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.RegisterCustomClassLayout(UGameFeatureData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FGameFeatureDataDetailsCustomization::MakeInstance));

			PropertyModule.NotifyCustomizationModuleChanged();
		}

		// Register to get a warning on startup if settings aren't configured correctly
		UAssetManager::CallOrRegister_OnAssetManagerCreated(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGameFeaturesEditorModule::OnAssetManagerCreated));

		// Add templates to the new plugin wizard
		{
			const FString PluginTemplateDir = IPluginManager::Get().FindPlugin(TEXT("GameFeatures"))->GetBaseDir() / TEXT("Templates");

			ContentOnlyTemplate = MakeShareable(new FGameFeaturePluginTemplateDescription(
				LOCTEXT("PluginWizard_NewGFPContentOnlyLabel", "Game Feature (Content Only)"),
				LOCTEXT("PluginWizard_NewGFPContentOnlyDesc", "Create a new Game Feature Plugin."),
				PluginTemplateDir / TEXT("GameFeaturePluginContentOnly")
			));

			IModularFeatures& ModularFeatures = IModularFeatures::Get();
			ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FGameFeaturesEditorModule::OnModularFeatureRegistered);
			ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FGameFeaturesEditorModule::OnModularFeatureUnregistered);

			if (ModularFeatures.IsModularFeatureAvailable(EditorFeatures::PluginsEditor))
			{
				IPluginsEditorFeature& PluginsEditor = ModularFeatures.GetModularFeature<IPluginsEditorFeature>(EditorFeatures::PluginsEditor);
				PluginsEditor.RegisterPluginTemplate(ContentOnlyTemplate.ToSharedRef());
			}
		}
	}

	void OnModularFeatureRegistered(const FName& Type, class IModularFeature* ModularFeature)
	{
		if (Type == EditorFeatures::PluginsEditor)
		{
			static_cast<IPluginsEditorFeature*>(ModularFeature)->RegisterPluginTemplate(ContentOnlyTemplate.ToSharedRef());
		}
	}

	void OnModularFeatureUnregistered(const FName& Type, class IModularFeature* ModularFeature)
	{
		if (Type == EditorFeatures::PluginsEditor)
		{
			static_cast<IPluginsEditorFeature*>(ModularFeature)->UnregisterPluginTemplate(ContentOnlyTemplate.ToSharedRef());
		}
	}

	virtual void ShutdownModule() override
	{
		// Remove the customization
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.UnregisterCustomClassLayout(UGameFeatureData::StaticClass()->GetFName());

			PropertyModule.NotifyCustomizationModuleChanged();
		}

		// Remove the plugin wizard override
		{
			IModularFeatures& ModularFeatures = IModularFeatures::Get();
 			ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
 			ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);

			if (ModularFeatures.IsModularFeatureAvailable(EditorFeatures::PluginsEditor))
			{
				IPluginsEditorFeature& PluginsEditor = ModularFeatures.GetModularFeature<IPluginsEditorFeature>(EditorFeatures::PluginsEditor);
				PluginsEditor.UnregisterPluginTemplate(ContentOnlyTemplate.ToSharedRef());
			}
			ContentOnlyTemplate.Reset();
		}
	}

	void AddDefaultGameDataRule()
	{
		// Check out the ini or make it writable
		UAssetManagerSettings* Settings = GetMutableDefault<UAssetManagerSettings>();

		const FString& ConfigFileName = Settings->GetDefaultConfigFilename();

		bool bSuccess = false;

		FText NotificationOpText;
 		if (!SettingsHelpers::IsCheckedOut(ConfigFileName, true))
 		{
			FText ErrorMessage;
			bSuccess = SettingsHelpers::CheckOutOrAddFile(ConfigFileName, true, !IsRunningCommandlet(), &ErrorMessage);
			if (bSuccess)
			{
				NotificationOpText = LOCTEXT("CheckedOutAssetManagerIni", "Checked out {0}");
			}
			else
			{
				UE_LOG(LogGameFeatures, Error, TEXT("%s"), *ErrorMessage.ToString());
				bSuccess = SettingsHelpers::MakeWritable(ConfigFileName);

				if (bSuccess)
				{
					NotificationOpText = LOCTEXT("MadeWritableAssetManagerIni", "Made {0} writable (you may need to manually add to source control)");
				}
				else
				{
					NotificationOpText = LOCTEXT("FailedToTouchAssetManagerIni", "Failed to check out {0} or make it writable, so no rule was added");
				}
			}
		}
		else
		{
			NotificationOpText = LOCTEXT("UpdatedAssetManagerIni", "Updated {0}");
			bSuccess = true;
		}

		// Add the rule to project settings
		if (bSuccess)
		{
			FDirectoryPath DummyPath;
			DummyPath.Path = TEXT("/Game/Unused");

			FPrimaryAssetTypeInfo NewTypeInfo(
				UGameFeatureData::StaticClass()->GetFName(),
				UGameFeatureData::StaticClass(),
				false,
				false,
				{ DummyPath },
				{});
			NewTypeInfo.Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;

			Settings->Modify(true);

			Settings->PrimaryAssetTypesToScan.Add(NewTypeInfo);

 			Settings->PostEditChange();
			Settings->UpdateDefaultConfigFile();

			UAssetManager::Get().ReinitializeFromConfig();
		}

		// Show a message that the file was checked out/updated and must be submitted
		FNotificationInfo Info(FText::Format(NotificationOpText, FText::FromString(FPaths::GetCleanFilename(ConfigFileName))));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	void OnAssetManagerCreated()
	{
		// Make sure the game has the appropriate asset manager configuration or we won't be able to load game feature data assets
		FPrimaryAssetId DummyGameFeatureDataAssetId(UGameFeatureData::StaticClass()->GetFName(), NAME_None);
		FPrimaryAssetRules GameDataRules = UAssetManager::Get().GetPrimaryAssetRules(DummyGameFeatureDataAssetId);
		if (GameDataRules.IsDefault())
		{
			FMessageLog("LoadErrors").Error()
				->AddToken(FTextToken::Create(FText::Format(NSLOCTEXT("GameFeatures", "MissingRuleForGameFeatureData", "Asset Manager settings do not include an entry for assets of type {0}, which is required for game feature plugins to function."), FText::FromName(UGameFeatureData::StaticClass()->GetFName()))))
				->AddToken(FActionToken::Create(NSLOCTEXT("GameFeatures", "AddRuleForGameFeatureData", "Add entry to PrimaryAssetTypesToScan?"), FText(),
					FOnActionTokenExecuted::CreateRaw(this, &FGameFeaturesEditorModule::AddDefaultGameDataRule), true));
		}
	}

private:
	TSharedPtr<FPluginTemplateDescription> ContentOnlyTemplate;
};

IMPLEMENT_MODULE(FGameFeaturesEditorModule, GameFeaturesEditor)

#undef LOCTEXT_NAMESPACE
