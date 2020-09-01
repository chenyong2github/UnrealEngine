// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/NetSerialization.h"
#include "Serialization/BitWriter.h"
#include "Containers/BitArray.h"
#include "CharacterMovementReplication.generated.h"

class UPackageMap;
class FSavedMove_Character;
class UCharacterMovementComponent;
struct FRootMotionSourceGroup;

#ifndef SUPPORT_DEPRECATED_CHARACTER_MOVEMENT_RPCS
#define SUPPORT_DEPRECATED_CHARACTER_MOVEMENT_RPCS 0
#endif

#if SUPPORT_DEPRECATED_CHARACTER_MOVEMENT_RPCS
#define DEPRECATED_CHARACTER_MOVEMENT_RPC(...)
#else
#define DEPRECATED_CHARACTER_MOVEMENT_RPC(DeprecatedFunction, NewFunction) UE_DEPRECATED_FORGAME(4.26, #DeprecatedFunction "() is deprecated, use " #NewFunction "() instead, or define SUPPORT_DEPRECATED_CHARACTER_MOVEMENT_RPCS=1 in the project and set CVar p.NetUsePackedMovementRPCs=0 to use the old code path.")
#endif

#ifndef CHARACTER_SERIALIZATION_PACKEDBITS_RESERVED_SIZE
#define CHARACTER_SERIALIZATION_PACKEDBITS_RESERVED_SIZE 256
#endif

/**
 * Intermediate data stream used for network serialization of Character RPC data.
 * This is basically an array of bits that is packed/unpacked via NetSerialize into custom data structs on the sending and receiving ends.
 */
USTRUCT()
struct ENGINE_API FCharacterNetworkSerializationPackedBits
{
	GENERATED_USTRUCT_BODY()

	FCharacterNetworkSerializationPackedBits()
		: SavedPackageMap(nullptr)
	{
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* PackageMap, bool& bOutSuccess);
	UPackageMap* GetPackageMap() const { return SavedPackageMap; }

	//////////////////////////////////////////////////////////////////////////
	// Data

	TBitArray<TInlineAllocator<CHARACTER_SERIALIZATION_PACKEDBITS_RESERVED_SIZE>> DataBits;

private:
	UPackageMap* SavedPackageMap;
};

template<>
struct TStructOpsTypeTraits<FCharacterNetworkSerializationPackedBits> : public TStructOpsTypeTraitsBase2<FCharacterNetworkSerializationPackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};


//////////////////////////////////////////////////////////////////////////
// Client to Server movement data
//////////////////////////////////////////////////////////////////////////

struct ENGINE_API FCharacterNetworkMoveData
{
public:

	enum class ENetworkMoveType
	{
		NewMove,
		PendingMove,
		OldMove
	};

	FCharacterNetworkMoveData()
		: NetworkMoveType(ENetworkMoveType::NewMove)
		, TimeStamp(0.f)
		, Acceleration(ForceInitToZero)
		, Location(ForceInitToZero)
		, ControlRotation(ForceInitToZero)
		, CompressedMoveFlags(0)
		, MovementBase(nullptr)
		, MovementBaseBoneName(NAME_None)
		, MovementMode(0)
	{
	}
	
	virtual ~FCharacterNetworkMoveData()
	{
	}

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType);

	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType);

	// Indicates whether this was the latest new move, a pending/dual move, or old important move.
	ENetworkMoveType NetworkMoveType;

	//////////////////////////////////////////////////////////////////////////
	// Basic movement data.

	float TimeStamp;
	FVector_NetQuantize10 Acceleration;
	FVector_NetQuantize100 Location;		// Either world location or relative to MovementBase if that is set.
	FRotator ControlRotation;
	uint8 CompressedMoveFlags;

	class UPrimitiveComponent* MovementBase;
	FName MovementBaseBoneName;
	uint8 MovementMode;
};


/**
 * Struct used for network RPC parameters between client/server by ACharacter and UCharacterMovementComponent.
 * Overriding Serialize() allows for addition of custom fields by derived implementations.
 * 
 * @see UCharacterMovementComponent::SetNetworkMoveDataContainer()
 */
struct ENGINE_API FCharacterNetworkMoveDataContainer
{
public:

	// Default constructor. Sets data storage (NewMoveData, PendingMoveData, OldMoveData) to point to default data members. Override those pointers to point to custom data if you want to use derived classes.
	FCharacterNetworkMoveDataContainer()
		: bHasPendingMove(false)
		, bIsDualHybridRootMotionMove(false)
		, bHasOldMove(false)
		, bDisableCombinedScopedMove(false)
	{
		NewMoveData		= &BaseDefaultMoveData[0];
		PendingMoveData	= &BaseDefaultMoveData[1];
		OldMoveData		= &BaseDefaultMoveData[2];
	}

	virtual ~FCharacterNetworkMoveDataContainer()
	{
	}

