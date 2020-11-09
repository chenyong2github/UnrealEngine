// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLinkCommon.h"
#include "DirectLinkParameterStore.h"



namespace DirectLink
{


enum class ESerializationStatus
{
	Ok,
	StreamError,
	VersionMinNotRespected,
	VersionMaxNotRespected,
};


struct DIRECTLINK_API FReferenceSnapshot
{
	void Serialize(FArchive& Ar);
	FElementHash Hash() const;

	struct FReferenceGroup
	{
		FName Name;
		TArray<FSceneGraphId> ReferencedIds;
	};

	TArray<FReferenceGroup> Groups;
};



class DIRECTLINK_API FElementSnapshot
{
public:
	FElementSnapshot() = default;
	FElementSnapshot(const class ISceneGraphNode& Node);

	friend FArchive& operator<<(FArchive& Ar, FElementSnapshot& This);

	ESerializationStatus Serialize(FArchive& Ar);

	FElementHash GetHash() const;
	FElementHash GetDataHash() const; // #ue_directlink_sync: serialize hashs
	FElementHash GetRefHash() const;

public:
	FSceneGraphId NodeId;
	mutable FElementHash DataHash = InvalidHash;
	mutable FElementHash RefHash = InvalidHash;
	FParameterStoreSnapshot DataSnapshot;
	FReferenceSnapshot RefSnapshot;
};

} // namespace DirectLink
