// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataRequest.h"
#include "IO/IoHash.h"
#include "Templates/Function.h"

struct FGuid;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { struct FBuildInputDataResolvedParams; }
namespace UE::DerivedData { struct FBuildInputMetaResolvedParams; }
namespace UE::DerivedData { struct FBuildKeyResolvedParams; }

namespace UE::DerivedData
{

using FBuildInputFilter = TUniqueFunction<bool (FStringView Key)>;
using FOnBuildInputDataResolved = TUniqueFunction<void (FBuildInputDataResolvedParams&& Params)>;
using FOnBuildInputMetaResolved = TUniqueFunction<void (FBuildInputMetaResolvedParams&& Params)>;
using FOnBuildKeyResolved = TUniqueFunction<void (FBuildKeyResolvedParams&& Params)>;

/** Metadata for build inputs with the input key. */
struct FBuildInputMetaByKey
{
	/** Key used to identify this input. */
	FStringView Key;
	/** Hash of the raw (uncompressed) input. */
	FIoHash RawHash;
	/** Size of the raw (uncompressed) input in bytes. */
	uint64 RawSize = 0;
};

/** Data for build inputs with the input key. */
struct FBuildInputDataByKey
{
	/** Key used to identify this input. */
	FStringView Key;
	/** Data for the input. */
	FCompressedBuffer Data;
};

/** Parameters for the resolved callback for build definition requests. */
struct FBuildKeyResolvedParams
{
	/** Key for the build definition request that resolved or was canceled. */
	FBuildKey Key;

	/** The resolved build definition. Only available when Status is Ok. */
	FOptionalBuildDefinition&& Definition;

	/** Status of the input request. */
	EStatus Status = EStatus::Error;
};

/** Parameters for the resolved callback for build input metadata requests. */
struct FBuildInputMetaResolvedParams
{
	/** All of the requested inputs sorted by key. Only available when Status is Ok. */
	TConstArrayView<FBuildInputMetaByKey> Inputs;

	/** Status of the input request. */
	EStatus Status = EStatus::Error;
};

/** Parameters for the resolved callback for build input data requests. */
struct FBuildInputDataResolvedParams
{
	/** All of the requested inputs sorted by raw hash. Only available when Status is Ok. */
	TConstArrayView<FBuildInputDataByKey> Inputs;

	/** Status of the input request. */
	EStatus Status = EStatus::Error;
};

/** Interface to resolve references to build inputs. */
class IBuildInputResolver
{
public:
	virtual ~IBuildInputResolver() = default;

	/**
	 * Asynchronous request to resolve a definition from a key.
	 *
	 * @param Key          The key of the definition to resolve.
	 * @param OnResolved   A required callback invoked when resolving completes or is canceled.
	 */
	virtual FRequest ResolveKey(
		const FBuildKey& Key,
		FOnBuildKeyResolved&& OnResolved)
	{
		OnResolved({Key, {}, EStatus::Error});
		return FRequest();
	}

	/**
	 * Asynchronous request to resolve metadata for the inputs from the definition.
	 *
	 * @param Definition   The definition to resolve input metadata for.
	 * @param Priority     A priority to consider when scheduling the request. See EPriority.
	 * @param OnResolved   A required callback invoked when resolving completes or is canceled.
	 */
	virtual FRequest ResolveInputMeta(
		const FBuildDefinition& Definition,
		EPriority Priority,
		FOnBuildInputMetaResolved&& OnResolved)
	{
		OnResolved({{}, EStatus::Error});
		return FRequest();
	}

	/**
	 * Asynchronous request to resolve data for the inputs from the definition.
	 *
	 * @param Definition   The definition to resolve input data for.
	 * @param Priority     A priority to consider when scheduling the request. See EPriority.
	 * @param OnResolved   A required callback invoked when resolving completes or is canceled.
	 * @param Filter       An optional predicate to filter which input keys have data resolved.
	 */
	virtual FRequest ResolveInputData(
		const FBuildDefinition& Definition,
		EPriority Priority,
		FOnBuildInputDataResolved&& OnResolved,
		FBuildInputFilter&& Filter = FBuildInputFilter())
	{
		OnResolved({{}, EStatus::Error});
		return FRequest();
	}

	/**
	 * Asynchronous request to resolve data for the inputs from the action.
	 *
	 * @param Action       The action to resolve input data for.
	 * @param Priority     A priority to consider when scheduling the request. See EPriority.
	 * @param OnResolved   A required callback invoked when resolving completes or is canceled.
	 * @param Filter       An optional predicate to filter which input keys have data resolved.
	 */
	virtual FRequest ResolveInputData(
		const FBuildAction& Action,
		EPriority Priority,
		FOnBuildInputDataResolved&& OnResolved,
		FBuildInputFilter&& Filter = FBuildInputFilter())
	{
		OnResolved({{}, EStatus::Error});
		return FRequest();
	}
};

} // UE::DerivedData
