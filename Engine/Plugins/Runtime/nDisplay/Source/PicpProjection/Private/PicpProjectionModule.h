// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"

#include "IPicpProjection.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"

class IDisplayClusterProjectionPolicyFactory;


class FPicpProjectionModule
	: public IPicpProjection
{
public:
	FPicpProjectionModule();
	virtual ~FPicpProjectionModule();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjection
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void GetSupportedProjectionTypes(TArray<FString>& OutProjectionTypes) override;
	virtual TSharedPtr<IDisplayClusterProjectionPolicyFactory> GetProjectionFactory(const FString& InProjectionType) override;
	
	virtual int GetPolicyCount(const FString& InProjectionType) override;


	virtual void AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener) override;
	virtual void RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener) override;
	virtual void CleanProjectionDataListeners() override;

	virtual void SetOverlayFrameData(const FString& PolicyType, FPicpProjectionOverlayFrameData& OverlayFrameData) override;
	virtual void SetViewport(const FString& ViewportId, FRotator& OutViewRotation, FVector& OutViewLocation, FMatrix& OutPrjMatrix);


	virtual void CaptureWarpTexture(UTextureRenderTarget2D* dst, const FString& ViewportId, const uint32 ViewIdx,  bool bCaptureNow) override;
	virtual bool GetWarpFrustum(const FString& ViewportId, const uint32 ViewIdx, IMPCDI::FFrustum& OutFrustum, bool bIsCaptureWarpTextureFrustum) override;

private:
	// Available factories
	TMap<FString, TSharedPtr<IDisplayClusterProjectionPolicyFactory>> ProjectionPolicyFactories;
	TArray<TScriptInterface<IPicpProjectionFrustumDataListener>> PicpEventListeners;
};
