// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if __UNREAL__
#include "CoreMinimal.h"
#endif

enum class ETextureShareFormat : uint8
{
	Undefined = 0,
	Format_DXGI,   // D3D DXGI format
	Format_EPixel  // Unreal EPixelFormat
};

enum class ETextureShareDevice : uint8
{
	Undefined = 0,
	D3D11,
	D3D12,
	Vulkan, // NOT_SUPPORTED now
	Memory  // NOT_SUPPORTED now
};

enum class ETextureShareProcess : uint8
{
	Server = 0,
	Client,
	COUNT
};

enum class ETextureShareSurfaceOp : uint8
{
	Write = 0,
	Read
};

/** Synchronize Session state events (BeginSession/EndSession) */
enum class ETextureShareSyncConnect : uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - do not wait for remote process */
	None,
	/** [SyncSession] - waiting until remote process not inside BeginSession()/EndSession() */
	SyncSession
};

/** Synchronize frame events (BeginFrame/EndFrame) */
enum class ETextureShareSyncFrame : uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - Unordered frames */
	None,
	/** [FrameSync] - waiting until remote process frame index is equal */
	FrameSync
};

/** Synchronize texture events (LockTexture/UnlockTexture) */
enum class ETextureShareSyncSurface : uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - Skip unpaired texture. Unordered read\write operations */
	None,
	/** [SyncRead] - Skip unpaired texture. Waiting until other process changed texture (readOP is wait for writeOP from remote process completed) */
	SyncRead,
	/** [SyncPairingRead] - Required texture pairing. Waiting until other process changed texture (readOP is wait for writeOP from remote process completed) */
	SyncPairingRead
};
