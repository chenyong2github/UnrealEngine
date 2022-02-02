// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMassRepresentationModule.h"
#include "UObject/CoreRedirects.h"

class FMassRepresentationModule : public IMassRepresentationModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FMassRepresentationModule, MassRepresentation)



void FMassRepresentationModule::StartupModule()
{
	TArray<FCoreRedirect> Redirects;

	Redirects.Emplace(ECoreRedirectFlags::Type_Struct, TEXT("/Script/MassVisualization.DataFragment_Visualization"), TEXT("/Script/MassRepresentation.MassRepresentationFragment"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassVisualization.MassProcessor_Visualization"), TEXT("/Script/MassRepresentation.MassRepresentationProcessor"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassVisualization.MassFragmentDestructor_Visualization"), TEXT("/Script/MassRepresentation.MassRepresentationFragmentDestructor"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassVisualization.MassVisualizationLODProcessor"), TEXT("/Script/MassRepresentation.MassVisualizationLODProcessor"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassRepresentation.MassRepresentationLODProcessor"), TEXT("/Script/MassRepresentation.MassVisualizationLODProcessor"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassVisualization.MassVisualizationFeature"), TEXT("/Script/MassRepresentation.MassVisualizationTrait"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Script/MassRepresentation.MassVisualizationTrait.TemplateActor"), TEXT("/Script/MassRepresentation.MassVisualizationTrait.HighResTemplateActor"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Enum, TEXT("/Script/MassRepresentation.RepresentationType"), TEXT("/Script/MassRepresentation.MassRepresentationType"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Enum, TEXT("RepresentationType"),                            TEXT("/Script/MassRepresentation.MassRepresentationType"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Enum, TEXT("/Script/MassRepresentation.ActorEnabledType"), TEXT("/Script/MassRepresentation.MassActorEnabledType"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Enum, TEXT("ActorEnabledType"),                            TEXT("/Script/MassRepresentation.MassActorEnabledType"));


	FCoreRedirects::AddRedirectList(Redirects, TEXT("MassRepresentation"));
}


void FMassRepresentationModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}



