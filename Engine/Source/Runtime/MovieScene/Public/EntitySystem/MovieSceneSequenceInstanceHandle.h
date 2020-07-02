// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "EntitySystem/MovieSceneComponentDebug.h"

namespace UE
{
namespace MovieScene
{

/** A handle to an active instance of a UMovieSceneSequence */
struct FInstanceHandle
{
	uint16 InstanceID;
	uint16 InstanceSerial;

	FInstanceHandle()
		: InstanceID(uint16(-1))
		, InstanceSerial(0)
	{}

	FInstanceHandle(uint16 InInstanceID, uint16 InInstanceSerial)
		: InstanceID(InInstanceID)
		, InstanceSerial(InInstanceSerial)
	{}

	bool IsValid() const
	{
		return InstanceID != uint16(-1);
	}

	friend bool operator==(FInstanceHandle A, FInstanceHandle B)
	{
		return A.InstanceID == B.InstanceID && A.InstanceSerial == B.InstanceSerial;
	}
	friend bool operator!=(FInstanceHandle A, FInstanceHandle B)
	{
		return !(A == B);
	}
	friend bool operator<(FInstanceHandle A, FInstanceHandle B)
	{
		return A.InstanceID < B.InstanceID;
	}
	friend uint32 GetTypeHash(FInstanceHandle In)
	{
		return uint32(In.InstanceID) | (uint32(In.InstanceSerial) >> 16);
	}
};

#if UE_MOVIESCENE_ENTITY_DEBUG
template<> struct TComponentDebugType<FInstanceHandle> { static const EComponentDebugType Type = EComponentDebugType::InstanceHandle; };
#endif


} // namespace MovieScene
} // namespace UE