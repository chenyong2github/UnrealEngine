// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCoreTechParametricSurfaceDataModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "DatasmithCoreTechParametricSurfaceDataModule"

void FDatasmithCoreTechParametricSurfaceDataModule::StartupModule()
{
	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("UCoreTechParametricSurfaceData.RawData"), TEXT("RawData_DEPRECATED"));
	FCoreRedirects::AddRedirectList(Redirects, DATASMITHCORETECHPARAMETRICSURFACEDATA_MODULE_NAME);
}

FDatasmithCoreTechParametricSurfaceDataModule& FDatasmithCoreTechParametricSurfaceDataModule::Get()
{
	return FModuleManager::LoadModuleChecked< FDatasmithCoreTechParametricSurfaceDataModule >(DATASMITHCORETECHPARAMETRICSURFACEDATA_MODULE_NAME);
}

bool FDatasmithCoreTechParametricSurfaceDataModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(DATASMITHCORETECHPARAMETRICSURFACEDATA_MODULE_NAME);
}

IMPLEMENT_MODULE(FDatasmithCoreTechParametricSurfaceDataModule, DatasmithCoreTechParametricSurfaceData);

#undef LOCTEXT_NAMESPACE // "DatasmithCoreTechParametricSurfaceDataModule"

