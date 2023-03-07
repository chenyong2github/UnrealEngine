// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryProcessingInterfacesModule.h"
#include "CoreGlobals.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

#include "GeometryProcessingInterfaces/ApproximateActors.h"
#include "GeometryProcessingInterfaces/CombineMeshInstances.h"


IMPLEMENT_MODULE(FGeometryProcessingInterfacesModule, GeometryProcessingInterfaces);


void FGeometryProcessingInterfacesModule::StartupModule()
{

}


void FGeometryProcessingInterfacesModule::ShutdownModule()
{
	ApproximateActors = nullptr;
	CombineMeshInstances = nullptr;
}


IGeometryProcessing_ApproximateActors* FGeometryProcessingInterfacesModule::GetApproximateActorsImplementation()
{
	if (ApproximateActors == nullptr)
	{
		TArray<IGeometryProcessing_ApproximateActors*> ApproximateActorsOptions =
			IModularFeatures::Get().GetModularFeatureImplementations<IGeometryProcessing_ApproximateActors>(IGeometryProcessing_ApproximateActors::GetModularFeatureName());

		ApproximateActors = (ApproximateActorsOptions.Num() > 0) ? ApproximateActorsOptions[0] : nullptr;
	}

	return ApproximateActors;
}
IGeometryProcessing_CombineMeshInstances* FGeometryProcessingInterfacesModule::GetCombineMeshInstancesImplementation()
{
	if (CombineMeshInstances == nullptr)
	{
		TArray<IGeometryProcessing_CombineMeshInstances*> CombineMeshInstancesOptions =
			IModularFeatures::Get().GetModularFeatureImplementations<IGeometryProcessing_CombineMeshInstances>(IGeometryProcessing_CombineMeshInstances::GetModularFeatureName());

		CombineMeshInstances = (CombineMeshInstancesOptions.Num() > 0) ? CombineMeshInstancesOptions[0] : nullptr;
	}

	return CombineMeshInstances;
}


