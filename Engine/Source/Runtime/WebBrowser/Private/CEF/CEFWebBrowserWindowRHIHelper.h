// Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3

class FSlateTexture2DRHIRef;

/**
 * Implementation of RHI renderer details for the CEF accelerated rendering path
 */
class FCEFWebBrowserWindowRHIHelper
{

public:
	/** Virtual Destructor. */
	virtual ~FCEFWebBrowserWindowRHIHelper();


public:
	static bool BUseRHIRenderer();
	FSlateUpdatableTexture*CreateTexture(void *ShareHandle);
	void UpdateSharedHandleTexture(void* SharedHandle, FSlateUpdatableTexture* SlateTexture, const FIntRect& DirtyIn);
	void UpdateCachedGeometry(const FGeometry& AllottedGeometry);
	TOptional<FSlateRenderTransform> GetWebBrowserRenderTransform() const;

private:
	FGeometry AllottedGeometry;
};

#endif
