// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"


/**
 * nDisplay post-process interface
 */
class IDisplayClusterPostProcess
{
public:
	virtual ~IDisplayClusterPostProcess() = 0
	{ }

public:
	/**
	* Game thread call. Initialize postprocess from config line
	*
	* @param CfgLine - Configuration line for this postprocess
	*/
	virtual void InitializePostProcess(const FString& CfgLine)
	{ }

	/**
	* Returns if an interface implementation processes each view region before warp&blend
	*
	* @example - A stereo device with 2 viewports renders 4 different regions so the corresponded function will be called 4 times (2 eyes * 2 viewports)
	* @example - A monoscopic device with 3 viewports renders 3 different regions so the corresponded function will be called 3 times (1 eye * 3 viewports)
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessViewBeforeWarpBlendRequired()
	{ return false; }

	/**
	* PP operation on a view region before warp&blend (if available for the current projection policy)
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	* @param ViewRect   - View region of the texture
	*/
	virtual void PerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const
	{ }

	/**
	* Returns if an interface implementation processes each view region after warp&blend
	*
	* @example - A stereo device with 2 viewports renders 4 different regions so the corresponded function will be called 4 times (2 eyes * 2 viewports)
	* @example - A monoscopic device with 3 viewports renders 3 different regions so the corresponded function will be called 3 times (1 eye * 3 viewports)
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessViewAfterWarpBlendRequired()
	{ return false; }

	/**
	* PP operation on a view region after warp&blend (if available for the current projection policy)
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	* @param ViewRect   - View region of the texture
	*/
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const
	{ }

	/**
	* Returns if an interface implementation processes output frames separately before warp&blend (2 frames for stereo, 1 frame for monoscopic)
	*
	* @example - A stereo device with 3 viewports will call the corresponded function twice (1 frame for 3 regions of left eye and 1 frame for 3 regions of right eye)
	* @example - A monoscopic device with 2 viewports will call the corresponded function once
	*
	* @param FramesAmount - Amount of frames on a render target (1 for mono, 2 for stereo...)
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessFrameBeforeWarpBlendRequired(uint32 FramesAmount)
	{ return false; }

	/**
	* PP operation on a frame region before warp&blend
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	* @param ViewRect   - Frame region of the texture
	*/
	virtual void PerformPostProcessFrameBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const
	{ }

	/**
	* Returns if an interface implementation processes output frames separately after warp&blend (2 frames for stereo, 1 frame for monoscopic)
	*
	* @example - A stereo device with 3 viewports will call the corresponded function twice (1 frame for 3 regions of left eye and 1 frame for 3 regions of right eye)
	* @example - A monoscopic device with 2 viewports will call the corresponded function once
	*
	* @param FramesAmount - Amount of frames on a render target (1 for mono, 2 for stereo...)
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessFrameAfterWarpBlendRequired(uint32 FramesAmount)
	{ return false; }

	/**
	* PP operation on a frame region after warp&blend
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	* @param ViewRect   - Frame region of the texture
	*/
	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const
	{ }

	/**
	* Returns if an interface implementation processes the final render target with all views before warp&blend
	*
	* @example - A stereo device typically requires 2 time bigger (at least) render target than the viewport size. This function allows to perform a PP operation to the whole render target.
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessRenderTargetBeforeWarpBlendRequired()
	{ return false; }

	/**
	* PP operation on a render target before warp&blend
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	*/
	virtual void PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const
	{ }

	/**
	* Returns if an interface implementation processes the final render target with all views after warp&blend
	*
	* @example - A stereo device typically requires 2 time bigger (at least) render target than the viewport size. This function allows to perform a PP operation to the whole render target.
	*
	* @return - true if required
	*/
	virtual bool IsPostProcessRenderTargetAfterWarpBlendRequired()
	{ return false; }

	/**
	* PP operation on a render target after warp&blend
	*
	* @param RHICmdList - RHI command list
	* @param SrcTexture - Source texture
	*/
	virtual void PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const
	{ }
};
