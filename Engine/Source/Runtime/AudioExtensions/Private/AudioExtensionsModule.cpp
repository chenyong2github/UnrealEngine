// Copyright Epic Games, Inc. All Rights Reserved.


#include "Modules/ModuleManager.h"

class FAudioExtensionsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModuleChecked(TEXT("SignalProcessing"));
		FModuleManager::Get().LoadModuleChecked(TEXT("AudioMixerCore"));
	}
};

IMPLEMENT_MODULE(FAudioExtensionsModule, AudioExtensions);
