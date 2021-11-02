// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITextureShareItemD3D12.h"
#include "Core/TextureShareItemBase.h"

#if TEXTURESHARELIB_USE_D3D12
namespace TextureShareItem
{
	class FTextureShareItemD3D12
		: public FTextureShareItemBase
		, public ITextureShareItemD3D12
	{
	public:
		FTextureShareItemD3D12(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess ProcessType);
		virtual ~FTextureShareItemD3D12();

		virtual ETextureShareDevice GetDeviceType() const override
			{ return ETextureShareDevice::D3D12; }
		virtual ITextureShareItemD3D12* GetD3D12() override
			{ return this; }

		/** Synchronized access to shared texture by name Lock()+Unlock() */
		virtual ID3D12Resource*  LockTexture_RenderThread(ID3D12Device* pD3D12Device, const FString& TextureName) override;
		virtual bool             UnlockTexture_RenderThread(const FString& TextureName) override;

	protected:
		virtual void DeviceReleaseTextures() override;
#if TEXTURESHARECORE_RHI
		virtual bool LockClientRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged) override;
#endif

	private:
		ID3D12Resource* OpenSharedResource(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData, int32 RemoteTextureIndex, bool& bIsResourceChanged);

		void CloseSharedResource(FSharedResourceTexture& LocalTextureData);
		bool OpenSharedResource(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData, ETextureShareDevice RemoteDeviceType);

		ID3D12Resource* Impl_LockTexture_RenderThread(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData);
		bool Impl_UnlockTexture_RenderThread(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged);

	private:
		// Shared texture container (unlock purpose)
		ID3D12Resource* OpenedSharedResources[MaxTextureShareItemTexturesCount];
	};
};
#endif // TEXTURESHARELIB_USE_D3D12
