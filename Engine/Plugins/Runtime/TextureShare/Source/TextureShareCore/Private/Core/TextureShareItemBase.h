// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareItem.h"
#include "TextureShareItemRHI.h"
#include "IPC/SharedResource.h"

namespace TextureShareItem
{
	class FTextureShareItemBase
		: public ITextureShareItem
#if TEXTURESHARECORE_RHI
		, public FTextureShareItemRHI
#endif
	{
	public:
		FTextureShareItemBase(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess ProcessType);
		virtual ~FTextureShareItemBase()
			{ Release(); }

		virtual void Release() override;

		virtual bool IsValid() const override;
		virtual bool IsSessionValid() const override;
		virtual bool IsLocalFrameLocked() const override;

		virtual bool IsClient() const override;
		virtual const FString& GetName() const override;

		virtual bool RegisterTexture(const FString& TextureName, const FIntPoint& InSize, ETextureShareFormat InFormat, uint32 InFormatValue, ETextureShareSurfaceOp OperationType) override;

		virtual bool SetTextureGPUIndex(const FString& TextureName, uint32 GPUIndex) override;
		virtual bool SetDefaultGPUIndex(uint32 GPUIndex) override;
		virtual void SetSyncWaitTime(float InSyncWaitTime) override; // SyncWaitTime now is deprecated

		virtual bool IsLocalTextureUsed(const FString& TextureName) const override;
		virtual bool IsRemoteTextureUsed(const FString& TextureName) const override;

		/** Return remote process best fit shared texture info */
		virtual bool GetRemoteTextureDesc(const FString& TextureName, FTextureShareSurfaceDesc& OutSharedTextureDesc) const override;

		virtual bool BeginSession() override;
		virtual void EndSession() override;
		virtual bool BeginFrame_RenderThread() override;
		virtual bool EndFrame_RenderThread() override;
		virtual bool SetLocalAdditionalData(const FTextureShareAdditionalData& InAdditionalData) override;
		virtual bool GetRemoteAdditionalData(FTextureShareAdditionalData& OutAdditionalData) override;

		virtual bool SetCustomProjectionData(const FTextureShareCustomProjectionData& InCustomProjectionData) override;

		virtual ETextureShareDevice GetDeviceType() const override
			{ return ETextureShareDevice::Undefined; }
		virtual ITextureShareItemD3D11* GetD3D11() override
			{ return nullptr; }
		virtual ITextureShareItemD3D12* GetD3D12() override
			{ return nullptr; }

#if TEXTURESHARECORE_RHI
		virtual bool LockRHITexture_RenderThread(const FString& TextureName, FTexture2DRHIRef& OutRHITexture) override;
		virtual bool TransferTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName) override;
		virtual bool IsFormatResampleRequired(const FRHITexture* Texture1, const FRHITexture* Texture2) override;
#endif

	public:
		void BeginRemoteConnection();
		void EndRemoteConnection();
		void RemoteConnectionLost();
		bool CheckRemoteConnectionLost();

		bool TryFrameSyncLost();

		bool IsConnectionValid() const
			{ return ResourceData.ClientData.IsValid() && ResourceData.ServerData.IsValid(); }

		bool WriteLocalProcessData();
		bool ReadRemoteProcessData();

		/**
		* Waits the specified amount of time for the other process data changed
		*
		* A wait time of MAX_uint32 is treated as infinite wait.
		*
		* @param WaitTime The time to wait (in milliseconds).
		* @param bIgnoreThreadIdleStats If true, ignores ThreadIdleStats
		* @return true if the data was changed, false if the wait timed out.
		*/
		bool WaitForRemoteProcessDataChanged(uint32 WaitTime, const bool bIgnoreThreadIdleStats = false);

	protected:
		bool IsFrameValid() const
			{ return GetLocalProcessData().IsFrameLockedNow() && IsConnectionValid(); }

		const FTextureShareSyncPolicySettings& GetSyncSettings() const;

		ETextureShareSyncConnect GetConnectionSyncMode() const;
		ETextureShareSyncFrame   GetFrameSyncMode() const;
		ETextureShareSyncSurface GetTextureSyncMode() const;


		FSharedResourceProcessData& GetLocalProcessData();
		FSharedResourceProcessData& GetRemoteProcessData();
		const FSharedResourceProcessData& GetLocalProcessData()const;
		const FSharedResourceProcessData& GetRemoteProcessData()const;

		bool TryBeginFrame();
		bool TryTextureSync(FSharedResourceTexture& LocalTextureData, int32& OutRemoteTextureIndex);

		bool BeginTextureOp(FSharedResourceTexture& LocalTextureData); /** Sync texture R\W */
		bool LockTextureMutex(FSharedResourceTexture& LocalTextureData);
		void UnlockTextureMutex(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged);

		int32 FindTextureIndex(const FSharedResourceProcessData& Src, ESharedResourceTextureState TextureState, bool bNotEqual = false) const;
		int32 FindTextureIndex(const FSharedResourceProcessData& Src, const FString& TextureName) const;

		bool CheckTextureInfo(const FString& TextureName, const FIntPoint& InSize, ETextureShareFormat InFormat, uint32 InFormatValue) const;
		bool IsTextureUsed(bool bIsLocal, const FString& TextureName) const;

		/** Find paired remote texture index. Return -1, if texture not used */
		int32 FindRemoteTextureIndex(const FSharedResourceTexture& LocalTextureData) const;
		/** Return best fit shared texture info */
		bool GetResampledTextureDesc(bool bToLocal, const FString& TextureName, FTextureShareSurfaceDesc& OutSharedTextureDesc) const;
		/** Fill undefined values in InOutTextureDescfrom InFillerTextureDesc*/
		bool CompleteTextureDesc(FTextureShareSurfaceDesc& InOutTextureDesc, const FTextureShareSurfaceDesc& InFillerTextureDesc) const;
		/** Return texture info, requested from local or remote side */
		bool FindTextureData(const FString& TextureName, bool bIsLocal, FSharedResourceTexture& OutTextureData) const;

		virtual void DeviceReleaseTextures()
		{}
#if TEXTURESHARECORE_RHI
		bool LockServerRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged, int32 RemoteTextureIndex);
		virtual bool LockClientRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged)
			{ return false; }
#endif

	protected:
		FSharedResourceSessionData ResourceData;
		FSharedResource*           SharedResource = nullptr;
		bool                       bIsSessionStarted = false;
		bool                       bRemoteConnectionValid = false;

	protected:
		// Multithread access lock guard
		mutable FCriticalSection DataLockGuard;

	private:
		mutable FCriticalSection FrameLockGuard;

	private:
		// Define default values globally
		static FTextureShareSyncPolicySettings GSyncPolicySettings[(uint8)ETextureShareProcess::COUNT];
		static FCriticalSection GSyncPolicySettingsDataGuard;

	public:
		static const FTextureShareSyncPolicySettings& GetSyncPolicySettings(ETextureShareProcess Process);
		static void                                   SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings);
	};
};
