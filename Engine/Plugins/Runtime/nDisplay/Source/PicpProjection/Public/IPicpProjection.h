// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Blueprints/PicpProjectionFrustumDataListener.h"
#include "IMPCDI.h"

class IDisplayClusterProjectionPolicyFactory;
class FPicpProjectionOverlayFrameData;
class UTextureRenderTarget2D;


class IPicpProjection : public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("PicpProjection");

public:
	virtual ~IPicpProjection()
	{ }

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IPicpProjection& Get()
	{
		return FModuleManager::LoadModuleChecked<IPicpProjection>(IPicpProjection::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IPicpProjection::ModuleName);
	}

	/**
	* Returns supported projection types
	*
	* @param OutProjectionTypes - (out) Array of supported projection types
	*/
	virtual void GetSupportedProjectionTypes(TArray<FString>& OutProjectionTypes) = 0;

	/**
	* Returns specified projection factory
	*
	* @param InProjectionType - Projection type
	*
	* @return - Projection policy factory of requested type, null if not available
	*/
	virtual TSharedPtr<IDisplayClusterProjectionPolicyFactory> GetProjectionFactory(const FString& InProjectionType) = 0;

	/**
	* Returns all used viewports
	*
	* @return - 
	*/
	//virtual TArray<TSharedPtr<FPicpProjectionViewportBase>> GetProjectionViewports(const FString& InProjectionType) = 0;

	virtual int GetPolicyCount(const FString& InProjectionType) = 0;
	virtual void SetOverlayFrameData(const FString& PolicyType, FPicpProjectionOverlayFrameData& OverlayFrameData) = 0;

	virtual void CaptureWarpTexture(UTextureRenderTarget2D* dst, const FString& ViewportId, const uint32 ViewIdx, bool bCaptureNow) = 0;

	virtual bool GetWarpFrustum(const FString& ViewportId, const uint32 ViewIdx, IMPCDI::FFrustum& OutFrustum, bool bIsCaptureWarpTextureFrustum) = 0;

	// callback
	virtual void SetViewport(const FString& ViewportId, FRotator& OutViewRotation, FVector& OutViewLocation, FMatrix& OutPrjMatrix) = 0;

	virtual void AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener) = 0;
	virtual void RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener) = 0;
	virtual void CleanProjectionDataListeners() = 0;
};
