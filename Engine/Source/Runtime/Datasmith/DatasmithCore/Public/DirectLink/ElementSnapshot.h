// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/ParameterStore.h"

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"



namespace DirectLink
{

class ISceneGraphNode;
class FParameterStore;

struct FReferenceSnapshot
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


class DATASMITHCORE_API FElementSnapshot
{
public:
	FElementSnapshot() = default;
	FElementSnapshot(const ISceneGraphNode& Node);

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
