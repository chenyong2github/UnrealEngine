// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareItemD3D12.h"
#include "TextureShareCoreLog.h"

#if TEXTURESHARELIB_USE_D3D12
#include "TextureShareDirectX.h"

#if TEXTURESHARECORE_RHI
#include "ITextureShareD3D12.h"
#endif /*TEXTURESHARECORE_RHI*/

namespace TextureShareItem
{
	static bool D3D11OpenSharedResource(ID3D12Device* pD3D12Device, HANDLE SharedHandle, ID3D12Resource** OutSharedTexture)
	{
		CHECK_HR_DEFAULT(pD3D12Device->OpenSharedHandle(SharedHandle, __uuidof(ID3D12Resource), (LPVOID*)OutSharedTexture));
		return true;
	}

	static bool D3D12OpenSharedResource(ID3D12Device* pD3D12Device, void* SharedHandle, const FString& SharedResourceName, ETextureShareSurfaceOp OperationType, ID3D12Resource** OutSharedTexture)
	{
		HANDLE NamedResourceHandle;
		CHECK_HR_DEFAULT(pD3D12Device->OpenSharedHandleByName(*SharedResourceName, GENERIC_ALL, &NamedResourceHandle));
		CHECK_HR_DEFAULT(pD3D12Device->OpenSharedHandle(NamedResourceHandle, __uuidof(ID3D12Resource), (LPVOID*)OutSharedTexture));
		CloseHandle(NamedResourceHandle);
		return true;
	}

	/*
	 * FTextureShareItemD3D12
	 */
	bool FTextureShareItemD3D12::OpenSharedResource(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData, ETextureShareDevice RemoteDeviceType)
	{
		check(OpenedSharedResources[LocalTextureData.Index] == nullptr)
		HANDLE LocalSharedHandle = LocalTextureData.SharingData.SharedHandle;

		switch (RemoteDeviceType)
		{
		case ETextureShareDevice::D3D11: // DX11 -> DX12,
		{
			// DX11 -> DX11 : Try open DX11 shared handle
			if (D3D11OpenSharedResource(pD3D12Device, LocalTextureData.SharingData.SharedHandle, &OpenedSharedResources[LocalTextureData.Index]))
			{
				return true;
			}
			break;
		}
		case ETextureShareDevice::D3D12: // DX12 -> DX12 : Try open DX12 shared NT handle
		{
			FString UniqueNTHandleId = FString::Printf(TEXT("Global\\%s"), *LocalTextureData.SharingData.SharedHandleGuid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			if (D3D12OpenSharedResource(pD3D12Device, LocalTextureData.SharingData.SharedHandle, UniqueNTHandleId, LocalTextureData.OperationType, &OpenedSharedResources[LocalTextureData.Index]))
			{
				return true;
			}
			break;
		}
		default:
			// Not implemeted
			break;
		}

		OpenedSharedResources[LocalTextureData.Index] = nullptr;
		return false;
	}

	FTextureShareItemD3D12::FTextureShareItemD3D12(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess ProcessType)
		: FTextureShareItemBase(ResourceName, SyncMode, ProcessType)
	{
		for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
		{
			OpenedSharedResources[i] = nullptr;
		}
	}

	FTextureShareItemD3D12::~FTextureShareItemD3D12()
	{
		DeviceReleaseTextures();
	}

	void FTextureShareItemD3D12::DeviceReleaseTextures()
	{
		if (IsValid())
		{
			FSharedResourceProcessData& LocalData = GetLocalProcessData();
			for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
			{
				if (OpenedSharedResources[i])
				{
					OpenedSharedResources[i]->Release();
					OpenedSharedResources[i] = nullptr;
				}

				LocalData.Textures[i].ReleaseConnection();
			}

#if TEXTURESHARECORE_RHI
			//Server, only for UE4. implement via RHI:
			ReleaseSharedRHITextures();
#endif /*TEXTURESHARECORE_RHI*/

			// Publish local data
			WriteLocalProcessData();
		}
	}

	ID3D12Resource* FTextureShareItemD3D12::LockTexture_RenderThread(ID3D12Device* pD3D12Device, const FString& TextureName)
	{
		if (IsFrameValid())
		{
			FSharedResourceTexture TextureData;
			if (FindTextureData(TextureName, true, TextureData))
			{
				FSharedResourceTexture& LocalTextureData = GetLocalProcessData().Textures[TextureData.Index];
				if (BeginTextureOp(LocalTextureData))
				{
					return Impl_LockTexture_RenderThread(pD3D12Device, LocalTextureData);
				}
			}
		}
		return nullptr;
	}

	bool FTextureShareItemD3D12::UnlockTexture_RenderThread(const FString& TextureName)
	{
		if (IsFrameValid())
		{
			FSharedResourceTexture TextureData;
			if (FindTextureData(TextureName, true, TextureData))
			{
				FSharedResourceTexture& LocalTextureData = GetLocalProcessData().Textures[TextureData.Index];
				return Impl_UnlockTexture_RenderThread(LocalTextureData, true);
			}
		}
		return false;
	}

	ID3D12Resource* FTextureShareItemD3D12::OpenSharedResource(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData, int RemoteTextureIndex, bool& bIsResourceChanged)
	{
		bIsResourceChanged = false;
		if (IsClient())
		{
			// Client use shared handle from server GPU-GPU
			const FSharedResourceProcessData& RemoteData = GetRemoteProcessData();
			const FSharedResourceTexture& RemoteTextureData = RemoteData.Textures[RemoteTextureIndex];

			// Update resource:
			if (!LocalTextureData.IsSharedHandleConnected(RemoteTextureData))
			{
				// Close current handle
				CloseSharedResource(LocalTextureData);

				// Copy sharing data (signal server process to close handle)
				LocalTextureData.SharingData = RemoteTextureData.SharingData;

				// Open shared texture
				if (!OpenSharedResource(pD3D12Device, LocalTextureData, RemoteData.DeviceType))
				{
					//@todo handle error
					return nullptr;
				}

				bIsResourceChanged = true;
			}
			return OpenedSharedResources[LocalTextureData.Index];
		}
		return nullptr;
	}

#if TEXTURESHARECORE_RHI
	bool FTextureShareItemD3D12::LockClientRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged)
	{
		bIsTextureChanged = false;
		if (!IsFrameValid() || !IsClient())
		{
			return false;
		}

		//Client RHI , only for UE4. implement via RHI:
		FSharedRHITexture* SharedRHITexture = GetSharedRHITexture(LocalTextureData);
		int RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);

		auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D12Resource* LockedResource = OpenSharedResource(UE4D3DDevice, LocalTextureData, RemoteTextureIndex, bIsTextureChanged);
		if (LockedResource)
		{
			if (bIsTextureChanged)
			{
				FTexture2DRHIRef& RHIResource = SharedRHITexture->GetOpenedResource();
				RHIResource.SafeRelease();

				EPixelFormat Format = FTextureShareItemRHI::FindSharedTextureFormat(LocalTextureData.TextureDesc);
				return ITextureShareD3D12::Get().CreateRHITexture((ID3D12Resource*)LockedResource, Format, RHIResource);
			}
			return true;
		}
		// texture now not shared, wait in ready state for server side
		return false;
	}
#endif /*TEXTURESHARECORE_RHI*/

	ID3D12Resource* FTextureShareItemD3D12::Impl_LockTexture_RenderThread(ID3D12Device* pD3D12Device, FSharedResourceTexture& LocalTextureData)
	{
		ID3D12Resource* LockedResource = nullptr;
		bool bIsResourceChanged = false;
		int RemoteTextureIndex = -1;
		if (TryTextureSync(LocalTextureData, RemoteTextureIndex) && LockTextureMutex(LocalTextureData))
		{
			if (IsClient())
			{
				LockedResource = OpenSharedResource(pD3D12Device, LocalTextureData, RemoteTextureIndex, bIsResourceChanged);
			}
			else
			{
#if TEXTURESHARECORE_RHI
				if (LockServerRHITexture(LocalTextureData, bIsResourceChanged, RemoteTextureIndex))
				{
					LockedResource = (ID3D12Resource*)GetSharedRHITexture(LocalTextureData)->GetSharedResource()->GetTexture2D()->GetNativeResource();

					// Update DXGI format
					if (LockedResource && LocalTextureData.TextureDesc.IsFormatValid())
					{
						D3D12_RESOURCE_DESC Desc = LockedResource->GetDesc();
						LocalTextureData.TextureDesc.PlatformFormat = Desc.Format;
					}
				}
				else
#endif /*TEXTURESHARECORE_RHI*/
				{
					// Texture format unsupported, disable
					LocalTextureData.State = ESharedResourceTextureState::Disabled;
				}
			}

			// Publish local data
			WriteLocalProcessData();
		}

		if (!LockedResource)
		{
			// Remove lock scope
			Impl_UnlockTexture_RenderThread(LocalTextureData, false);

			// Close current handle
			CloseSharedResource(LocalTextureData);
		}
		return LockedResource;
	}

	void FTextureShareItemD3D12::CloseSharedResource(FSharedResourceTexture& LocalTextureData)
	{
		// Empty local opened resource
		if (OpenedSharedResources[LocalTextureData.Index])
		{
			OpenedSharedResources[LocalTextureData.Index]->Release();
			OpenedSharedResources[LocalTextureData.Index] = nullptr;
		}
		// Empty local shared handle
		LocalTextureData.SharingData.SharedHandle = nullptr;
	}

	bool FTextureShareItemD3D12::Impl_UnlockTexture_RenderThread(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged)
	{
		UnlockTextureMutex(LocalTextureData, bIsTextureChanged);
		return true;
	}
};
#endif // TEXTURESHARELIB_USE_D3D12
