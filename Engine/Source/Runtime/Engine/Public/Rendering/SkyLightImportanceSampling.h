// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"
#include "RenderingThread.h"

class FSkyLightImportanceSamplingData :
	public FRenderResource,
	private FDeferredCleanupInterface // FDeferredCleanupInterface & reference counting to survive ApplyComponentInstanceData so we don't have catastrophic performance with blueprinted components / sequencer
{
public:
	bool bIsValid = false;

	FIntVector MipDimensions;

	FRWBuffer MipTreePosX;
	FRWBuffer MipTreeNegX;
	FRWBuffer MipTreePosY;
	FRWBuffer MipTreeNegY;
	FRWBuffer MipTreePosZ;
	FRWBuffer MipTreeNegZ;

	FRWBuffer MipTreePdfPosX;
	FRWBuffer MipTreePdfNegX;
	FRWBuffer MipTreePdfPosY;
	FRWBuffer MipTreePdfNegY;
	FRWBuffer MipTreePdfPosZ;
	FRWBuffer MipTreePdfNegZ;

	FRWBuffer SolidAnglePdf;

	void BuildCDFs(FTexture* ProcessedTexture);

	virtual void ReleaseRHI() override;

public:
	void AddRef();
	void Release();

private:
	int32 NumRefs = 0;
};
