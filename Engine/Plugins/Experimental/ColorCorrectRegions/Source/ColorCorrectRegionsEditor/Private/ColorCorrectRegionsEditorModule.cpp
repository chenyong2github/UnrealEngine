// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionsEditorModule.h"
#include "Editor/PlacementMode/Public/IPlacementModeModule.h"
#include "ColorCorrectRegion.h"
#include "ColorCorrectRegionsStyle.h"
#include "ActorFactories/ActorFactoryBlueprint.h"

#define LOCTEXT_NAMESPACE "FColorCorrectRegionsModule"

void FColorCorrectRegionsEditorModule::StartupModule()
{
	IPlacementModeModule::Get().OnPlacementModeCategoryRefreshed().AddRaw(this, &FColorCorrectRegionsEditorModule::OnPlacementModeRefresh);
	FColorCorrectRegionsStyle::Initialize();
}

void FColorCorrectRegionsEditorModule::OnPlacementModeRefresh(FName CategoryName)
{
	static FName VolumeName = FName(TEXT("Volumes"));
	static FName AllClasses = FName(TEXT("AllClasses"));

	if (CategoryName == VolumeName || CategoryName == AllClasses)
	{
		IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
		UBlueprint* CCRBlueprint = Cast<UBlueprint>(FSoftObjectPath(TEXT("/ColorCorrectRegions/Blueprints/ColorCorrectRegion.ColorCorrectRegion")).TryLoad());
		
		FPlaceableItem* CCRPlaceableItem = new FPlaceableItem(
			*UActorFactoryBlueprint::StaticClass(),
			FAssetData(CCRBlueprint, true),
			FName("CCR.PlaceActorIcon"),
			TOptional<FLinearColor>(),
			TOptional<int32>(),
			NSLOCTEXT("PlacementMode", "Color Correct Region", "Color Correct Region")
		);

		PlacementModeModule.RegisterPlaceableItem(CategoryName, MakeShareable(CCRPlaceableItem));
	}
}

void FColorCorrectRegionsEditorModule::ShutdownModule()
{
	FColorCorrectRegionsStyle::Shutdown();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FColorCorrectRegionsEditorModule, ColorCorrectRegionsEditor);
DEFINE_LOG_CATEGORY(ColorCorrectRegionsEditorLogOutput);
