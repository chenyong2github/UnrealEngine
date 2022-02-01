// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParametricSurfaceModule.h"

#include "CoreTech/CoreTechSurfaceData.h"
#include "TechSoft/TechSoftParametricSurface.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "ParametricSurfaceModule"

void FParametricSurfaceModule::StartupModule()
{
	{
		TArray<FCoreRedirect> Redirects;
		Redirects.Emplace(ECoreRedirectFlags::Type_Package, TEXT("/Script/DatasmithCoreTechParametricSurfaceData"), TEXT("/Script/ParametricSurface"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("UParametricSurfaceData.RawData"), TEXT("RawData_DEPRECATED"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("UCoreTechParametricSurfaceData.RawData"), TEXT("RawData_DEPRECATED"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("CoreTechSceneParameters"), TEXT("ParametricSceneParameters"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("CoreTechMeshParameters"), TEXT("ParametricMeshParameters"));
		FCoreRedirects::AddRedirectList(Redirects, PARAMETRICSURFACE_MODULE_NAME);
	}
}

FParametricSurfaceModule& FParametricSurfaceModule::Get()
{
	return FModuleManager::LoadModuleChecked< FParametricSurfaceModule >(PARAMETRICSURFACE_MODULE_NAME);
}

bool FParametricSurfaceModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(PARAMETRICSURFACE_MODULE_NAME);
}

UParametricSurfaceData* FParametricSurfaceModule::CreateParametricSurface(const TCHAR* CADLibraryName)
{
	if (!FCString::Strcmp(TEXT("TechSoft"), CADLibraryName))
	{
		return Datasmith::MakeAdditionalData<UTechSoftParametricSurfaceData>();
	}

	if (!FCString::Strcmp(TEXT("KernelIO"), CADLibraryName))
	{
		return Datasmith::MakeAdditionalData<UCoreTechParametricSurfaceData>();
	}

	return nullptr;
}

IMPLEMENT_MODULE(FParametricSurfaceModule, ParametricSurface)

#undef LOCTEXT_NAMESPACE // "ParametricSurfaceModule"

