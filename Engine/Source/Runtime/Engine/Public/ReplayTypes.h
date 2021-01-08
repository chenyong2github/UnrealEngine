// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/EngineVersion.h"
#include "Misc/NetworkGuid.h"
#include "Misc/NetworkVersion.h"
#include "Net/Common/Packets/PacketTraits.h"
#include "IPAddress.h"
#include "Serialization/BitReader.h"
#include "ReplayTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDemo, Log, All);

class UNetConnection;

enum class EReplayHeaderFlags : uint32
{
	None = 0,
	ClientRecorded = (1 << 0),
	HasStreamingFixes = (1 << 1),
	DeltaCheckpoints = (1 << 2),
	GameSpecificFrameData = (1 << 3),
	ReplayConnection = (1 << 4),
};

ENUM_CLASS_FLAGS(EReplayHeaderFlags);

const TCHAR* LexToString(EReplayHeaderFlags Flag);

enum class EWriteDemoFrameFlags : uint32
{
	None = 0,
	SkipGameSpecific = (1 << 0),
};

ENUM_CLASS_FLAGS(EWriteDemoFrameFlags);

struct FPlaybackPacket
{
	TArray<uint8>		Data;
	float				TimeSeconds;
	int32				LevelIndex;
	uint32				SeenLevelIndex;

	void CountBytes(FArchive& Ar) const
	{
		Data.CountBytes(Ar);
	}
};

USTRUCT()
struct FLevelNameAndTime
{
	GENERATED_BODY()

	FLevelNameAndTime()
		: LevelChangeTimeInMS(0)
	{}

	FLevelNameAndTime(const FString& InLevelName, uint32 InLevelChangeTimeInMS)
		: LevelName(InLevelName)
		, LevelChangeTimeInMS(InLevelChangeTimeInMS)
	{}

	UPROPERTY()
	FString LevelName;

	UPROPERTY()
	uint32 LevelChangeTimeInMS;

	friend FArchive& operator<<(FArchive& Ar, FLevelNameAndTime& LevelNameAndTime)
	{
		Ar << LevelNameAndTime.LevelName;
		Ar << LevelNameAndTime.LevelChangeTimeInMS;
		return Ar;
	}

	void CountBytes(FArchive& Ar) const
	{
		LevelName.CountBytes(Ar);
	}
};

enum ENetworkVersionHistory
{
	HISTORY_REPLAY_INITIAL = 1,
	HISTORY_SAVE_ABS_TIME_MS = 2,			// We now save the abs demo time in ms for each frame (solves accumulation errors)
	HISTORY_INCREASE_BUFFER = 3,			// Increased buffer size of packets, which invalidates old replays
	HISTORY_SAVE_ENGINE_VERSION = 4,			// Now saving engine net version + InternalProtocolVersion
	HISTORY_EXTRA_VERSION = 5,			// We now save engine/game protocol version, checksum, and changelist
	HISTORY_MULTIPLE_LEVELS = 6,			// Replays support seamless travel between levels
	HISTORY_MULTIPLE_LEVELS_TIME_CHANGES = 7,			// Save out the time that level changes happen
	HISTORY_DELETED_STARTUP_ACTORS = 8,			// Save DeletedNetStartupActors inside checkpoints
	HISTORY_HEADER_FLAGS = 9,			// Save out enum flags with demo header
	HISTORY_LEVEL_STREAMING_FIXES = 10,			// Optional level streaming fixes.
	HISTORY_SAVE_FULL_ENGINE_VERSION = 11,			// Now saving the entire FEngineVersion including branch name
	HISTORY_HEADER_GUID = 12,			// Save guid to demo header
	HISTORY_CHARACTER_MOVEMENT = 13,			// Change to using replicated movement and not interpolation
	HISTORY_CHARACTER_MOVEMENT_NOINTERP = 14,			// No longer recording interpolated movement samples
	HISTORY_GUID_NAMETABLE = 15,

	// -----<new versions can be added before this line>-------------------------------------------------
	HISTORY_PLUS_ONE,
	HISTORY_LATEST = HISTORY_PLUS_ONE - 1
};

static const uint32 MIN_SUPPORTED_VERSION = HISTORY_EXTRA_VERSION;

