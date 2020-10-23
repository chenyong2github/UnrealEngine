// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechSurfaceModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "CoreTechSurfaceModule"

void FCoreTechSurfaceModule::StartupModule()
{
	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("UCoreTechParametricSurfaceData.RawData"), TEXT("RawData_DEPRECATED"));
	FCoreRedirects::AddRedirectList(Redirects, CORETECHSURFACE_MODULE_NAME);
}

FCoreTechSurfaceModule& FCoreTechSurfaceModule::Get()
{
	return FModuleManager::LoadModuleChecked< FCoreTechSurfaceModule >(CORETECHSURFACE_MODULE_NAME);
}

bool FCoreTechSurfaceModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(CORETECHSURFACE_MODULE_NAME);
}

IMPLEMENT_MODULE(FCoreTechSurfaceModule, CoreTechSurface);

#undef LOCTEXT_NAMESPACE // "CoreTechSurfaceModule"

