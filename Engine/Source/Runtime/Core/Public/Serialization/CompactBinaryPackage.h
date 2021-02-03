// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "IO/IoHash.h"
#include "Memory/SharedBuffer.h"
#include "Serialization/CompactBinary.h"

class FArchive;
class FCbWriter;
template <typename FuncType> class TFunctionRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An attachment is either binary or compact binary and is identified by its hash.
 *
 * A compact binary attachment is also a valid binary attachment and may be accessed as binary.
 *
 * Attachments are serialized as one or two compact binary fields with no name. A Binary field is
 * written first with its content. The content hash is omitted when the content size is zero, and
 * is otherwise written as a BinaryAttachment or CompactBinaryAttachment depending on the type.
 */
class FCbAttachment
{
public:
	/** Construct a null attachment. */
	FCbAttachment() = default;

	/** Construct a compact binary attachment. Value is cloned if not owned. */
	inline explicit FCbAttachment(FCbFieldRefIterator Value)
		: FCbAttachment(MoveTemp(Value), nullptr)
	{
	}

	/** Construct a compact binary attachment. Value is cloned if not owned. Hash must match Value. */
	inline explicit FCbAttachment(FCbFieldRefIterator Value, const FIoHash& Hash)
		: FCbAttachment(MoveTemp(Value), &Hash)
	{
	}

	/** Construct a binary attachment. Value is cloned if not owned. */
	inline explicit FCbAttachment(FSharedBuffer Value)
		: FCbAttachment(MoveTemp(Value), nullptr)
	{
	}

	/** Construct a binary attachment. Value is cloned if not owned. Hash must match Value. */
	inline explicit FCbAttachment(FSharedBuffer Value, const FIoHash& Hash)
		: FCbAttachment(MoveTemp(Value), &Hash)
	{
	}

	/** Reset this to a null attachment. */
	inline void Reset() { *this = FCbAttachment(); }

	/** Whether the attachment has a value. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Whether the attachment has a value. */
	inline bool IsNull() const { return !Buffer; }

	/** Access the attachment as binary. Defaults to a null buffer on error. */
	CORE_API FSharedBuffer AsBinary() const;

	/** Access the attachment as compact binary. Defaults to a field iterator with no value on error. */
	CORE_API FCbFieldRefIterator AsCompactBinary() const;

	/** Returns whether the attachment is binary or compact binary. */
	inline bool IsBinary() const { return !Buffer.IsNull(); }

	/** Returns whether the attachment is compact binary. */
	inline bool IsCompactBinary() const { return CompactBinary.HasValue(); }

	/** Returns the hash of the attachment value. */
	inline const FIoHash& GetHash() const { return Hash; }

	/** Compares attachments by their hash. Any discrepancy in type must be handled externally. */
	inline bool operator==(const FCbAttachment& Attachment) const { return Hash == Attachment.Hash; }
	inline bool operator!=(const FCbAttachment& Attachment) const { return Hash != Attachment.Hash; }
	inline bool operator<(const FCbAttachment& Attachment) const { return Hash < Attachment.Hash; }

	/**
	 * Load the attachment from compact binary as written by Save.
	 *
	 * The attachment references the input iterator if it is owned, and otherwise clones the value.
	 *
	 * The iterator is advanced as attachment fields are consumed from it.
	 */
	CORE_API void Load(FCbFieldRefIterator& Fields);

	/**
	 * Load the attachment from compact binary as written by Save.
	 *
	 * The attachments value will be loaded into an owned buffer.
	 *
	 * @param Allocator Allocator for the attachment value buffer.
	 * @note Allocated buffers will be cloned if they are not owned.
	 */
	CORE_API void Load(FArchive& Ar, FCbBufferAllocator Allocator = FUniqueBuffer::Alloc);

	/** Save the attachment into the writer as a stream of compact binary fields. */
	CORE_API void Save(FCbWriter& Writer) const;

