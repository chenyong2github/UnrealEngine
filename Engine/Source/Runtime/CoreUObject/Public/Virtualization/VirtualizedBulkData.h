// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "HAL/Platform.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Guid.h"
#include "Misc/PackagePath.h"
#include "Virtualization/IVirtualizedData.h"
#include "Virtualization/VirtualizationManager.h"

class FArchive;
class UObject;

//TODO: At some point it might be a good idea to uncomment this to make sure that FVirtualizedUntypedBulkData is
//		never used at runtime (requires a little too much reworking of some assets for now though)
//#if WITH_EDITORONLY_DATA

namespace UE::Virtualization
{
	
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
class COREUOBJECT_API FVirtualizedUntypedBulkData : public IVirtualizedData
{
public:
	FVirtualizedUntypedBulkData() = default;
	FVirtualizedUntypedBulkData(FVirtualizedUntypedBulkData&&) = default;
	FVirtualizedUntypedBulkData& operator=(FVirtualizedUntypedBulkData&& ) = default;

	FVirtualizedUntypedBulkData(const FVirtualizedUntypedBulkData& Other);
	FVirtualizedUntypedBulkData& operator=(const FVirtualizedUntypedBulkData& Other);

	virtual ~FVirtualizedUntypedBulkData() = default;

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

	/**
	 * Returns a unique identifier for the object itself.
	 * This should only return a valid FGuid as long as the object owns a valid payload.
	 * If an object with a valid payload, has that payload removed then it should start
	 * returning an invalid FGuid instead.
	 * Should that object be given a new payload it should then return the original
	 * identifier, there is no need to generate a new one.
	 */
	virtual FGuid GetIdentifier() const override;

	/** Returns an unique identifier for the content of the payload */
	virtual const FPayloadId& GetPayloadId() const override { return PayloadContentId; }

	/** Returns the size of the payload in bytes */
	virtual int64 GetPayloadSize() const override { return PayloadSize; }
	
	/** Temp method, to make it easier to transition older code to virtualized bulkdata, remove when we remove UE_USE_VIRTUALBULKDATA */
	FORCEINLINE int64 GetBulkDataSize() const { return GetPayloadSize(); }

	// TODO: This (IsDataLoaded) is only needed for TextureDerivedData.cpp (which assumes that the data 
	// needs to be loaded in order to be able to run on a background thread. Since FVirtualizedUntypedBulkData 
	// will aim to be thread safe we could change TextureDerivedData and remove this.
	/** Returns if the data is being held in memory (true) or will be loaded from disk (false) */
	bool IsDataLoaded() const { return !Payload.IsNull(); }

	/** Returns an immutable FCompressedBuffer reference to the payload data. */
	virtual TFuture<FSharedBuffer> GetPayload() const override;

	/**
	 * Returns an immutable FCompressedBuffer reference to the payload data.
	 *
	 * Note that depending on the internal storage formats, the payload might not actually be compressed, but that
	 * will be handled by the FCompressedBuffer interface. Call FCompressedBuffer::Decompress() to get access to
	 * the payload in FSharedBuffer format.
	 */
	virtual TFuture<FCompressedBuffer> GetCompressedPayload() const override;

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
	virtual void UpdatePayload(FSharedBuffer InPayload, FName CompressionFormat = NAME_Default) override;

	/**
	* Get the CustomVersions used in the file containing the payload. Currently this is assumed
	* to always be the versions in the InlineArchive
	* 
	* @param InlineArchive The archive that was used to load this object
	* 
	* @return The CustomVersions that apply to the interpretation of the payload.
	*/
	FCustomVersionContainer GetCustomVersions(FArchive& InlineArchive);

private:
	/** Flags used to store additional meta information about the bulk data */
	enum class EFlags : uint32
	{
		/** No flags are set */
		None						= 0,
		/** Is the data actually virtualized or not? */
		IsVirtualized				= 1 << 0,
		/** Does the package have access to a .upayload file? */
		HasPayloadSidecarFile		= 1 << 1,
		/** The bulkdata object is currently referencing a payload saved under old bulkdata formats */
		ReferencesLegacyFile		= 1 << 2,
		/** The legacy file being referenced is stored with Zlib compression format */
		LegacyFileIsCompressed		= 1 << 3,
		/** The payload should not have compression applied to it. It is assumed that the payload is already 
			in some sort of compressed format, see the compression documentation above for more details. */
		DisablePayloadCompression	= 1 << 4,
	};