static const uint32 NETWORK_DEMO_MAGIC = 0x2CF5A13D;
static const uint32 NETWORK_DEMO_VERSION = HISTORY_LATEST;
static const uint32 MIN_NETWORK_DEMO_VERSION = HISTORY_EXTRA_VERSION;

static const uint32 NETWORK_DEMO_METADATA_MAGIC = 0x3D06B24E;
static const uint32 NETWORK_DEMO_METADATA_VERSION = 0;

struct FNetworkDemoHeader
{
	uint32	Magic;									// Magic to ensure we're opening the right file.
	uint32	Version;								// Version number to detect version mismatches.
	uint32	NetworkChecksum;						// Network checksum
	uint32	EngineNetworkProtocolVersion;			// Version of the engine internal network format
	uint32	GameNetworkProtocolVersion;				// Version of the game internal network format
	FGuid	Guid;									// Unique identifier

	FEngineVersion EngineVersion;					// Full engine version on which the replay was recorded
	EReplayHeaderFlags HeaderFlags;					// Replay flags
	TArray<FLevelNameAndTime> LevelNamesAndTimes;	// Name and time changes of levels loaded for demo
	TArray<FString> GameSpecificData;				// Area for subclasses to write stuff

	FNetworkDemoHeader() :
		Magic(NETWORK_DEMO_MAGIC),
		Version(NETWORK_DEMO_VERSION),
		NetworkChecksum(FNetworkVersion::GetLocalNetworkVersion()),
		EngineNetworkProtocolVersion(FNetworkVersion::GetEngineNetworkProtocolVersion()),
		GameNetworkProtocolVersion(FNetworkVersion::GetGameNetworkProtocolVersion()),
		Guid(),
		EngineVersion(FEngineVersion::Current()),
		HeaderFlags(EReplayHeaderFlags::None)
	{
	}

	friend FArchive& operator << (FArchive& Ar, FNetworkDemoHeader& Header)
	{
		Ar << Header.Magic;

		// Check magic value
		if (Header.Magic != NETWORK_DEMO_MAGIC)
		{
			UE_LOG(LogDemo, Error, TEXT("Header.Magic != NETWORK_DEMO_MAGIC"));
			Ar.SetError();
			return Ar;
		}

		Ar << Header.Version;

		// Check version
		if (Header.Version < MIN_NETWORK_DEMO_VERSION)
		{
			UE_LOG(LogDemo, Error, TEXT("Header.Version < MIN_NETWORK_DEMO_VERSION. Header.Version: %i, MIN_NETWORK_DEMO_VERSION: %i"), Header.Version, MIN_NETWORK_DEMO_VERSION);
			Ar.SetError();
			return Ar;
		}

		Ar << Header.NetworkChecksum;
		Ar << Header.EngineNetworkProtocolVersion;
		Ar << Header.GameNetworkProtocolVersion;

		if (Header.Version >= HISTORY_HEADER_GUID)
		{
			Ar << Header.Guid;
		}

		if (Header.Version >= HISTORY_SAVE_FULL_ENGINE_VERSION)
		{
			Ar << Header.EngineVersion;
		}
		else
		{
			// Previous versions only stored the changelist
			uint32 Changelist = 0;
			Ar << Changelist;

			if (Ar.IsLoading())
			{
				// We don't have any valid information except the changelist.
				Header.EngineVersion.Set(0, 0, 0, Changelist, FString());
			}
		}

		if (Header.Version < HISTORY_MULTIPLE_LEVELS)
		{
			FString LevelName;
			Ar << LevelName;
			Header.LevelNamesAndTimes.Add(FLevelNameAndTime(LevelName, 0));
		}
		else if (Header.Version == HISTORY_MULTIPLE_LEVELS)
		{
			TArray<FString> LevelNames;
			Ar << LevelNames;

			for (const FString& LevelName : LevelNames)
			{
				Header.LevelNamesAndTimes.Add(FLevelNameAndTime(LevelName, 0));
			}
		}
		else
		{
			Ar << Header.LevelNamesAndTimes;
		}

		if (Header.Version >= HISTORY_HEADER_FLAGS)
		{
			Ar << Header.HeaderFlags;
		}

		Ar << Header.GameSpecificData;

		return Ar;
	}

	void CountBytes(FArchive& Ar) const
	{
		LevelNamesAndTimes.CountBytes(Ar);
		for (const FLevelNameAndTime& LevelNameAndTime : LevelNamesAndTimes)
		{
			LevelNameAndTime.CountBytes(Ar);
		}

		GameSpecificData.CountBytes(Ar);
		for (const FString& Datum : GameSpecificData)
		{
			Datum.CountBytes(Ar);
		}
	}
};

