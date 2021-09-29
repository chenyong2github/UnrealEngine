// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMassMovementModule.h"
#include "UObject/CoreRedirects.h"


class FMassAIMovementModule : public IMassAIMovementModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FMassAIMovementModule, MassAIMovement)



void FMassAIMovementModule::StartupModule()
{
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)

	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("AvoidanceComponent"), TEXT("MassAvoidanceFragment"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("DataFragment_GridCellLocation"), TEXT("MassGridCellLocationFragment"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("DataFragment_PathFollow"), TEXT("MassPathFollowFragment"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("DataFragment_Wander"), TEXT("MassWanderFragment"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("DataFragment_MoveTo"), TEXT("MassMoveToFragment"));
	
	FCoreRedirects::AddRedirectList(Redirects, TEXT("MassAIMovement"));
}


void FMassAIMovementModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}



