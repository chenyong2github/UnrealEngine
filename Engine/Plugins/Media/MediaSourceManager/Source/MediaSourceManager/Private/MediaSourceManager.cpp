// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManager.h"

#include "MediaPlateModule.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"

#define LOCTEXT_NAMESPACE "MediaSourceManager"

UMediaSourceManager::UMediaSourceManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FMediaPlateModule* MediaPlateModule = FModuleManager::LoadModulePtr<FMediaPlateModule>("MediaPlate");
		if (MediaPlateModule != nullptr)
		{
			MediaPlateModule->OnMediaPlateApplyMaterial.AddUObject(this,
				&UMediaSourceManager::OnMediaPlateApplyMaterial);
		}
	}
}

void UMediaSourceManager::BeginDestroy()
{
	FMediaPlateModule* MediaPlateModule = FModuleManager::GetModulePtr<FMediaPlateModule>("MediaPlate");
	if (MediaPlateModule != nullptr)
	{
		MediaPlateModule->OnMediaPlateApplyMaterial.RemoveAll(this);
	}

	Super::BeginDestroy();
}

void UMediaSourceManager::Validate()
{
	// Check we have some channels.
	int32 NumChannels = 8;

	while (Channels.Num() < NumChannels)
	{
		UMediaSourceManagerChannel* Channel = NewObject<UMediaSourceManagerChannel>(this);
		Channel->Name = FString::FromInt(Channels.Num());
		Channels.Add(Channel);
	}

	// Validate each channel.
	for (UMediaSourceManagerChannel* Channel : Channels)
	{
		if (Channel != nullptr)
		{
			Channel->Validate();
		}
	}
}

void UMediaSourceManager::OnMediaPlateApplyMaterial(UMaterialInterface* Material,
	AMediaPlate* MediaPlate, bool& bCanModify)
{
	// Is this material one of ours?
	for (UMediaSourceManagerChannel* Channel : Channels)
	{
		if (Channel->Material.Get() == Material)
		{
			bCanModify = false;
			break;
		}
	}
}


#undef LOCTEXT_NAMESPACE
