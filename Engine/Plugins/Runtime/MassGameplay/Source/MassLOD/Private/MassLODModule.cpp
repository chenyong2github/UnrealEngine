// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMassLODModule.h"
#include "UObject/CoreRedirects.h"


class FMassLODModule : public IMassLODModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FMassLODModule, MassLOD)



void FMassLODModule::StartupModule()
{
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)

	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("/Script/MassCommon.DataFragment_MassSimulationLODInfo"), TEXT("/Script/MassLOD.DataFragment_MassSimulationLODInfo"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("/Script/MassCommon.DataFragment_MassSimulationLOD"), TEXT("/Script/MassLOD.DataFragment_MassSimulationLOD"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("/Script/MassCommon.DataFragment_MassViewerLOD"), TEXT("/Script/MassLOD.DataFragment_MassViewerLOD"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassCommon.MassProcessor_LODBase"), TEXT("/Script/MassLOD.MassLODProcessorBase"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassLOD.MassProcessor_LODBase"), TEXT("/Script/MassLOD.MassLODProcessorBase"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassCommon.MassProcessor_MassSimulationLODViewersInfo"), TEXT("/Script/MassLOD.MassProcessor_MassSimulationLODViewersInfo"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassCommon.MassProcessor_MassSimulationLOD"), TEXT("/Script/MassLOD.MassProcessor_MassSimulationLOD"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassCommon.MassProcessor_MassViewerLOD"), TEXT("/Script/MassLOD.MassProcessor_MassViewerLOD"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassLOD.MassLODTrait"), TEXT("/Script/MassLOD.MassSimulationLODTrait"));
	
	FCoreRedirects::AddRedirectList(Redirects, TEXT("MassMovement"));
}


void FMassLODModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}



