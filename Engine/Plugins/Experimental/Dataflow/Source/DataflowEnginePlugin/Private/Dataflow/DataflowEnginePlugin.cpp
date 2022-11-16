// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEnginePlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "Dataflow/DataflowEngineRendering.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"


class FDataflowEnginePlugin : public IDataflowEnginePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FDataflowEnginePlugin, DataflowEnginePlugin)



void FDataflowEnginePlugin::StartupModule()
{
	Dataflow::RenderingCallbacks();
}


void FDataflowEnginePlugin::ShutdownModule()
{
	
}



