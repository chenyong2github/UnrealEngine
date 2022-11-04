// Copyright Epic Games, Inc. All Rights Reserved.

#include "Resources/TextureShareResource.h"
#include "Containers/TextureShareContainers.h"

#include "ITextureShareCore.h"
#include "ITextureShareCoreAPI.h"
#include "ITextureShareCoreObject.h"
#include "ITextureShareCoreD3D12ResourcesCache.h"

#include "ID3D12DynamicRHI.h"

//////////////////////////////////////////////////////////////////////////////////////////////
namespace TextureShareResourceHelpers
{
	static TSharedPtr<ITextureShareCoreD3D12ResourcesCache, ESPMode::ThreadSafe> GetD3D12ResourcesCache()
	{
		static ITextureShareCoreAPI& TextureShareCoreAPI = ITextureShareCore::Get().GetTextureShareCoreAPI();
		return TextureShareCoreAPI.GetD3D12ResourcesCache();
	}

};
using namespace TextureShareResourceHelpers;

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareResource
//////////////////////////////////////////////////////////////////////////////////////////////
bool FTextureShareResource::D3D12RegisterResourceHandle(const FTextureShareCoreResourceRequest& InResourceRequest)
{
	if (IsRHID3D12() && TextureRHI.IsValid())
	{
		TSharedPtr<ITextureShareCoreD3D12ResourcesCache, ESPMode::ThreadSafe> D3D12ResourcesCache = GetD3D12ResourcesCache();
		if (D3D12ResourcesCache.IsValid())
		{
			ID3D12DynamicRHI* D3D12RHI = GetID3D12DynamicRHI();
			ID3D12Device* D3D12DevicePtr = D3D12RHI->RHIGetDevice(0);
			ID3D12Resource* D3D12ResourcePtr = (ID3D12Resource*)D3D12RHI->RHIGetResource(TextureRHI);

			if (D3D12DevicePtr && D3D12ResourcePtr)
			{
				FTextureShareCoreResourceHandle ResourceHandle;
				if (D3D12ResourcesCache->CreateSharedResource(CoreObject->GetObjectDesc(), D3D12DevicePtr, D3D12ResourcePtr, ResourceDesc, ResourceHandle))
				{
					CoreObject->GetProxyData_RenderThread().ResourceHandles.Add(ResourceHandle);

					return true;
				}
			}
		}
	}

	return false;
}

bool FTextureShareResource::D3D12ReleaseTextureShareHandle()
{
	if (IsRHID3D12() && TextureRHI.IsValid())
	{
		ID3D12DynamicRHI* D3D12RHI = GetID3D12DynamicRHI();
		ID3D12Device* D3D12DevicePtr = D3D12RHI->RHIGetDevice(0);
		ID3D12Resource* D3D12ResourcePtr = (ID3D12Resource*)D3D12RHI->RHIGetResource(TextureRHI);

		if (D3D12DevicePtr && D3D12ResourcePtr)
		{
			TSharedPtr<ITextureShareCoreD3D12ResourcesCache, ESPMode::ThreadSafe> D3D12ResourcesCache = GetD3D12ResourcesCache();
			if (D3D12ResourcesCache.IsValid())
			{
				return D3D12ResourcesCache->RemoveSharedResource(CoreObject->GetObjectDesc(), D3D12ResourcePtr);
			}
		}
	}

	return false;
}
