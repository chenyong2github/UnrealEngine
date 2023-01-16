// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosFleshEnginePlugin.h"

#include "ChaosCache/FleshComponentCacheAdapter.h"



class FChaosFleshEnginePlugin : public IChaosFleshEnginePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<Chaos::FFleshCacheAdapter> FleshCacheAdapter;
};

IMPLEMENT_MODULE( FChaosFleshEnginePlugin, FleshEngine )



void FChaosFleshEnginePlugin::StartupModule()
{
	FleshCacheAdapter = MakeUnique<Chaos::FFleshCacheAdapter>();
	Chaos::RegisterAdapter(FleshCacheAdapter.Get());
}


void FChaosFleshEnginePlugin::ShutdownModule()
{
	
}