// The type we use to store offsets in the archive
typedef int64 FArchivePos;

struct FDeltaCheckpointData
{
	/** Net startup actors that were destroyed */
	TSet<FString> DestroyedNetStartupActors;
	/** Destroyed dynamic actors that were active in the previous checkpoint */
	TSet<FNetworkGUID> DestroyedDynamicActors;
	/** Channels closed that were open in the previous checkpoint, and the reason why */
	TMap<FNetworkGUID, EChannelCloseReason> ChannelsToClose;

	void CountBytes(FArchive& Ar) const
	{
		DestroyedNetStartupActors.CountBytes(Ar);
		DestroyedDynamicActors.CountBytes(Ar);
		ChannelsToClose.CountBytes(Ar);
	}
};

class FRepActorsCheckpointParams
{
public:
	const double StartCheckpointTime;
	const double CheckpointMaxUploadTimePerFrame;
};

struct FQueuedDemoPacket
{
	/** The packet data to send */
	TArray<uint8> Data;

	/** The size of the packet in bits */
	int32 SizeBits;

	/** The traits applied to the packet, if applicable */
	FOutPacketTraits Traits;

	/** Index of the level this packet is associated with. 0 indicates no association. */
	uint32 SeenLevelIndex;

public:
	FQueuedDemoPacket(uint8* InData, int32 InSizeBytes, int32 InSizeBits)
		: Data()
		, SizeBits(InSizeBits)
		, Traits()
		, SeenLevelIndex(0)
	{
		Data.AddUninitialized(InSizeBytes);
		FMemory::Memcpy(Data.GetData(), InData, InSizeBytes);
	}

	FQueuedDemoPacket(uint8* InData, int32 InSizeBits, FOutPacketTraits& InTraits)
		: Data()
		, SizeBits(InSizeBits)
		, Traits(InTraits)
		, SeenLevelIndex(0)
	{
		int32 SizeBytes = FMath::DivideAndRoundUp(InSizeBits, 8);

		Data.AddUninitialized(SizeBytes);
		FMemory::Memcpy(Data.GetData(), InData, SizeBytes);
	}

	void CountBytes(FArchive& Ar) const
	{
		Data.CountBytes(Ar);
	}
};

/*------------------------------------------------------------------------------------------
	FInternetAddrDemo - dummy internet addr that can be used for anything that requests it.
--------------------------------------------------------------------------------------------*/
class FInternetAddrDemo : public FInternetAddr
{
public:

	FInternetAddrDemo()
	{
	}

	virtual TArray<uint8> GetRawIp() const override
	{
		return TArray<uint8>();
	}

	virtual void SetRawIp(const TArray<uint8>& RawAddr) override
	{
	}

	void SetIp(uint32 InAddr) override
	{
	}


	void SetIp(const TCHAR* InAddr, bool& bIsValid) override
	{
	}

	void GetIp(uint32& OutAddr) const override
	{
		OutAddr = 0;
	}

	void SetPort(int32 InPort) override
	{
	}


	void GetPort(int32& OutPort) const override
	{
		OutPort = 0;
	}


	int32 GetPort() const override
	{
		return 0;
	}

	void SetAnyAddress() override
	{
	}

	void SetBroadcastAddress() override
	{
	}

	void SetLoopbackAddress() override
	{
	}

	FString ToString(bool bAppendPort) const override
	{
		return FString(TEXT("Demo Internet Address"));
	}

	virtual bool operator==(const FInternetAddr& Other) const override
	{
		return Other.ToString(true) == ToString(true);
	}

	bool operator!=(const FInternetAddrDemo& Other) const
	{
		return !(FInternetAddrDemo::operator==(Other));
	}

	virtual uint32 GetTypeHash() const override
	{
		return GetConstTypeHash();
	}

	uint32 GetConstTypeHash() const
	{
		return ::GetTypeHash(ToString(true));
	}

	friend uint32 GetTypeHash(const FInternetAddrDemo& A)
	{
		return A.GetConstTypeHash();
	}

	virtual bool IsValid() const override
	{
		return true;
	}

