// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITextureShareItemD3D11.h"
#include "Core/TextureShareItemBase.h"

#if TEXTURESHARELIB_USE_D3D11
namespace TextureShareItem
{
	class FTextureShareItemD3D11
		: public FTextureShareItemBase
		, public ITextureShareItemD3D11
	{
	public:
		FTextureShareItemD3D11(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess ProcessType);
		virtual ~FTextureShareItemD3D11();

		virtual ETextureShareDevice GetDeviceType() const override
			{ return ETextureShareDevice::D3D11; }
		virtual ITextureShareItemD3D11* GetD3D11() override
			{ return this; }

		/*
		* Synchronized access to shared texture by name Lock()+Unlock()
		*/
		virtual ID3D11Texture2D* LockTexture_RenderThread(ID3D11Device* pD3D11Device, const FString& TextureName) override;
		virtual bool             UnlockTexture_RenderThread(const FString& TextureName) override;

	protected:
		virtual void DeviceReleaseTextures() override;
#if TEXTURESHARECORE_RHI
		virtual bool LockClientRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged) override;
#endif

	private:
		ID3D11Texture2D* OpenSharedResource(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData, int32 RemoteTextureIndex, bool& bIsResourceChanged);

		bool OpenSharedResource(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData, ETextureShareDevice RemoteDeviceType);
		void CloseSharedResource(FSharedResourceTexture& LocalTextureData);

		ID3D11Texture2D* Impl_LockTexture_RenderThread(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData);
		bool Impl_UnlockTexture_RenderThread(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged);

	private:
		// Shared texture container (unlock purpose)
		ID3D11Texture2D* OpenedSharedResources[MaxTextureShareItemTexturesCount];
	};
};
#endif // TEXTURESHARELIB_USE_D3D11
