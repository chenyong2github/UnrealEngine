// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraEditorModule.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "VirtualCameraUserSettings.h"
#include "IPlacementModeModule.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "IVPUtilitiesEditorModule.h"
#include "SimpleVirtualCamera.h"


#define LOCTEXT_NAMESPACE "FVirtualCameraEditorModule"

class FVirtualCameraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterSettings();
		RegisterPlacementModeItems();
	}

	virtual void ShutdownModule() override
	{
		UnregisterSettings();
	}

	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualCamera",
				LOCTEXT("VirtualCameraUserSettingsName", "Virtual Camera"),
				LOCTEXT("VirtualCameraUserSettingsDescription", "Configure the Virtual Camera settings."),
				GetMutableDefault<UVirtualCameraUserSettings>());
		}
	}

	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualCamera");
		}
	}

	void RegisterPlacementModeItems()
	{
		if (GEditor)
		{
			if (const FPlacementCategoryInfo* Info = IVPUtilitiesEditorModule::Get().GetVirtualProductionPlacementCategoryInfo())
			{
				FAssetData SimpleVirtualCameraAssetData(
					TEXT("/VirtualCamera/VCamCore/Blueprints/SimpleVirtualCamera"),
					TEXT("/VirtualCamera/VCamCore/Blueprints"),
					TEXT("SimpleVirtualCamera"),
					TEXT("Blueprint")
				);

				// register the simple virtual camera
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*UActorFactoryBlueprint::StaticClass(),
					SimpleVirtualCameraAssetData,
					FName("ClassIcon.CameraActor"),
					TOptional<FLinearColor>(),
					TOptional<int32>(),
					NSLOCTEXT("PlacementMode", "Simple Virtual Camera", "Simple Virtual Camera")
					));

				FAssetData VirtualCamera2ActorAssetData(
					TEXT("/VirtualCamera/V2/VirtualCamera2Actor"),
					TEXT("/VirtualCamera/V2"),
					TEXT("VirtualCamera2Actor"),
					TEXT("Blueprint")
				);

				// register the full-fat camera
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*UActorFactoryBlueprint::StaticClass(),
					VirtualCamera2ActorAssetData,
					FName("ClassIcon.CameraActor"),
					TOptional<FLinearColor>(),
					TOptional<int32>(),
					NSLOCTEXT("PlacementMode", "VirtualCamera2 Actor", "VirtualCamera2 Actor")
				));
			}
		}
	}
};
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)


