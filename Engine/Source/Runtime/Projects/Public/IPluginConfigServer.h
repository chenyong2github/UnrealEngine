// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"
#include "PluginReferenceDescriptor.h"
#include "CoreMinimal.h"

/** Modular feature which allows arbirary systems to extend plugin discovery inside of the engine's plugin manager. */
class IPluginConfigServer : public IModularFeature
{
public:
	virtual ~IPluginConfigServer() {}

	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("PluginConfigServer"));
		return FeatureName;
	}

	/**
	 * Supplies additional configurations for enabling/disabling plugins.
	 * This is called BEFORE the engine has processed plugin configurations in the .uproject file.
	 * NOTE: In the case of multiple configurations for the same plugin, the configuration processed first takes priority.
	 * 
	 * @param  OutPluginRefs	An array of plugin configurations to append to.
	 */
	virtual void PreProjConfig_GetPluginConfigurations(TArray<FPluginReferenceDescriptor>& OutPluginRefs) const = 0;

	/**
	 * Supplies additional configurations for enabling/disabling plugins.
	 * This is called AFTER the engine has processed plugin configurations in the .uproject file.
	 * NOTE: In the case of multiple configurations for the same plugin, the configuration processed first takes priority.
	 *
	 * @param  OutPluginRefs	An array of plugin configurations to append to.
	 */
	virtual void PostProjConfig_GetPluginConfigurations(TArray<FPluginReferenceDescriptor>& OutPluginRefs) const = 0;
};