	// Passes through calls to ClientFillNetworkMoveData on each FCharacterNetworkMoveData matching the client moves. Note that ClientNewMove will never be null, but others may be.
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove, const FSavedMove_Character* ClientOldMove);

	// Serialize movement data. Passes Serialize calls to each FCharacterNetworkMoveData as applicable, based on bHasPendingMove and bHasOldMove.
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);

	//////////////////////////////////////////////////////////////////////////
	// Basic movement data. NewMoveData is the most recent move, PendingMoveData is a move right before it (dual move). OldMoveData is an "important" move not yet acknowledged.

	FORCEINLINE FCharacterNetworkMoveData* GetNewMoveData() const		{ return NewMoveData; }
	FORCEINLINE FCharacterNetworkMoveData* GetPendingMoveData() const	{ return PendingMoveData; }
	FORCEINLINE FCharacterNetworkMoveData* GetOldMoveData() const		{ return OldMoveData; }

	//////////////////////////////////////////////////////////////////////////
	// Optional pending data used in "dual moves".
	bool bHasPendingMove;
	bool bIsDualHybridRootMotionMove;
	
	// Optional "old move" data, for redundant important old moves not yet ack'd.
	bool bHasOldMove;

	// True if we want to disable a scoped move around both dual moves (optional from bEnableServerDualMoveScopedMovementUpdates), typically set if bForceNoCombine was true which can indicate an important change in moves.
	bool bDisableCombinedScopedMove;
	
protected:

	FCharacterNetworkMoveData* NewMoveData;
	FCharacterNetworkMoveData* PendingMoveData;	// Only valid if bHasPendingMove is true
	FCharacterNetworkMoveData* OldMoveData;		// Only valid if bHasOldMove is true

private:

	FCharacterNetworkMoveData BaseDefaultMoveData[3];
};


USTRUCT()
struct ENGINE_API FCharacterServerMovePackedBits : public FCharacterNetworkSerializationPackedBits
{
	GENERATED_USTRUCT_BODY()
	FCharacterServerMovePackedBits() {}
};

template<>
struct TStructOpsTypeTraits<FCharacterServerMovePackedBits> : public TStructOpsTypeTraitsBase2<FCharacterServerMovePackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};


//////////////////////////////////////////////////////////////////////////
// Server to Client response
//////////////////////////////////////////////////////////////////////////

// ClientAdjustPosition replication (event called at end of frame by server)
struct ENGINE_API FClientAdjustment
{
public:

	FClientAdjustment()
		: TimeStamp(0.f)
		, DeltaTime(0.f)
		, NewLoc(ForceInitToZero)
		, NewVel(ForceInitToZero)
		, NewRot(ForceInitToZero)
		, NewBase(NULL)
		, NewBaseBoneName(NAME_None)
		, bAckGoodMove(false)
		, bBaseRelativePosition(false)
		, MovementMode(0)
	{
	}

	float TimeStamp;
	float DeltaTime;
	FVector NewLoc;
	FVector NewVel;
	FRotator NewRot;
	UPrimitiveComponent* NewBase;
	FName NewBaseBoneName;
	bool bAckGoodMove;
	bool bBaseRelativePosition;
	uint8 MovementMode;
};


struct ENGINE_API FCharacterMoveResponseDataContainer
{
public:

	FCharacterMoveResponseDataContainer()
		: bHasBase(false)
		, bHasRotation(false)
		, bRootMotionMontageCorrection(false)
		, bRootMotionSourceCorrection(false)
		, RootMotionTrackPosition(-1.0f)
		, RootMotionRotation(ForceInitToZero)
	{
	}

	virtual ~FCharacterMoveResponseDataContainer()
	{
	}

	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment);

	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);

	bool IsGoodMove() const		{ return ClientAdjustment.bAckGoodMove;}
	bool IsCorrection() const	{ return !IsGoodMove(); }

	FRootMotionSourceGroup* GetRootMotionSourceGroup(UCharacterMovementComponent& CharacterMovement) const;

	bool bHasBase;
	bool bHasRotation; // By default ClientAdjustment.NewRot is not serialized. Set this to true after base ServerFillResponseData if you want Rotation to be serialized.
	bool bRootMotionMontageCorrection;
	bool bRootMotionSourceCorrection;

	// Client adjustment. All data other than bAckGoodMove and TimeStamp is only valid if this is a correction (not an ack).
	FClientAdjustment ClientAdjustment;

	float RootMotionTrackPosition;
	FVector_NetQuantizeNormal RootMotionRotation;
};


USTRUCT()
struct ENGINE_API FCharacterMoveResponsePackedBits : public FCharacterNetworkSerializationPackedBits
{
	GENERATED_USTRUCT_BODY()
	FCharacterMoveResponsePackedBits() {}
};

template<>
struct TStructOpsTypeTraits<FCharacterMoveResponsePackedBits> : public TStructOpsTypeTraitsBase2<FCharacterMoveResponsePackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};
