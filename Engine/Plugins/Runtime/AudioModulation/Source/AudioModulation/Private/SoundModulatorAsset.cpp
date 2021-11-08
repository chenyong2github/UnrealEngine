// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundModulatorAsset.h"
#include "Templates/SharedPointer.h"

namespace AudioModulation
{
	const FText PluginAuthor = NSLOCTEXT("AudioModulation", "PluginAuthor", "Epic Games, Inc.");
	const FText PluginNodeMissingPrompt = NSLOCTEXT("AudioModulation", "DefaultMissingNodePrompt", "The node was likely removed, renamed, or the AudioModulation plugin is not loaded.");

	FSoundModulatorAsset::FSoundModulatorAsset(const Audio::IProxyDataPtr& InInitData)
	{
		if (!InInitData.IsValid())
		{
			return;
		}

		if (!ensure(InInitData->CheckTypeCast<FSoundModulatorAssetProxy>()))
		{
			return;
		}

		FSoundModulatorAssetProxy* NewProxy = static_cast<FSoundModulatorAssetProxy*>(InInitData->Clone().Release());
		Proxy = TSharedPtr<FSoundModulatorAssetProxy, ESPMode::ThreadSafe>(NewProxy);
	}

	FSoundModulationParameterAsset::FSoundModulationParameterAsset(const Audio::IProxyDataPtr& InInitData)
	{
		if (!InInitData.IsValid())
		{
			return;
		}

		if (!ensure(InInitData->CheckTypeCast<FSoundModulationParameterAssetProxy>()))
		{
			return;
		}

		FSoundModulationParameterAssetProxy* NewProxy = static_cast<FSoundModulationParameterAssetProxy*>(InInitData->Clone().Release());
		Proxy = TSharedPtr<FSoundModulationParameterAssetProxy, ESPMode::ThreadSafe>(NewProxy);
	}
} // namespace AudioModulation
