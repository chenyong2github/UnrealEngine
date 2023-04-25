// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFactories/ActorFactoryBlueprint.h"
#include "CineCameraActor.h"
#include "Filters/CustomClassFilterData.h"
#include "Interfaces/IPluginManager.h"
#include "IPlacementModeModule.h"
#include "IVPUtilitiesEditorModule.h"
#include "LevelEditor.h"
#include "LevelEditorOutlinerSettings.h"
#include "Modules/ModuleManager.h"
#include "VirtualCameraActor.h"

#define LOCTEXT_NAMESPACE "FVirtualCameraMacPresetEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualCameraPixelStreamingPresetEditor, Log, All);

class FVirtualCameraMacPresetEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterPlacementModeItems();
		RegisterOutlinerFilters();
	}

	virtual void ShutdownModule() override
	{
	}

	void RegisterPlacementModeItems()
	{
		if (const FPlacementCategoryInfo* Info = IVPUtilitiesEditorModule::Get().GetVirtualProductionPlacementCategoryInfo()
			; Info && GEditor)
		{
			FAssetData VirtualCamera2ActorAssetData(
				TEXT("/VCamMacPreset/VCamActor"),
				TEXT("/VCamMacPreset"),
				TEXT("VCamActor"),
				FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"))
			);

			// Makes it appear in the VP category ...
			IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
				*UActorFactoryBlueprint::StaticClass(),
				VirtualCamera2ActorAssetData,
				FName("ClassThumbnail.CameraActor"),
				FName("ClassIcon.CameraActor"),
				TOptional<FLinearColor>(),
				TOptional<int32>(),
				LOCTEXT("VCam Actor", "VCam Actor")
				)
			);

			// ... but if you search for it by text this is needed to make it show up (without having the user load it manually).
			// The search filters everything in the FBuiltInPlacementCategories::AllClasses category only;
			// it contains 1. loaded BP classes and 2. specialized actor factories. This manual load adds it to case 1. 
			const UClass* VCamActorBlueprintClass = LoadClass<UObject>(nullptr, TEXT("/VCamMacPreset/VCamActor.VCamActor_C"));
			UE_CLOG(VCamActorBlueprintClass == nullptr, LogVirtualCameraPixelStreamingPresetEditor, Warning, TEXT("Failed to load '/VCamMacPreset/VCamActor.VCamActor_C'. Has the Blueprint been moved?"));
		}
	}

	void RegisterOutlinerFilters()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		if (const TSharedPtr<FFilterCategory> VPFilterCategory = LevelEditorModule.GetOutlinerFilterCategory(FLevelEditorOutlinerBuiltInCategories::VirtualProduction()))
		{
			TSharedRef<FCustomClassFilterData> CineCameraActorClassData = MakeShared<FCustomClassFilterData>(ACineCameraActor::StaticClass(), VPFilterCategory, FLinearColor::White);
			LevelEditorModule.AddCustomClassFilterToOutliner(CineCameraActorClassData);
		}
	}
};
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualCameraMacPresetEditorModule, VCamMacPresetEditor)


