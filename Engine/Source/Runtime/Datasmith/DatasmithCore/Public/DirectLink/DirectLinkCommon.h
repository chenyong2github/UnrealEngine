// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"


namespace DirectLink
{

/**
 * Node Id, aka Element Id. Represent a node within a scene.
 * As a scene has a guid, the combination guid/id must be globally unique.
 */
using FSceneGraphId = uint32;
constexpr FSceneGraphId InvalidId = 0;


using FElementHash = uint32;
constexpr FElementHash InvalidHash = 0;


using FStreamPort = uint32;
constexpr FStreamPort InvalidStreamPort = 0;

/**
 * Guid and optional name, used to designate a scene across processes without ambiguity.
 * The name is not necessary to identify a scene, but it offer a better UX
 */
struct FSceneIdentifier
{
	FSceneIdentifier() = default;

	FSceneIdentifier(const FGuid& Id, const FString& Name)
		: SceneGuid(Id)
		, DisplayName(Name)
	{}

	// Id of scene SharedState
	FGuid SceneGuid;

	// Nice user-facing name. Do not expect it to be stable or consistent.
	FString DisplayName;
};


/**
 * Data shared by all element of a given scene.
 * The scene is uniquely identified by this element.
 * Within this scene, all elements ids are unique. To ensure this property,
 * this shared state is responsible for the id attribution.
 * Id 0 is considered invalid (see InvalidId).
 */
class FSceneGraphSharedState
{
public:
	FSceneGraphId MakeId() { return ++LastElementId; }
	const FGuid& GetGuid() const { return SceneId.SceneGuid; }
	const FSceneIdentifier& GetSceneId() const { return SceneId; }

protected:
	FSceneGraphId LastElementId = InvalidId;
	FSceneIdentifier SceneId{FGuid::NewGuid(), FString()};
};


/**
 * DirectLink exchanges messages between pairs. Those versions numbers helps making sure pairs are compatible
 */
static constexpr uint8 kMagic = 0xd1; // this constant should never change, it's used as a marker in a byte stream
static constexpr uint8 kCurrentProtocolVersion = 7;
static constexpr uint8 kMinSupportedProtocolVersion = 7; // oldest supported version

enum class ESerializationStatus
{
	Ok,
	StreamError,
	VersionMinNotRespected,
	VersionMaxNotRespected,
};


/** Used by data source and destination to describe how they are discovered by remote endpoints */
enum class EVisibility
{
	Public,    // The connection point can accept connection requests from remote
	Private,   // The connection point is not expected to be contacted from a remote
};


} // namespace DirectLink
