// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDirectLinkManager.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"		// For inline LoadModuleChecked()

#define DIRECTLINKEXTENSION_MODULE_NAME TEXT("DirectLinkExtension")

namespace UE::DatasmithImporter
{
	class FDirectLinkExternalSource;
}

namespace DirectLink
{
	class FEndpoint;
}

class IDirectLinkExtensionModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to IDirectLinkEditorModule
	 *
	 * @return Returns IDirectLinkEditorModule singleton instance, loading the module on demand if needed
	 */
	static inline IDirectLinkExtensionModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IDirectLinkExtensionModule>(DIRECTLINKEXTENSION_MODULE_NAME);
	}

	/**
	 * Checks to see if this module is loaded and ready.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(DIRECTLINKEXTENSION_MODULE_NAME);
	}

	/**
	 * Return the DirectLinkManager singleton.
	 */
	virtual UE::DatasmithImporter::IDirectLinkManager& GetManager() const = 0;

	/**
	 * Spawn a dialog window prompting the user to select one available FDirectLinkExternalSource.
	 * The list of displayed DirectLink source can be filtered.
	 * @return The selected DirectLinkExternalSource, nullptr is returned if the dialog was canceled. 
	 */
	virtual TSharedPtr<UE::DatasmithImporter::FDirectLinkExternalSource> DisplayDirectLinkSourcesDialog() = 0;

	static DirectLink::FEndpoint& GetEndpoint()
	{
		return Get().GetManager().GetEndpoint();
	}
};

