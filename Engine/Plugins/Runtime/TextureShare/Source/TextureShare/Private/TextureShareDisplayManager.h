// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

#include "Templates/SharedPointer.h"

class FTextureShareDisplayExtension;
class FViewport;
class FTextureShareModule;
class FSceneViewFamily;

class FTextureShareDisplayManager
{
public:
	FTextureShareDisplayManager(FTextureShareModule& InTextureShareModule);
	~FTextureShareDisplayManager();

	/** Will return the extension associated to the desired viewport or create one if it's not tracked */
	TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe> FindOrAddDisplayConfiguration(FViewport* InViewport);

	/** Remove display configuration associated with this viewport */
	bool RemoveDisplayConfiguration(const FViewport* InViewport);

	/** Whether or not InViewport has a display configuration linked to it */
	bool IsTrackingViewport(const FViewport* InViewport) const;

public:
	/** Begin scene sharing, initialize callbacks */
	bool BeginSceneSharing();
	void EndSceneSharing();

	/** Display extension global cb */
	void OnBeginRenderViewFamily(FSceneViewFamily& InViewFamily);
	void OnPreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily);
	void OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily);


private:
	/** Rendered callback (get scene textures to share) */
	void OnResolvedSceneColor_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext);

	bool IsSceneSharingValid() const
	{ return bIsRenderedCallbackAssigned && DisplayExtensions.Num()>0; }


protected:
	TArray<TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>> DisplayExtensions;
	FTextureShareModule& TextureShareModule;

private:
	bool bIsRenderedCallbackAssigned = false;
	FSceneViewFamily* CurrentSceneViewFamily = nullptr;

};

