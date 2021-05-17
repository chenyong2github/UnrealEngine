// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"


class FDisplayClusterRender_MeshComponentProxy;
class FDisplayClusterShaderParameters_ICVFX;
class FDisplayClusterShaderParameters_Override;
struct FDisplayClusterShaderParameters_GenerateMips;
struct FDisplayClusterShaderParameters_PostprocessBlur;
struct FDisplayClusterShaderParameters_WarpBlend;
class IDisplayClusterWarpBlendManager;


class IDisplayClusterShaders : public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("DisplayClusterShaders");

public:
	virtual ~IDisplayClusterShaders() = default;

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IDisplayClusterShaders& Get()
	{
		return FModuleManager::LoadModuleChecked<IDisplayClusterShaders>(IDisplayClusterShaders::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IDisplayClusterShaders::ModuleName);
	}

public:
	/**
	* Warp&blend manager interface
	*
	* @return interface
	*/
	virtual const IDisplayClusterWarpBlendManager& GetWarpBlendManager() const = 0;

	/**
	* Render warp&blend
	*
	* @param RHICmdList      - RHI command list
	* @param InWarpBlendParameters - warp blend parameters
	*
	* @return - true if success
	*/
	virtual bool RenderWarpBlend_MPCDI(FRHICommandListImmediate& RHICmdList, const FDisplayClusterShaderParameters_WarpBlend& InWarpBlendParameters) const = 0;

	/**
	* Render ICVFX warp&blend
	*
	* @param RHICmdList            - RHI command list
	* @param InWarpBlendParameters - WarpBlend parameters
	* @param InICVFXParameters     - ICVFX parameters
	*
	* @return - true if success
	*/
	virtual bool RenderWarpBlend_ICVFX(FRHICommandListImmediate& RHICmdList, const FDisplayClusterShaderParameters_WarpBlend& InWarpBlendParameters, const FDisplayClusterShaderParameters_ICVFX& InICVFXParameters) const = 0;

	/**
	* Render postprocess OutputRemap
	*
	* @param RHICmdList                    - RHI command list
	* @param InSourceTexture -             - Source shader resource texture
	* @param InRenderTargetableDestTexture - Destination RTT texture
	* @param MeshProxy                     - mesh for output remapping
	*
	* @return - true if success
	*/
	virtual bool RenderPostprocess_OutputRemap(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InSourceTexture, FRHITexture2D* InRenderTargetableDestTexture, const FDisplayClusterRender_MeshComponentProxy& MeshProxy) const = 0;

	/**
	* Render postprocess Blur
	*
	* @param RHICmdList                    - RHI command list
	* @param InSourceTexture -             - Source shader resource texture
	* @param InRenderTargetableDestTexture - Destination RTT texture
	* @param InSettings                    - Blur pp settings
	*
	* @return - true if success
	*/
	virtual bool RenderPostprocess_Blur(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InSourceTexture, FRHITexture2D* InRenderTargetableDestTexture, const FDisplayClusterShaderParameters_PostprocessBlur& InSettings) const = 0;

	/**
	* Generate Mips texture
	*
	* @param RHICmdList       - RHI command list
	* @param InOutMipsTexture - Mips textures with assigned mip0
	* @param InSettings       - mips settings
	*
	* @return - true if success
	*/
	virtual bool GenerateMips(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutMipsTexture, const FDisplayClusterShaderParameters_GenerateMips& InSettings) const = 0;
};
