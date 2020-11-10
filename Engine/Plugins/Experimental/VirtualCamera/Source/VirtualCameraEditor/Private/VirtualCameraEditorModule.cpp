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
#include "SimpleVirtualCamera.h"


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
				// register VirtualCameraLite
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*ASimpleVirtualCamera::StaticClass(),
					FAssetData(ASimpleVirtualCamera::StaticClass()),
					FName("ClassIcon.CameraActor")
				));


				FAssetData VirtualCamera2ActorAssetData(
					TEXT("/VirtualCamera/V2/VirtualCamera2Actor"), 
					TEXT("/VirtualCamera/V2"), 
					TEXT("VirtualCamera2Actor"), 
					TEXT("Blueprint")
				);
				
				IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
					*UActorFactoryBlueprint::StaticClass(),
					VirtualCamera2ActorAssetData,
					FName("ClassIcon.CameraActor"),
					TOptional<FLinearColor>(),
					TOptional<int32>(),
					LOCTEXT("VCamActorPlacementName", "VirtualCamera2 Actor")
				));
			}
		}
	}
};

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)

#undef LOCTEXT_NAMESPACE
