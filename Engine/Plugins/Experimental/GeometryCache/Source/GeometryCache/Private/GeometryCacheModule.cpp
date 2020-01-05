// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheModule.h"
#if WITH_EDITOR
#include "GeometryCacheEdModule.h"
#endif // WITH_EDITOR
#include "CodecV1.h"

IMPLEMENT_MODULE(FGeometryCacheModule, GeometryCache)

void FGeometryCacheModule::StartupModule()
{
#if WITH_EDITOR
	FGeometryCacheEdModule& Module = FModuleManager::LoadModuleChecked<FGeometryCacheEdModule>(TEXT("GeometryCacheEd"));
#endif

	FCodecV1Decoder::InitLUT();
}

void FGeometryCacheModule::ShutdownModule()
{
}
