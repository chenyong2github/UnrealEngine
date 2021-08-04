// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/EnumClassFlags.h"

namespace UE::DerivedData
{

/**
 * Flags to control the behavior of build requests.
 *
 * The build policy flags can be combined to support a variety of usage patterns. Examples:
 *
 * Build(Default): allow cache get; build if missing; return every payload.
 * Build(Build | CacheStore): never query the cache; always build; return every payload.
 * Build(Cache | CacheSkipData): allow cache get; never build; skip every payload.
 *
 * BuildPayload(Default): allow cache get; build if missing; return one payload.
 * BuildPayload(Build | CacheStore): never get from the cache; always build; return one payload.
 * BuildPayload(Cache | CacheSkipData): allow cache get; never build; skip every payload.
 */
enum class EBuildPolicy : uint8
{
	/** A value without any flags set. */
	None            = 0,

	/** Allow local execution of the build function. */
	BuildLocal      = 1 << 0,
	/** Allow remote execution of the build function if it has a registered build worker. */
	BuildRemote     = 1 << 1,
	/** Allow local and remote execution of the build function. */
	Build           = BuildLocal | BuildRemote,

	/** Allow a cache query to avoid having to build. */
	CacheQuery      = 1 << 2,
	/** Allow a cache store to persist the build output. */
	CacheStore      = 1 << 3,
	/** Allow a cache query and a cache store for the build. */
	Cache           = CacheQuery | CacheStore,

	/** Skip fetching the payload data from the cache. */
	SkipData        = 1 << 4,

	/** Allow cache query+store, allow local+remote build when missed or skipped, and fetch the payload(s). */
	Default         = Build | Cache,
};

ENUM_CLASS_FLAGS(EBuildPolicy);

/** Flags for build request completion callbacks. */
enum class EBuildStatus : uint32
{
	/** A value without any flags set. */
	None            = 0,

	/** The build function was executed locally. */
	BuildLocal      = 1 << 0,
	/** The build function was executed remotely. */
	BuildRemote     = 1 << 1,
	/** The build action and inputs were exported. */
	BuildExport     = 1 << 2,

	/** An attempt was made to execute the build function remotely. */
	BuildTryRemote  = 1 << 3,
	/** An attempt was made to export the build action and inputs. */
	BuildTryExport  = 1 << 4,

	/** The build made a cache query request. */
	CacheQuery      = 1 << 5,
	/** Valid build output was found in the cache. */
	CacheQueryHit   = 1 << 6,

	/** The build made a cache store request. */
	CacheStore      = 1 << 7,
	/** Valid build output was stored in the cache. */
	CacheStoreHit   = 1 << 8,
};

ENUM_CLASS_FLAGS(EBuildStatus);

} // UE::DerivedData
