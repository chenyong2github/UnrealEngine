// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "HAL/Platform.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Guid.h"
#include "Misc/PackagePath.h"
#include "Virtualization/VirtualizationManager.h"

class FArchive;
class UObject;

//TODO: At some point it might be a good idea to uncomment this to make sure that FVirtualizedUntypedBulkData is
//		never used at runtime (requires a little too much reworking of some assets for now though)
//#if WITH_EDITORONLY_DATA

/**
 * The goal of this class is to provide an editor time version of BulkData that will work with the content
 * virtualization system.
 *
 * Assuming that the DDC is hot, the virtualized payloads are accessed relatively infrequently, usually when the package
 * is being edited in the editor in some manner. So the payload access is designed around this. If the data is frequently 
 * accessed when running the editor then the user would not gain from having it virtualized as they would end up pulling
 * it immediately anyway.
 *
 * The biggest difference with normal bulkdata is that the access times might be significantly longer if the
 * payload is not readily available which is why the only way to access the payload is by a TFuture or a callback
 * so that the caller is forced to consider how to handle the potential stall and hopefully organize their code 
 * in such a way that the time lag is not noticeable to the user.
 *
 * The second biggest difference is that the caller will own the payload memory once it is returned to them, unlike
 * the old bulkdata class which would retain ownership. Which forces the calling code to be in control of when the 
 * memory is actually allocated and for how long. With the old bulkdata class a single access would leave that memory
 * allocated in a non-obvious way and would lead to memory bloats within the editor.
 *
 * The method ::GetGuid can be used to access a unique identifier for the payload, currently it is based on the 
 * payload itself, so that two objects with the same payload would both have the same Guid. The intent is that we 
 * would be able to share local copies of the payload between branches to reduce the cost of having multiple branches 
 * with similar data on the same machine.
 * 
 */

/** The base class with no type */
class COREUOBJECT_API FVirtualizedUntypedBulkData
{
public:
	using OnDataReadyCallback = TUniqueFunction<void(FSharedBuffer)>;

	FVirtualizedUntypedBulkData() = default;
	~FVirtualizedUntypedBulkData();

	/** 
	* Convenience method to make it easier to convert from BulkData to FVirtualizedBulkData and sets the Guid 
	*
	* @param BulkData	The bulkdata object to create from.
	* @param Guid		A guid associated with the bulkdata object which will be used to identify the payload.
	*					This MUST remain the same between sessions so that the payloads key remains consistent!
	*/
	void CreateFromBulkData(FUntypedBulkData& BulkData, const FGuid& Guid);

	void Serialize(FArchive& Ar, UObject* Owner);

	/** Reset to a truly empty state */
	void Reset();

	// TODO: Probably can just remove this as there probably isn't a good use case for unloading updated payloads as there is no
	// way for us to restore it. In that case ::Reset might as well be used.
	/** Unloads the data (if possible) but leaves it in a state where the data can be reloaded */
	void UnloadData();

	/** Returns the a key that identifies the payload */
	const FGuid& GetKey() const { return Key; }

	/** Returns the size of the payload in bytes */
	int64 GetBulkDataSize() const { return PayloadLength; }

	// TODO: This (IsDataLoaded) is only needed for TextureDerivedData.cpp (which assumes that the data 
	// needs to be loaded in order to be able to run on a background thread. Since FVirtualizedUntypedBulkData 
	// will aim to be thread safe we could change TextureDerivedData and remove this.
	/** Returns if the data is being held in memory (true) or will be loaded from disk (false) */
	bool IsDataLoaded() const { return !Payload.IsNull(); }

	/** 
	 * Enable/Disable if the payload should be compressed if it ends up being serialized to disk on the local
	 * machine (if virtualization is disabled or if the payload does not meet the virtualization requirements
	 * of the project for example)
	 */
	void EnableDataCompression(bool bFlag) { bShouldCompressData = bFlag;  }

	/**
	 * Returns an immutable FSharedBuffer reference to the payload data. 
	 *
	 * To convert to a mutable reference call'FSharedBufferRef Mutable = MakeSharedBuffer(FSharedBuffer::Clone, Immutable->GetView());'
	 * which will return a newly allocated clone of the payload which the caller will have full ownership over.
	 */
	TFuture<FSharedBuffer> GetData() const;

	/** Returns an immutable FSharedBuffer reference to the payload data via the OnDataReadyCallback */
	void GetData(OnDataReadyCallback&& Callback) const;
		
	/**
	 * Allows the existing payload to be replaced with a new one.
	 *
	 * To pass in a raw pointer use 'MakeSharedBuffer(...)' to create a valid FSharedBufferConstRef
	 * Use 'FSharedBuffer::Wrap' if you want to retain ownership on the data being passed in, and use 
	 * 'FSharedBuffer::AssumeOwnership' if you are okay with the bulkdata object taking over ownership of it.
	 * The bulkdata object must own it's internal buffer, so if you pass in a non-owned FSharedBuffer (ie 
	 * by using 'FSharedBuffer::Wrap') then a clone of the data will be created internally and assigned to
	 * the bulkdata object.
	 */
	void UpdatePayload(const FSharedBuffer& InPayload);

private:

	FSharedBuffer GetDataInternal() const;

	void CalculateKey();

	FSharedBuffer LoadFromDisk() const;
	bool SerializeData(FArchive& Ar, FSharedBuffer& Payload) const;

	void PushData();
	FSharedBuffer PullData() const;

	FPackagePath GetPackagePathFromOwner(UObject* Owner, EPackageSegment& OutPackageSegment) const;

	bool CanUnloadData() const;

	/** Unique identifier for the payload*/
	FGuid Key;

	/** Pointer to the payload if it is held in memory (it has been updated but not yet saved to disk for example) */
	FSharedBuffer Payload;

	/** Length of the payload in bytes */
	int64 PayloadLength = 0;

	/** Is the data actually virtualized or not? */
	bool bIsVirtualized = false;
	
	//---- The remaining members are used when the payload is not virtualized.
	
	/** Should we compress the data next time (don't want to serialize this)
	TODO: Not currently affected by ::Reset which is an odd design and needs fixing */
	bool bShouldCompressData = false;

	/** If the data has been saved to disk rather than being Virtualized, was it stored in a compressed format? */
	bool bIsDataStoredAsCompressed = false ;

	/** PackagePath containing the payload (this will be empty if the payload does not come from PackageResourceManager)*/
	FPackagePath PackagePath;

	/** PackageSegment to load with the packagepath (unused if the payload does not come from PackageResourceManager) */
	EPackageSegment PackageSegment;

	/** Offset of the payload in the file that contains it (INDEX_NONE if the payload does not come from a file)*/
	int64 OffsetInFile = INDEX_NONE;
};

//TODO: Probably remove this and change FVirtualizedUntypedBulkData to always be TVirtualizedBulkData<uint8>
/** Type safe versions */
template<typename DataType>
class TVirtualizedBulkData final : public FVirtualizedUntypedBulkData
{
public:
	TVirtualizedBulkData() = default;
	~TVirtualizedBulkData() = default;
};

using FByteVirtualizedBulkData	= TVirtualizedBulkData<uint8>;
using FWordVirtualizedBulkData	= TVirtualizedBulkData<uint16>;
using FIntVirtualizedBulkData	= TVirtualizedBulkData<int32>;
using FFloatVirtualizedBulkData	= TVirtualizedBulkData<float>;

//#endif //WITH_EDITORONLY_DATA
