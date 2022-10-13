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
#include "VirtualCameraActor.h"
#include "LevelEditor.h"
#include "LevelEditorOutlinerSettings.h"
#include "Filters/CustomClassFilterData.h"



#define LOCTEXT_NAMESPACE "FVirtualCameraEditorModule"

class FVirtualCameraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterSettings();
		RegisterPlacementModeItems();
		RegisterOutlinerFilters();
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
				FAssetData VirtualCamera2ActorAssetData(
					TEXT("/VirtualCamera/VCamActor"),
					TEXT("/VirtualCamera"),
					TEXT("VCamActor"),
					FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"))
				);

				// register the full-fat camera
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*UActorFactoryBlueprint::StaticClass(),
					VirtualCamera2ActorAssetData,
					FName("ClassThumbnail.CameraActor"),
					FName("ClassIcon.CameraActor"),
					TOptional<FLinearColor>(),
					TOptional<int32>(),
					NSLOCTEXT("PlacementMode", "VCam Actor", "VCam Actor")
				));
			}
		}
	}

	void RegisterOutlinerFilters()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		
		if(TSharedPtr<FFilterCategory> VPFilterCategory = LevelEditorModule.GetOutlinerFilterCategory(FLevelEditorOutlinerBuiltInCategories::VirtualProduction()))
		{
			TSharedRef<FCustomClassFilterData> CineCameraActorClassData = MakeShared<FCustomClassFilterData>(ACineCameraActor::StaticClass(), VPFilterCategory, FLinearColor::White);
			LevelEditorModule.AddCustomClassFilterToOutliner(CineCameraActorClassData);
		}
	}
};
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)


