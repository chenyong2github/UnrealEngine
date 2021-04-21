// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHITransientResourceAllocator.h"
#include "RHICommandList.h"

FRHIShaderResourceView* FRHITransientTexture::GetOrCreateSRV(const FRHITextureSRVCreateInfo& SRVCreateInfo)
{
	for (const auto& KeyValue : SRVs)
	{
		if (KeyValue.Key == SRVCreateInfo)
		{
			return KeyValue.Value.GetReference();
		}
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (SRVCreateInfo.MetaData == ERHITextureMetaDataAccess::None)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(GetRHI(), SRVCreateInfo);
	}
	else
	{
		switch (SRVCreateInfo.MetaData)
		{
		case ERHITextureMetaDataAccess::HTile:
			check(GRHISupportsExplicitHTile);
			RHIShaderResourceView = RHICreateShaderResourceViewHTile((FRHITexture2D*)GetRHI());
			break;

		case ERHITextureMetaDataAccess::FMask:
			RHIShaderResourceView = RHICreateShaderResourceViewFMask((FRHITexture2D*)GetRHI());
			break;

		case ERHITextureMetaDataAccess::CMask:
			RHIShaderResourceView = RHICreateShaderResourceViewWriteMask((FRHITexture2D*)GetRHI());
			break;
		}
	}

	check(RHIShaderResourceView);
	FRHIShaderResourceView* View = RHIShaderResourceView.GetReference();
	SRVs.Emplace(SRVCreateInfo, MoveTemp(RHIShaderResourceView));
	return View;
}

FRHIUnorderedAccessView* FRHITransientTexture::GetOrCreateUAV(const FRHITextureUAVCreateInfo& UAVCreateInfo)
{
	for (const auto& KeyValue : UAVs)
	{
		if (KeyValue.Key == UAVCreateInfo)
		{
			return KeyValue.Value.GetReference();
		}
	}

	FUnorderedAccessViewRHIRef RHIUnorderedAccessView;

	if (UAVCreateInfo.MetaData == ERHITextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		RHIUnorderedAccessView = RHICreateUnorderedAccessViewHTile((FRHITexture2D*)GetRHI());
	}
	else if (UAVCreateInfo.MetaData == ERHITextureMetaDataAccess::Stencil)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessViewStencil((FRHITexture2D*)GetRHI(), 0);
	}
	else
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(GetRHI(), UAVCreateInfo.MipLevel);
	}

	check(RHIUnorderedAccessView);
	FRHIUnorderedAccessView* View = RHIUnorderedAccessView.GetReference();
	UAVs.Emplace(UAVCreateInfo, MoveTemp(RHIUnorderedAccessView));
	return View;
}


FRHIShaderResourceView* FRHITransientBuffer::GetOrCreateSRV(const FRHIBufferSRVCreateInfo& SRVCreateInfo)
{
	for (const auto& KeyValue : SRVs)
	{
		if (KeyValue.Key == SRVCreateInfo)
		{
			return KeyValue.Value.GetReference();
		}
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (SRVCreateInfo.Format != PF_Unknown)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(GetRHI(), SRVCreateInfo.BytesPerElement, SRVCreateInfo.Format);
	}
	else
	{
		RHIShaderResourceView = RHICreateShaderResourceView(GetRHI());
	}

	FRHIShaderResourceView* View = RHIShaderResourceView.GetReference();
	SRVs.Emplace(SRVCreateInfo, MoveTemp(RHIShaderResourceView));
	return View;
}

FRHIUnorderedAccessView* FRHITransientBuffer::GetOrCreateUAV(const FRHIBufferUAVCreateInfo& UAVCreateInfo)
{
	for (const auto& KeyValue : UAVs)
	{
		if (KeyValue.Key == UAVCreateInfo)
		{
			return KeyValue.Value.GetReference();
		}
	}

	FUnorderedAccessViewRHIRef RHIUnorderedAccessView;

	if (UAVCreateInfo.Format != PF_Unknown)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(GetRHI(), UAVCreateInfo.Format);
	}
	else
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(GetRHI(), UAVCreateInfo.bSupportsAtomicCounter, UAVCreateInfo.bSupportsAppendBuffer);
	}

	FRHIUnorderedAccessView* View = RHIUnorderedAccessView.GetReference();
	UAVs.Emplace(UAVCreateInfo, MoveTemp(RHIUnorderedAccessView));
	return View;
}

void IRHITransientResourceAllocator::Release(FRHICommandListImmediate&)
{
	delete this;
}