	virtual TSharedRef<FInternetAddr> Clone() const override
	{
		return DemoInternetAddr.ToSharedRef();
	}

	static TSharedPtr<FInternetAddr> DemoInternetAddr;
};

class FScopedForceUnicodeInArchive
{
public:
	FScopedForceUnicodeInArchive(FScopedForceUnicodeInArchive&&) = delete;
	FScopedForceUnicodeInArchive(const FScopedForceUnicodeInArchive&) = delete;
	FScopedForceUnicodeInArchive& operator=(const FScopedForceUnicodeInArchive&) = delete;
	FScopedForceUnicodeInArchive& operator=(FScopedForceUnicodeInArchive&&) = delete;

	FScopedForceUnicodeInArchive(FArchive& InArchive)
		: Archive(InArchive)
		, bWasUnicode(InArchive.IsForcingUnicode())
	{
		EnableFastStringSerialization();
	}

	~FScopedForceUnicodeInArchive()
	{
		RestoreStringSerialization();
	}

private:
	void EnableFastStringSerialization()
	{
		if (FPlatformString::TAreEncodingsCompatible<WIDECHAR, TCHAR>::Value)
		{
			Archive.SetForceUnicode(true);
		}
	}

	void RestoreStringSerialization()
	{
		if (FPlatformString::TAreEncodingsCompatible<WIDECHAR, TCHAR>::Value)
		{
			Archive.SetForceUnicode(bWasUnicode);
		}
	}

	FArchive& Archive;
	bool bWasUnicode;
};

/**
 * Helps track Offsets in an Archive before the actual size of the offset is known.
 * This relies on serialization always used a fixed number of bytes for primitive types,
 * and Sane implementations of Seek and Tell.
 */
class FScopedStoreArchiveOffset
{
public:
	FScopedStoreArchiveOffset(FScopedStoreArchiveOffset&&) = delete;
	FScopedStoreArchiveOffset(const FScopedStoreArchiveOffset&) = delete;
	FScopedStoreArchiveOffset& operator=(const FScopedStoreArchiveOffset&) = delete;
	FScopedStoreArchiveOffset& operator=(FScopedStoreArchiveOffset&&) = delete;

	FScopedStoreArchiveOffset(FArchive& InAr) :
		Ar(InAr),
		StartPosition(Ar.Tell())
	{
		// Save room for the offset here.
		FArchivePos TempOffset = 0;
		Ar << TempOffset;
	}

	~FScopedStoreArchiveOffset()
	{
		const FArchivePos CurrentPosition = Ar.Tell();
		FArchivePos Offset = CurrentPosition - (StartPosition + sizeof(FArchivePos));
		Ar.Seek(StartPosition);
		Ar << Offset;
		Ar.Seek(CurrentPosition);
	}

private:

	FArchive& Ar;
	const FArchivePos StartPosition;
};

class FReplayExternalData
{
public:
	FReplayExternalData() : TimeSeconds(0.0f)
	{
	}

	FReplayExternalData(FBitReader&& InReader, const float InTimeSeconds) : TimeSeconds(InTimeSeconds)
	{
		Reader = MoveTemp(InReader);
	}

	FBitReader	Reader;
	float		TimeSeconds;

	void CountBytes(FArchive& Ar) const
	{
		Reader.CountMemory(Ar);
	}
};

// Using an indirect array here since FReplayExternalData stores an FBitReader, and it's not safe to store an FArchive directly in a TArray.
typedef TIndirectArray<FReplayExternalData> FReplayExternalDataArray;

// Helps manage packets, and any associations with streaming levels or exported GUIDs / fields.
class UE_DEPRECATED(4.26, "No longer used") FScopedPacketManager
{
public:
	FScopedPacketManager(FScopedPacketManager&&) = delete;
	FScopedPacketManager(const FScopedPacketManager&) = delete;
	FScopedPacketManager& operator=(const FScopedPacketManager&) = delete;
	FScopedPacketManager& operator=(FScopedPacketManager&&) = delete;

	FScopedPacketManager(UNetConnection& InConnection, TArray<FQueuedDemoPacket>& InPackets, const uint32 InSeenLevelIndex);
	~FScopedPacketManager();

private:
	void AssociatePacketsWithLevel();

	UNetConnection& Connection;
	TArray<FQueuedDemoPacket>& Packets;
	const uint32 SeenLevelIndex;
	int32 StartPacketCount;
};