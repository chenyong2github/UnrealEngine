// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GPUTextureTransfer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#endif

#include "DVPAPI.h"

#if PLATFORM_WINDOWS
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::GPUTextureTransfer::Private
{
	class FTextureTransferBase : public ITextureTransfer
	{
	public:
		virtual ~FTextureTransferBase() {};

		//~ Begin ITextureTransfer interface
		virtual bool BeginSync(void* InBuffer, ETransferDirection TransferDirection) override;
		virtual void EndSync(void* InBuffer) override;
		virtual bool TransferTexture(void* InBuffer, void* InRHITexture, ETransferDirection TransferDirection) override;
		virtual void RegisterBuffer(const FRegisterDMABufferArgs& Args) override;
		virtual void UnregisterBuffer(void* InBuffer) override;
		virtual void RegisterTexture(const FRegisterDMATextureArgs& Args) override;
		virtual void UnregisterTexture(void* InRHITexture) override;
		virtual void LockTexture(void* InRHITexture) override;
		virtual void UnlockTexture(void* InRHITexture) override;
		// End ITextureTransfer interface

	protected:
		virtual bool Initialize(const FInitializeDMAArgs& Args) override;
		virtual bool Uninitialize() override;

		struct DVPSync
		{
			DVPSync(uint32 SemaphoreAllocSize, uint32 SemaphoreAlignment);
			~DVPSync();

			volatile uint32* Semaphore = nullptr;
			volatile uint32 ReleaseValue = 0;
			volatile uint32 AcquireValue = 0;
			DVPSyncObjectHandle DVPSyncObject = 0;

			void SetValue(uint32 Value);
		};

		struct FExternalBufferInfo
		{
			uint32 Width = 0;
			uint32 Height = 0;
			uint32 Stride = 0;
			TUniquePtr<DVPSync> SystemMemorySync;
			TUniquePtr<DVPSync> GPUMemorySync;
			DVPBufferHandle DVPHandle = 0;
		};

		struct FTextureInfo
		{
			DVPBufferHandle DVPHandle = 0;
			union 
			{
				void* Handle = nullptr;
				int Fd;
			} External; // D3D12 and Vulkan only
		};

	protected:
		virtual DVPStatus Init_Impl(const FInitializeDMAArgs& InArgs) = 0;
		virtual DVPStatus GetConstants_Impl(uint32* OutBufferAddrAlignment, uint32* OutBufferGPUStrideAlignment, uint32* OutSemaphoreAddrAlignment, uint32* OutSemaphoreAllocSize, uint32* OutSemaphorePayloadOffset, uint32* OutSemaphorePayloadSize) const = 0;
		virtual DVPStatus BindBuffer_Impl(DVPBufferHandle InBufferHandle) const = 0;
		virtual DVPStatus CreateGPUResource_Impl(void* InTexture, FTextureInfo* OutTextureInfo) const = 0;
		virtual DVPStatus UnbindBuffer_Impl(DVPBufferHandle InBufferHandle) const = 0;
		virtual DVPStatus CloseDevice_Impl() const = 0;
		virtual DVPStatus MapBufferWaitAPI_Impl(DVPBufferHandle InHandle) const;
		virtual DVPStatus MapBufferEndAPI_Impl(DVPBufferHandle InHandle) const;

		
	private:
		void ClearBufferInfo(FExternalBufferInfo& BufferInfo);
		void ClearRegisteredTextures();
		void ClearRegisteredBuffers();
	private:

		bool bInitialized = false;
		TMap<void*, FExternalBufferInfo> RegisteredBuffers;
		TMap<void*, FTextureInfo> RegisteredTextures;

		uint32 BufferAddressAlignment = 0;
		uint32 BufferGpuStrideAlignment = 0;
		uint32 SemaphoreAddressAlignment = 0;
		uint32 SemaphoreAllocSize = 0;
		uint32 SemaphorePayloadOffset = 0;
		uint32 SemaphorePayloadSize = 0;

		FCriticalSection CriticalSection;
	};
}
