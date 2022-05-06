// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMesh.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMesh"

class FNaniteDisplacedMeshModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FNaniteDisplacedMeshModule, NaniteDisplacedMesh);

#undef LOCTEXT_NAMESPACE