	/** Save the attachment into the archive as a stream of compact binary fields. */
	CORE_API void Save(FArchive& Ar) const;

private:
	CORE_API FCbAttachment(FCbFieldRefIterator Value, const FIoHash* Hash);
	CORE_API FCbAttachment(FSharedBuffer Value, const FIoHash* Hash);

	/** An owned buffer containing the binary or compact binary data. */
	FSharedBuffer Buffer;
	/** A field iterator that is valid only for compact binary attachments. */
	FCbFieldIterator CompactBinary;
	/** A hash of the attachment value. */
	FIoHash Hash;
};

/** Hashes attachments by their hash. Any discrepancy in type must be handled externally. */
inline uint32 GetTypeHash(const FCbAttachment& Attachment)
{
	return GetTypeHash(Attachment.GetHash());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A package is a compact binary object with attachments for its external references.
 *
 * A package is basically a Merkle tree with compact binary as its root and other non-leaf nodes,
 * and either binary or compact binary as its leaf nodes. A node references its child nodes using
 * BinaryHash or FieldHash fields in its compact binary representation.
 *
 * It is invalid for a package to include attachments that are not referenced by its object or by
 * one of its referenced compact binary attachments. When attachments are added explicitly, it is
 * the responsibility of the package creator to follow this requirement. Attachments that are not
 * referenced may not survive a round-trip through certain storage systems.
 *
 * It is valid for a package to exclude referenced attachments, but then it is the responsibility
 * of the package consumer to have a mechanism for resolving those references when necessary.
 *
 * A package is serialized as a sequence of compact binary fields with no name. The object may be
 * both preceded and followed by attachments. The object itself is written as an Object field and
 * followed by its hash in a CompactBinaryAttachment field, if the object is non-empty. A package
 * ends with a Null field. The canonical order of components is the object and its hash, followed
 * by the attachments ordered by hash, followed by a Null field. It is valid for the a package to
 * have its components serialized in any order, provided there is at most one object and the null
 * field is written last, and the object hash immediately follows the object when present.
 */
class FCbPackage
{
public:
	/**
	 * A function that resolves a hash to a buffer containing the data matching that hash.
	 *
	 * The resolver may return a null buffer to skip resolving an attachment for the hash.
	 */
	using FAttachmentResolver = TFunctionRef<FSharedBuffer (const FIoHash& Hash)>;

	/** Construct a null package. */
	FCbPackage() = default;

	/**
	 * Construct a package from a root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 */
	inline explicit FCbPackage(FCbObjectRef InObject)
	{
		SetObject(MoveTemp(InObject));
	}

	/**
	 * Construct a package from a root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline explicit FCbPackage(FCbObjectRef InObject, FAttachmentResolver InResolver)
	{
		SetObject(MoveTemp(InObject), InResolver);
	}

	/**
	 * Construct a package from a root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 */
	inline explicit FCbPackage(FCbObjectRef InObject, const FIoHash& InObjectHash)
	{
		SetObject(MoveTemp(InObject), InObjectHash);
	}

	/**
	 * Construct a package from a root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline explicit FCbPackage(FCbObjectRef InObject, const FIoHash& InObjectHash, FAttachmentResolver InResolver)
	{
		SetObject(MoveTemp(InObject), InObjectHash, InResolver);
	}

	/** Reset this to a null package. */
	inline void Reset() { *this = FCbPackage(); }

	/** Whether the package has a non-empty object or attachments. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Whether the package has an empty object and no attachments. */
	inline bool IsNull() const
	{
		return !Object.CreateIterator() && Attachments.Num() == 0;
	}

	/** Returns the compact binary object for the package. */
	inline const FCbObjectRef& GetObject() const { return Object; }

	/** Returns the has of the compact binary object for the package. */
	inline const FIoHash& GetObjectHash() const { return ObjectHash; }

	/**
	 * Set the root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 */
	inline void SetObject(FCbObjectRef InObject)
	{
		SetObject(MoveTemp(InObject), nullptr, nullptr);
	}

	/**
	 * Set the root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline void SetObject(FCbObjectRef InObject, FAttachmentResolver InResolver)
	{
		SetObject(MoveTemp(InObject), nullptr, &InResolver);
	}

	/**
	 * Set the root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 */
	inline void SetObject(FCbObjectRef InObject, const FIoHash& InObjectHash)
	{
		SetObject(MoveTemp(InObject), &InObjectHash, nullptr);
	}

	/**
	 * Set the root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline void SetObject(FCbObjectRef InObject, const FIoHash& InObjectHash, FAttachmentResolver InResolver)
	{
		SetObject(MoveTemp(InObject), &InObjectHash, &InResolver);
	}

	/** Returns the attachments in this package. */
	inline TConstArrayView<FCbAttachment> GetAttachments() const { return Attachments; }

	/**
	 * Find an attachment by its hash.
	 *
	 * @return The attachment, or null if the attachment is not found.
	 * @note The returned pointer is only valid until the attachments on this package are modified.
	 */
	CORE_API const FCbAttachment* FindAttachment(const FIoHash& Hash) const;

	/** Find an attachment if it exists in the package. */
	inline const FCbAttachment* FindAttachment(const FCbAttachment& Attachment) const
	{
		return FindAttachment(Attachment.GetHash());
	}

	/** Add the attachment to this package. */
	inline void AddAttachment(const FCbAttachment& Attachment)
	{
		AddAttachment(Attachment, nullptr);
	}

	/** Add the attachment to this package, along with any references that can be resolved. */
	inline void AddAttachment(const FCbAttachment& Attachment, FAttachmentResolver Resolver)
	{
		AddAttachment(Attachment, &Resolver);
	}

	/**
	 * Remove an attachment by hash.
	 *
	 * @return Number of attachments removed, which will be either 0 or 1.
	 */
	CORE_API int32 RemoveAttachment(const FIoHash& Hash);
	inline int32 RemoveAttachment(const FCbAttachment& Attachment) { return RemoveAttachment(Attachment.GetHash()); }

	/** Compares packages by their object and attachment hashes. */
	CORE_API bool Equals(const FCbPackage& Package) const;
	inline bool operator==(const FCbPackage& Package) const { return Equals(Package); }
	inline bool operator!=(const FCbPackage& Package) const { return !Equals(Package); }

	/**
	 * Load the object and attachments from compact binary as written by Save.
	 *
	 * The object and attachments reference the input iterator, if it is owned, and otherwise clones
	 * the object and attachments individually to make owned copies.
	 *
	 * The iterator is advanced as object and attachment fields are consumed from it.
	 */
	CORE_API void Load(FCbFieldRefIterator& Fields);

	/**
	 * Load the object and attachments from compact binary as written by Save.
	 *
	 * The object and attachments will be individually loaded into owned buffers.
	 *
	 * @param Allocator Allocator for object and attachment buffers.
	 * @note Allocated buffers will be cloned if they are not owned.
	 */
	CORE_API void Load(FArchive& Ar, FCbBufferAllocator Allocator = FUniqueBuffer::Alloc);

	/** Save the object and attachments into the writer as a stream of compact binary fields. */
	CORE_API void Save(FCbWriter& Writer) const;

	/** Save the object and attachments into the archive as a stream of compact binary fields. */
	CORE_API void Save(FArchive& Ar) const;

private:
	CORE_API void SetObject(FCbObjectRef Object, const FIoHash* Hash, FAttachmentResolver* Resolver);
	CORE_API void AddAttachment(const FCbAttachment& Attachment, FAttachmentResolver* Resolver);

	void GatherAttachments(const FCbFieldIterator& Fields, FAttachmentResolver Resolver);

	/** Attachments ordered by their hash. */
	TArray<FCbAttachment> Attachments;
	FCbObjectRef Object;
	FIoHash ObjectHash;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
