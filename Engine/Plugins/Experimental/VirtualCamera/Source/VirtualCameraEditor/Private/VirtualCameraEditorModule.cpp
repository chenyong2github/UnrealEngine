// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraEditorModule.h"
#include "VirtualCameraEditorStyle.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "VirtualCameraTab.h"
#include "VirtualCameraUserSettings.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "IPlacementModeModule.h"
#include "IVPUtilitiesEditorModule.h"
#include "VirtualCameraActor.h"
#include "ActorFactories/ActorFactoryBlueprint.h"


#define LOCTEXT_NAMESPACE "FVirtualCameraEditorModule"


class FVirtualCameraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FVirtualCameraEditorStyle::Register();
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> DeveloperToolsGroup = MenuStructure.GetDeveloperToolsMiscCategory();
		SVirtualCameraTab::RegisterNomadTabSpawner(DeveloperToolsGroup);

		RegisterSettings();
		RegisterPlacementModeItems();
	}

	virtual void ShutdownModule() override
	{
		UnregisterSettings();

		if (!IsEngineExitRequested() && UObjectInitialized())
		{
			FVirtualCameraEditorStyle::Unregister();
			SVirtualCameraTab::UnregisterNomadTabSpawner();
		}
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
				FAssetData VCamActorAssetData(
					TEXT("/VirtualCamera/V2/VcamActor"), 
					TEXT("/VirtualCamera/V2"), 
					TEXT("VcamActor"), 
					TEXT("Blueprint")
				);
				
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*UActorFactoryBlueprint::StaticClass(),
					VCamActorAssetData,
					FName("ClassIcon.CameraActor"),
					TOptional<FLinearColor>(),
					TOptional<int32>(),
					LOCTEXT("VCamActorPlacementName", "VCam Actor")
				));
			}
		}
	}
};

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)

#undef LOCTEXT_NAMESPACE
