// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateModule.h"

#include "MediaPlate.h"

DEFINE_LOG_CATEGORY(LogMediaPlate);

UClass* FMediaPlateModule::GetAMediaPlateClass()
{
	return AMediaPlate::StaticClass();
}

void FMediaPlateModule::StartupModule()
{
}

void FMediaPlateModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FMediaPlateModule, MediaPlate)
