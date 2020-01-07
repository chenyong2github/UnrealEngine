// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

class FMPCDIData;

class IOutputRemap : public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("OutputRemap");
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */

	static inline IOutputRemap& Get()
	{
		return FModuleManager::LoadModuleChecked<IOutputRemap>(IOutputRemap::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IOutputRemap::ModuleName);
	}

public:
	/**
	* Loads an external remap mesh file
	*
	* @param RemapMeshFile - file path (.obj fmt)
	* @param OutMeshHandler - handler to loaded data
	*
	* @return - true if success
	*/
	virtual bool Load(const FString& RemapMeshFile, uint32& OutMeshRef) = 0;

	/**
	* Reload all changed external mesh files
	*/
	virtual void ReloadAll() = 0;


	/**
	* Applies Output Remap
	*
	* @param RHICmdList      - RHI command list
	* @param TextureWarpData - texture warp data
	* @param ShaderInputData - shader data
	*
	* @return - true if success
	*/
	virtual bool ApplyOutputRemap_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* TargetableTexture, uint32 MeshRef) = 0;
};
