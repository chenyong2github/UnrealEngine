// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureShareCoreEnums.h"

#if __UNREAL__
#include "CoreMinimal.h"
#endif

struct FTextureShareSurfaceDesc
{
	uint32  PlatformFormat  = 0; /** DXGIFormat value */
	uint32  PixelFormat = 0;     /** EPixelFormat value (valid only for unreal process) */
	uint32  Width = 0;
	uint32  Height = 0;

	bool IsValid() const
	{
		return IsSizeValid() && IsFormatValid();
	}

	bool IsSizeValid() const
	{
		return (Width > 0) && (Height > 0);
	}

	bool IsFormatValid() const
	{
		return (PlatformFormat != 0);
	}

	bool IsEqual(FTextureShareSurfaceDesc& t) const
	{
		return (Width == t.Width) && (Height == t.Height) && IsPlatformFormatEqual(t);
	}

	bool IsPlatformFormatEqual(FTextureShareSurfaceDesc& t) const
	{	
		if (PlatformFormat == t.PlatformFormat)
		{
			return true;
		}
		return false;
	}

	void SetSize(const FTextureShareSurfaceDesc& Src)
	{
		Width  = Src.Width;
		Height = Src.Height;
	}

	void SetFormat(const FTextureShareSurfaceDesc& Src)
	{
		PlatformFormat = Src.PlatformFormat;
		PixelFormat = Src.PixelFormat;
	}
};

struct FTextureShareSyncPolicy
{
public:
	ETextureShareSyncConnect ConnectionSync;
	ETextureShareSyncFrame   FrameSync;
	ETextureShareSyncSurface TextureSync;

	FTextureShareSyncPolicy()
		: ConnectionSync(ETextureShareSyncConnect::Default)
		, FrameSync(ETextureShareSyncFrame::Default)
		, TextureSync(ETextureShareSyncSurface::Default)
	{}

	FTextureShareSyncPolicy(ETextureShareSyncConnect InConnectionSync, ETextureShareSyncFrame InFrameSync, ETextureShareSyncSurface InTextureSync)
		: ConnectionSync(InConnectionSync)
		, FrameSync(InFrameSync)
		, TextureSync(InTextureSync)
	{}

	// Default sync policy for client & server
	FTextureShareSyncPolicy(ETextureShareProcess Process)
	{
		switch (Process)
		{
		case ETextureShareProcess::Client:
			ConnectionSync = ETextureShareSyncConnect::SyncSession;
			FrameSync      = ETextureShareSyncFrame::FrameSync;
			TextureSync    = ETextureShareSyncSurface::SyncRead;
			break;
		default:
			ConnectionSync = ETextureShareSyncConnect::None;
			FrameSync      = ETextureShareSyncFrame::FrameSync;
			TextureSync    = ETextureShareSyncSurface::SyncRead;
			break;
		}
	}

	bool operator==(const FTextureShareSyncPolicy& InSyncPolicy) const
	{
		return ConnectionSync == InSyncPolicy.ConnectionSync
			&& FrameSync == InSyncPolicy.FrameSync
			&& TextureSync == InSyncPolicy.TextureSync;
	}
};

struct FTextureShareTimeOut
{
public:
	// Wait for processes shares connection (ETextureShareSyncConnect::Sync) [Seconds, zero for infinite]
	float ConnectionSync = 0;

	// Wait for frame index sync (ETextureShareSyncFrame::Sync) [Seconds, zero for infinite]
	float FrameSync = 0;

	// Wait for remote process texture registering (ETextureShareSyncSurface::SyncPairingRead) [Seconds, zero for infinite]
	float TexturePairingSync = 0;

	// Wait for remote resource(GPU) handle ready (ETextureShareSyncFrame::Sync) [Seconds, zero for infinite]
	float TextureResourceSync = 0;

	// Wait before Read op texture until remote process finished texture write op ( ETextureShareSurfaceOp::Read || ETextureShareSyncSurface::SyncPairingRead) [Seconds, zero for infinite]
	float TextureSync = 0;

	// Wait inside texture lock\unlock
	float TextureLockMutex = 0;

	// Internal. Shared memory resource
	float InitializeSync = 1;
	float ReleaseSync = 1;
	float DiscardDataSync = 0.5f;
	float SharedMemorySync = 0.5f;

	FTextureShareTimeOut()
	{};

	// Default sync policy for client & server
	FTextureShareTimeOut(ETextureShareProcess Process)
	{
		switch (Process)
		{
		case ETextureShareProcess::Server:
			// Override special values for server(UE4) side
			ConnectionSync = 5;
			FrameSync = 3;
			TexturePairingSync = 3;
			TextureResourceSync = 3;
			TextureSync = 3;
			TextureLockMutex = 3;
			break;
		default:
			// Use default values
			break;
		}
	}
};

// Sync settings
struct FTextureShareSyncPolicySettings
{
public:
	FTextureShareSyncPolicy DefaultSyncPolicy;
	FTextureShareTimeOut    TimeOut;

	FTextureShareSyncPolicySettings()
	{};

	FTextureShareSyncPolicySettings(ETextureShareProcess Process)
		: DefaultSyncPolicy(Process)
		, TimeOut(Process)
	{}
};
