// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterMediaBase.h"
#include "UObject/GCObject.h"

class FRHICommandListImmediate;
class UMediaCapture;
class UMediaOutput;
class UTextureRenderTarget2D;


/**
 * Base media capture class
 */
class FDisplayClusterMediaCaptureBase
	: public FDisplayClusterMediaBase
	, public FGCObject
{
public:
	FDisplayClusterMediaCaptureBase(const FString& MediaId, const FString& ClusterNodeId, UMediaOutput* MediaOutput, UTextureRenderTarget2D* RenderTarget);

public:
	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FDisplayClusterMediaCaptureBase");
	}
	//~ End FGCObject interface

public:
	virtual bool StartCapture();
	virtual void StopCapture();

	UTextureRenderTarget2D* GetRenderTarget()
	{
		return RenderTarget;
	}

protected:
	void ExportMediaData(FRHICommandListImmediate& RHICmdList, const FMediaTextureInfo& TextureInfo);

private:
	//~ Begin GC by AddReferencedObjects
	UMediaOutput*           MediaOutput  = nullptr;
	UMediaCapture*          MediaCapture = nullptr;
	UTextureRenderTarget2D* RenderTarget = nullptr;
	//~ End GC by AddReferencedObjects
};
