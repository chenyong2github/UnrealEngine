// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Templates/Function.h"

class FCompressedBuffer;
class FSharedBuffer;

namespace UE::Virtualization
{
	
class FPayloadId;

/**
 * Base interface for virtualized data
 */
class IVirtualizedData
{
public:
	
	virtual ~IVirtualizedData() = default;

public:
	/**
	 * Returns a unique identifier for the object itself.
	 * This should only return a valid FGuid as long as the object owns a valid payload.
	 * If an object with a valid payload, has that payload removed then it should start
	 * returning an invalid FGuid instead.
	 * Should that object be given a new payload it should then return the original 
	 * identifier, there is no need to generate a new one.
	 */
	virtual FGuid GetIdentifier() const = 0;

	/** Returns an unique identifier for the content of the payload. */
	virtual const FPayloadId& GetPayloadId() const = 0;

	/** Returns the size of the payload in bytes */
	virtual int64 GetPayloadSize() const = 0;

	/** Returns an immutable FSharedBuffer reference to the payload data. */
	virtual TFuture<FSharedBuffer> GetPayload() const = 0;

	/**
	  * Returns an immutable FCompressedBuffer reference to the payload data.
	  *
	  * Note that depending on the internal storage formats, the payload might not actually be compressed, but that
	  * will be handled by the FCompressedBuffer interface. Call FCompressedBuffer::Decompress() to get access to
	  * the payload in FSharedBuffer format.
	  */
	 virtual TFuture<FCompressedBuffer> GetCompressedPayload() const = 0;

	/**
	 * Allows the existing payload to be replaced with a new one.
	 *
	 * To pass in a raw pointer use 'FSharedBuffer::...(Data, Size)' to create a valid FSharedBuffer.
	 * Use 'FSharedBuffer::MakeView' if you want to retain ownership on the data being passed in, and use
	 * 'FSharedBuffer::TakeOwnership' if you are okay with the bulkdata object taking over ownership of it.
	 * The bulkdata object must own its internal buffer, so if you pass in a non-owned FSharedBuffer (ie
	 * by using 'FSharedBuffer::MakeView') then a clone of the data will be created internally and assigned
	 * to the bulkdata object.
	 * 
	 * @param InPayload The payload to update the bulkdata with
	 * @param CompressionFormat (optional) The compression format to use, NAME_None indicates that the
	 * payload is already in a compressed format and will not gain from being compressed again. These 
	 * payloads will never be compressed. NAME_Default will apply which ever compression format that the
	 * underlying code deems appropriate. Other specific compression formats may be allowed, see the 
	 * documentation of FCompressedBuffer for details.
	 */
	virtual void UpdatePayload(FSharedBuffer InPayload, FName CompressionFormat = NAME_Default) = 0;
};

} // namespace UE::Virtualization
