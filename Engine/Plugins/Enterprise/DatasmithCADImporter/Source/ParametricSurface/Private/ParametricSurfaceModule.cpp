// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParametricSurfaceModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "ParametricSurfaceModule"

void FParametricSurfaceModule::StartupModule()
{
	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("UParametricSurfaceData.RawData"), TEXT("RawData_DEPRECATED"));
	FCoreRedirects::AddRedirectList(Redirects, PARAMETRICSURFACE_MODULE_NAME);
}

FParametricSurfaceModule& FParametricSurfaceModule::Get()
{
	return FModuleManager::LoadModuleChecked< FParametricSurfaceModule >(PARAMETRICSURFACE_MODULE_NAME);
}

bool FParametricSurfaceModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(PARAMETRICSURFACE_MODULE_NAME);
}

IMPLEMENT_MODULE(FParametricSurfaceModule, ParametricSurface);

#undef LOCTEXT_NAMESPACE // "ParametricSurfaceModule"

