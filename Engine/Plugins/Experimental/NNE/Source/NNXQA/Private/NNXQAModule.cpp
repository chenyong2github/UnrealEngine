// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "NNXQAParametricTest.h"

class FNNXQAModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		NNX::Test::InitializeParametricTests();
	}

	virtual void ShutdownModule() override
	{
	}

};

IMPLEMENT_MODULE(FNNXQAModule, NNXQA);