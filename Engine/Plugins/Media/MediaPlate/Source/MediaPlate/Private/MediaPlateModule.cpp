// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateModule.h"

#include "MediaPlate.h"
#include "MediaPlateComponent.h"

DEFINE_LOG_CATEGORY(LogMediaPlate);

UClass* FMediaPlateModule::GetAMediaPlateClass()
{
	return AMediaPlate::StaticClass();
}

UMediaPlayer* FMediaPlateModule::GetMediaPlayer(UObject* Object)
{
	UMediaPlayer* MediaPlayer = nullptr;

	AMediaPlate* MediaPlate = Cast<AMediaPlate>(Object);
	if (MediaPlate != nullptr)
	{
		UMediaPlateComponent* MediaPlateComponent = MediaPlate->MediaPlateComponent;
		if (MediaPlateComponent != nullptr)
		{
			MediaPlayer = MediaPlateComponent->GetMediaPlayer();
		}
	}

	return MediaPlayer;
}

void FMediaPlateModule::StartupModule()
{
}

void FMediaPlateModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FMediaPlateModule, MediaPlate)
