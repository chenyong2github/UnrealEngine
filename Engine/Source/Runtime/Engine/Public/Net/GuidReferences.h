// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/BitReader.h"
#include "Misc/NetworkGuid.h"

class FGuidReferences;

using FGuidReferencesMap = TMap<int32, FGuidReferences>;

/**
 * References to Objects (including Actors, Components, etc.) are replicated as NetGUIDs, since
 * the literal memory pointers will be different across game instances. In these cases, actual
 * replicated data for the Object will be handled elsewhere (either on its own Actor channel,
 * or on its Owning Actor's channel, as a replicated subobject).
 *
 * This class helps manage those references for specific replicated properties.
 * A FGuidReferences instance will be created for each Replicated Property that is a reference to an object.
 *
 * Guid References may also be nested in properties (like dynamic arrays), so we'll recursively track
 * those as well.
 */
class ENGINE_API FGuidReferences
{
public:
	FGuidReferences():
		NumBufferBits(0),
		Array(nullptr)
	{}

	FGuidReferences(
		FBitReader&					InReader,
		FBitReaderMark&				InMark,
		const TSet<FNetworkGUID>&	InUnmappedGUIDs,
		const TSet<FNetworkGUID>&	InMappedDynamicGUIDs,
		const int32					InParentIndex,
		const int32					InCmdIndex
	):
		ParentIndex(InParentIndex),
		CmdIndex(InCmdIndex),
		UnmappedGUIDs(InUnmappedGUIDs),
		MappedDynamicGUIDs(InMappedDynamicGUIDs),
		Array(nullptr)
	{
		NumBufferBits = InReader.GetPosBits() - InMark.GetPos();
		InMark.Copy(InReader, Buffer);
	}

	FGuidReferences(
		FGuidReferencesMap*	InArray,
		const int32			InParentIndex,
		const int32			InCmdIndex
	):
		ParentIndex(InParentIndex),
		CmdIndex(InCmdIndex),
		NumBufferBits(0),
		Array(InArray)
	{}

	~FGuidReferences();

	void CountBytes(FArchive& Ar) const
	{
		UnmappedGUIDs.CountBytes(Ar);
		MappedDynamicGUIDs.CountBytes(Ar);
		Buffer.CountBytes(Ar);

		if (Array)
		{
			Array->CountBytes(Ar);
			for (const auto& GuidReferencePair : *Array)
			{
				GuidReferencePair.Value.CountBytes(Ar);
			}
		}
	}

	/** The Property Command index of the top level property that references the GUID. */
	int32 ParentIndex;

	/** The Property Command index of the actual property that references the GUID. */
	int32 CmdIndex;

	int32 NumBufferBits;

	/** GUIDs for objects that haven't been loaded / created yet. */
	TSet<FNetworkGUID> UnmappedGUIDs;

	/** GUIDs for dynamically spawned objects that have already been created. */
	TSet<FNetworkGUID> MappedDynamicGUIDs;

	/** A copy of the last network data read related to this GUID Reference. */
	TArray<uint8> Buffer;

	/**
	 * If this FGuidReferences instance is owned by an Array Property that contains nested GUID References,
	 * then this will be a valid FGuidReferencesMap containing the nested FGuidReferences.
	 */
	FGuidReferencesMap* Array;
};