// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareItemD3D11.h"
#include "TextureShareCoreLog.h"

#if TEXTURESHARELIB_USE_D3D11
#include "TextureShareDirectX.h"

#if TEXTURESHARECORE_RHI
#include "ITextureShareD3D11.h"
#endif /*TEXTURESHARECORE_RHI*/

#include "Misc/ScopeLock.h"

namespace TextureShareItem
{
	static bool D3D11OpenSharedResource(ID3D11Device* pD3D11Device, void* SharedHandle, ID3D11Texture2D** OutSharedTexture)
	{
		CHECK_HR_DEFAULT(pD3D11Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)OutSharedTexture));

		return true;
	}

	static bool D3D12OpenSharedResource(ID3D11Device* pD3D11Device, void* SharedHandle, const FString& SharedResourceName, ETextureShareSurfaceOp OperationType, ID3D11Texture2D** OutSharedTexture)
	{
		TRefCountPtr <ID3D11Device1> Device1;
		CHECK_HR_DEFAULT(pD3D11Device->QueryInterface(__uuidof(ID3D11Device1), (void**)Device1.GetInitReference()));

		const DWORD   dwDesiredAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
		CHECK_HR_DEFAULT(Device1->OpenSharedResourceByName(*SharedResourceName, dwDesiredAccess, __uuidof(ID3D11Texture2D), (LPVOID*)OutSharedTexture));

		return true;
	}

	/*
	 * FTextureShareItemD3D11
	 */

	bool FTextureShareItemD3D11::OpenSharedResource(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData, ETextureShareDevice RemoteDeviceType)
	{
		check(OpenedSharedResources[LocalTextureData.Index] == nullptr)

			switch (RemoteDeviceType)
			{
			case ETextureShareDevice::D3D11:
			{
				// DX11 -> DX11 : Try open DX11 shared handle
				if (D3D11OpenSharedResource(pD3D11Device, LocalTextureData.GetConnectionSharedHandle(), &OpenedSharedResources[LocalTextureData.Index]))
				{
					return true;
				}
				break;
			}
			case ETextureShareDevice::D3D12:
			{
				FString UniqueNTHandleId = FString::Printf(TEXT("Global\\%s"), *LocalTextureData.GetConnectionSharedHandleGuid().ToString(EGuidFormats::DigitsWithHyphensInBraces));
				// DX12 -> DX11 : Try open DX12 shared NT handle
				if (D3D12OpenSharedResource(pD3D11Device, LocalTextureData.GetConnectionSharedHandle(), UniqueNTHandleId, LocalTextureData.OperationType, &OpenedSharedResources[LocalTextureData.Index]))
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

	FTextureShareItemD3D11::FTextureShareItemD3D11(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess ProcessType)
		: FTextureShareItemBase(ResourceName, SyncMode, ProcessType)
	{
		for (int32 TextureIndex = 0; TextureIndex < MaxTextureShareItemTexturesCount; TextureIndex++)
		{
			OpenedSharedResources[TextureIndex] = nullptr;
		}
	}

	FTextureShareItemD3D11::~FTextureShareItemD3D11()
	{
		DeviceReleaseTextures();
	}

	void FTextureShareItemD3D11::DeviceReleaseTextures()
	{
		FScopeLock DataLock(&DataLockGuard);

		if (IsValid())
		{
			FSharedResourceProcessData& LocalData = GetLocalProcessData();
			for (int32 TextureIndex = 0; TextureIndex < MaxTextureShareItemTexturesCount; TextureIndex++)
			{
				if (OpenedSharedResources[TextureIndex])
				{
					OpenedSharedResources[TextureIndex]->Release();
					OpenedSharedResources[TextureIndex] = nullptr;
				}

				LocalData.Textures[TextureIndex].ReleaseConnection();
			}

			//Server, only for UE4. implement via RHI:
#if TEXTURESHARECORE_RHI
			ReleaseSharedRHITextures();
#endif /*TEXTURESHARECORE_RHI*/

			// Publish local data
			WriteLocalProcessData();
		}
	}

	bool FTextureShareItemD3D11::UnlockTexture_RenderThread(const FString& TextureName)
	{
		FScopeLock DataLock(&DataLockGuard);

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

	ID3D11Texture2D* FTextureShareItemD3D11::LockTexture_RenderThread(ID3D11Device* pD3D11Device, const FString& TextureName)
	{
		FScopeLock DataLock(&DataLockGuard);

		if (IsFrameValid())
		{
			FSharedResourceTexture TextureData;
			if (FindTextureData(TextureName, true, TextureData))
			{
				FSharedResourceTexture& LocalTextureData = GetLocalProcessData().Textures[TextureData.Index];
				if (BeginTextureOp(LocalTextureData))
				{
					return Impl_LockTexture_RenderThread(pD3D11Device, LocalTextureData);
				}
			}
		}
		return nullptr;
	}

	ID3D11Texture2D* FTextureShareItemD3D11::Impl_LockTexture_RenderThread(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData)
	{
		bool bIsResourceChanged;
		ID3D11Texture2D* LockedResource = nullptr;

		int32 RemoteTextureIndex;
		if (TryTextureSync(LocalTextureData, RemoteTextureIndex) && LockTextureMutex(LocalTextureData))
		{
			if (IsClient())
			{
				LockedResource = OpenSharedResource(pD3D11Device, LocalTextureData, RemoteTextureIndex, bIsResourceChanged);
			}
			else
			{
#if TEXTURESHARECORE_RHI
				if (LockServerRHITexture(LocalTextureData, bIsResourceChanged, RemoteTextureIndex))
				{
					LockedResource = (ID3D11Texture2D*)GetSharedRHITexture(LocalTextureData)->GetSharedResource()->GetTexture2D()->GetNativeResource();

					// Update Platform format
					if (LockedResource && !LocalTextureData.TextureDesc.IsFormatValid())
					{
						D3D11_TEXTURE2D_DESC Desc;
						LockedResource->GetDesc(&Desc);
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

	ID3D11Texture2D* FTextureShareItemD3D11::OpenSharedResource(ID3D11Device* pD3D11Device, FSharedResourceTexture& LocalTextureData, int32 RemoteTextureIndex, bool& bIsResourceChanged)
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
				LocalTextureData.OpenConnection(RemoteTextureData);

				// Open shared texture
				if (!OpenSharedResource(pD3D11Device, LocalTextureData, RemoteData.DeviceType))
				{
					//@todo handle error
					return nullptr;
				}

				bIsResourceChanged = true;
			}

			// Get current handle:
			return OpenedSharedResources[LocalTextureData.Index];
		}
		return nullptr;
	}

#if TEXTURESHARECORE_RHI
	bool FTextureShareItemD3D11::LockClientRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged)
	{
		FScopeLock DataLock(&DataLockGuard);

		bIsTextureChanged = false;

		if (!IsFrameValid() || !IsClient())
		{
			return false;
		}

		//Client RHI , only for UE4. implement via RHI:
		FSharedRHITexture* SharedRHITexture = GetSharedRHITexture(LocalTextureData);

		int32 RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);

		auto UE4D3DDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D11Texture2D* LockedResource = OpenSharedResource(UE4D3DDevice, LocalTextureData, RemoteTextureIndex, bIsTextureChanged);
		if (LockedResource)
		{
			if (bIsTextureChanged)
			{
				FTexture2DRHIRef& RHIResource = SharedRHITexture->GetOpenedResource();
				RHIResource.SafeRelease();

				EPixelFormat Format = FTextureShareItemRHI::FindSharedTextureFormat(LocalTextureData.TextureDesc);
				return ITextureShareD3D11::Get().CreateRHITexture((ID3D11Texture2D*)LockedResource, Format, RHIResource);
			}
			return true;
		}
		// texture now not shared, wait in ready state for server side
		return false;
	}
#endif /*TEXTURESHARECORE_RHI*/

	void FTextureShareItemD3D11::CloseSharedResource(FSharedResourceTexture& LocalTextureData)
	{
		// Empty local opened resource
		if (OpenedSharedResources[LocalTextureData.Index])
		{
			OpenedSharedResources[LocalTextureData.Index]->Release();
			OpenedSharedResources[LocalTextureData.Index] = nullptr;
		}

		// Empty local shared handle
		LocalTextureData.ReleaseConnection();
	}

	bool FTextureShareItemD3D11::Impl_UnlockTexture_RenderThread(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged)
	{
		UnlockTextureMutex(LocalTextureData, bIsTextureChanged);
		return true;
	}
};
#endif // TEXTURESHARELIB_USE_D3D11
