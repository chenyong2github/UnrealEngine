// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "TimeSynthVolumeGroup.h"

#include "TimeSynthComponent.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_TimeSynthVolumeGroup::GetSupportedClass() const
{
	return UTimeSynthVolumeGroup::StaticClass();
}

const TArray<FText>& FAssetTypeActions_TimeSynthVolumeGroup::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundSynthesisSubMenu", "Synthesis"))
	};

	return SubMenus;
}

UTimeSynthVolumeGroupFactory::UTimeSynthVolumeGroupFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTimeSynthVolumeGroup::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UTimeSynthVolumeGroupFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UTimeSynthVolumeGroup* NewTimeSynthVolumeGroup = NewObject<UTimeSynthVolumeGroup>(InParent, InName, Flags);

	return NewTimeSynthVolumeGroup;
}

#undef LOCTEXT_NAMESPACE