	/** Used to control what level of error reporting we return from some methods */
	enum ErrorVerbosity
	{
		/** No errors should be logged */
		None = 0,
		/** Everything should be logged */
		All
	};

	FRIEND_ENUM_CLASS_FLAGS(EFlags);

	FCompressedBuffer GetDataInternal() const;

	FCompressedBuffer LoadFromDisk() const;
	FCompressedBuffer LoadFromPackageFile() const;
	FCompressedBuffer LoadFromSidecarFile() const;
	FCompressedBuffer LoadFromSidecarFileInternal(ErrorVerbosity Verbosity) const;

	bool SerializeData(FArchive& Ar, FCompressedBuffer& Payload, const EFlags PayloadFlags) const;

	void PushData();
	FCompressedBuffer PullData() const;

	FPackagePath GetPackagePathFromOwner(UObject* Owner, EPackageSegment& OutPackageSegment) const;

	bool CanUnloadData() const;

	void UpdateKeyIfNeeded();

	void RecompressForSerialization(FCompressedBuffer& InOutPayload, EFlags PayloadFlags) const;
	EFlags BuildFlagsForSerialization(FArchive& Ar) const;

	bool IsDataVirtualized() const { return EnumHasAnyFlags(Flags, EFlags::IsVirtualized); }
	bool HasPayloadSidecarFile() const { return EnumHasAnyFlags(Flags, EFlags::HasPayloadSidecarFile); }
	bool IsReferencingOldBulkData() const { return EnumHasAnyFlags(Flags, EFlags::ReferencesLegacyFile); }

	/** Unique identifier for the bulkdata object itself */
	FGuid BulkDataId;

	/** Unique identifier for the contents of the payload*/
	FPayloadId PayloadContentId;

	/** Pointer to the payload if it is held in memory (it has been updated but not yet saved to disk for example) */
	FSharedBuffer Payload;

	/** Length of the payload in bytes */
	int64 PayloadSize = 0;

	//---- The remaining members are used when the payload is not virtualized.
	
	/** The compression algorithm to use when saving the member 'Payload' */
	FName CompressionFormatToUse = NAME_Default;

	/** Offset of the payload in the file that contains it (INDEX_NONE if the payload does not come from a file)*/
	int64 OffsetInFile = INDEX_NONE;

	/** PackagePath containing the payload (this will be empty if the payload does not come from PackageResourceManager)*/
	FPackagePath PackagePath;

	/** PackageSegment to load with the packagepath (unused if the payload does not come from PackageResourceManager) */
	EPackageSegment PackageSegment;

	/** Records if the payload key was created from a FGuid via CreateFromBulkData */
	bool bWasKeyGuidDerived = false;

	/** A 32bit bitfield of flags */
	EFlags Flags = EFlags::None;
};

ENUM_CLASS_FLAGS(FVirtualizedUntypedBulkData::EFlags);

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

/** 
  * Represents an entry to the table of contents found at the start of a payload sidecar file.
  * This might be moved to it's own header and the table of contents made into a proper class 
  * if we decide that we want to make access of the payload sidecar file a generic feature.
  */
struct FTocEntry
{
	static constexpr uint32 PayloadSidecarFileVersion = 1;

	/** Identifier for the payload */
	FPayloadId Identifier;
	/** The offset into the file where we can find the payload */
	int64 OffsetInFile = INDEX_NONE;
	/** The size of the payload WHEN uncompressed. */
	int64 UncompressedSize = INDEX_NONE;

	friend FArchive& operator<<(FArchive& Ar, FTocEntry& Entry)
	{
		Ar << Entry.Identifier;
		Ar << Entry.OffsetInFile;
		Ar << Entry.UncompressedSize;

		return Ar;
	}
};

} // namespace UE::Virtualization

//#endif //WITH_EDITORONLY_DATA
