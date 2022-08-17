// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheModule.h"
#if WITH_EDITOR
#include "GeometryCacheEdModule.h"
#endif // WITH_EDITOR
#include "GeometryCacheStreamingManager.h"
#include "CodecV1.h"
#include "NiagaraGeometryCacheRendererProperties.h"

IMPLEMENT_MODULE(FGeometryCacheModule, GeometryCache)

void FGeometryCacheModule::StartupModule()
{
#if WITH_EDITOR
	FGeometryCacheEdModule& Module = FModuleManager::LoadModuleChecked<FGeometryCacheEdModule>(TEXT("GeometryCacheEd"));
#endif

	IGeometryCacheStreamingManager::Register();

	FCodecV1Decoder::InitLUT();

	UNiagaraRendererProperties::InitCDOPropertiesAfterModuleStartup(UNiagaraGeometryCacheRendererProperties::StaticClass());
}

void FGeometryCacheModule::ShutdownModule()
{
	IGeometryCacheStreamingManager::Unregister();
}
