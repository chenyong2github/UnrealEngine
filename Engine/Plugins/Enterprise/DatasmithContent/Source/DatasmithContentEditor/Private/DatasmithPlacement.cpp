// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithPlacement.h"
#include "IPlacementModeModule.h"
#include "ActorFactories/ActorFactoryBlueprint.h"

#define LOCTEXT_NAMESPACE "DatasmithContentEditorModule"

void FDatasmithPlacement::RegisterPlacement()
{	
	UBlueprint* HDRIBackdrop = Cast<UBlueprint>(FSoftObjectPath(TEXT("/DatasmithContent/Datasmith/HDRIBackdrop.HDRIBackdrop")).TryLoad());
	if (HDRIBackdrop == nullptr)
	{
		return;
	}

	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
	FPlacementCategoryInfo Info = *PlacementModeModule.GetRegisteredPlacementCategory(FBuiltInPlacementCategories::Lights());

	FPlaceableItem* BPPlacement = new FPlaceableItem(
		*UActorFactoryBlueprint::StaticClass(),
		FAssetData(HDRIBackdrop, true),
		FName("DatasmithPlacement.HDRIBackdrop"),
		TOptional<FLinearColor>(),
		TOptional<int32>(),
		NSLOCTEXT("PlacementMode", "HDRI Backdrop", "HDRI Backdrop")
	);

	IPlacementModeModule::Get().RegisterPlaceableItem( Info.UniqueHandle, MakeShareable(BPPlacement) );
}

#undef LOCTEXT_NAMESPACE