// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SceneUtils.h"

bool IsMobileHDR()
{
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	return MobileHDRCvar->GetValueOnAnyThread() == 1;
}

bool IsMobileHDR32bpp()
{
	static auto* MobileHDR32bppModeCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	return IsMobileHDR() && (GSupportsRenderTargetFormat_PF_FloatRGBA == false || MobileHDR32bppModeCvar->GetValueOnAnyThread() != 0);
}

bool IsMobileHDRMosaic()
{
	if (!IsMobileHDR32bpp())
		return false;

	static auto* MobileHDR32bppMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	switch (MobileHDR32bppMode->GetValueOnAnyThread())
	{
		case 1:
			return true;
		case 2:
		case 3:
			return false;
		default:
			return !(GSupportsHDR32bppEncodeModeIntrinsic && GSupportsShaderFramebufferFetch);
	}
}

ENGINE_API EMobileHDRMode GetMobileHDRMode()
{
	EMobileHDRMode HDRMode = EMobileHDRMode::EnabledFloat16;

	if (!IsMobileHDR() && !IsHTML5Platform())
	{
		HDRMode = EMobileHDRMode::Disabled;
	}
	
	if (IsMobileHDR32bpp())
	{
		static auto* MobileHDR32bppMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
		switch (MobileHDR32bppMode->GetValueOnAnyThread())
		{
			case 1:
				HDRMode = EMobileHDRMode::EnabledMosaic;
				break;
			case 2:
				HDRMode = EMobileHDRMode::EnabledRGBE;
				break;
			case 3:
				HDRMode = EMobileHDRMode::EnabledRGBA8;
				break;
			default:
				HDRMode = (GSupportsHDR32bppEncodeModeIntrinsic && GSupportsShaderFramebufferFetch) ? EMobileHDRMode::EnabledRGBE : EMobileHDRMode::EnabledMosaic;
				break;
		}
	}

	return HDRMode;
}

ENGINE_API bool IsMobileColorsRGB()
{
	static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
	const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);

	return !IsMobileHDR() && bMobileUseHWsRGBEncoding;
}

/*-----------------------------------------------------------------------------
FRenderQueryPool
-----------------------------------------------------------------------------*/
FRenderQueryPool::~FRenderQueryPool()
{
	Release();
}

void FRenderQueryPool::Release()
{
	Queries.Empty();
	NumQueriesAllocated = 0;
}

FRenderQueryRHIRef FRenderQueryPool::AllocateQuery()
{
	// Are we out of available render queries?
	NumQueriesAllocated++;
	if (Queries.Num() == 0)
	{
		// Create a new render query.
		return RHICreateRenderQuery(QueryType);
	}

	return Queries.Pop(/*bAllowShrinking=*/ false);
}

void FRenderQueryPool::ReleaseQuery(FRenderQueryRHIRef &Query)
{
	if (IsValidRef(Query))
	{
		NumQueriesAllocated--;

		// Is no one else keeping a refcount to the query?
		if (Query.GetRefCount() == 1)
		{
			// Return it to the pool.
			Queries.Add(Query);
		}

		// De-ref without deleting.
		Query = NULL;
	}
}