// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "ICustomizableObjectModule.h"
#include "CustomizableObjectSystem.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"

#include "Runtime/Launch/Resources/Version.h"

#include "MutableRuntime/Public/MutableMemory.h"
#include "MutableRuntime/Public/System.h"

/**
 * Customizable Object module implementation (private)
 */
class FCustomizableObjectModule : public ICustomizableObjectModule
{
public:

	// IModuleInterface 
	void StartupModule() override;
	void ShutdownModule() override;

	// ICustomizableObjectModule interface
	FString GetPluginVersion() const override;
	bool AreExtraBoneInfluencesEnabled() const override;
};


IMPLEMENT_MODULE( FCustomizableObjectModule, CustomizableObject );

void FCustomizableObjectModule::StartupModule()
{
}


void FCustomizableObjectModule::ShutdownModule()
{
}


FString FCustomizableObjectModule::GetPluginVersion() const
{
	FString Version = "x.x";
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("Mutable");
	if (Plugin.IsValid() && Plugin->IsEnabled())
	{
		Version = Plugin->GetDescriptor().VersionName;
	}
	return Version;
}

bool FCustomizableObjectModule::AreExtraBoneInfluencesEnabled() const
{
	bool bAreExtraBoneInfluencesEnabled = false;

	FConfigFile* PluginConfig = GConfig->FindConfigFileWithBaseName("Mutable");
	if (PluginConfig)
	{
		bool bValue = false;
		if (PluginConfig->GetBool(TEXT("Features"), TEXT("bExtraBoneInfluencesEnabled"), bValue))
		{
			bAreExtraBoneInfluencesEnabled = bValue;
		}
	}

	return bAreExtraBoneInfluencesEnabled;
}
