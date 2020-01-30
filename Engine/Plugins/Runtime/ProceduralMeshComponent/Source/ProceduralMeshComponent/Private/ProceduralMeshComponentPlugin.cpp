// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "ComponentSourceInterfaces.h"
#include "Modules/ModuleManager.h"
#include "ProceduralMeshBridge.h"
#include "IProceduralMeshComponentPlugin.h"

class FProceduralMeshComponentPlugin : public IProceduralMeshComponentPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FProceduralMeshComponentPlugin, ProceduralMeshComponent )



void FProceduralMeshComponentPlugin::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	AddComponentTargetFactory( TUniquePtr<FComponentTargetFactory>{new FProceduralMeshComponentTargetFactory{} } );
}


void FProceduralMeshComponentPlugin::ShutdownModule()
{
	
}



