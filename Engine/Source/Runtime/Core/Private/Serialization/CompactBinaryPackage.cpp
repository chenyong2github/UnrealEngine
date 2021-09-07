// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/BinarySearch.h"
#include "Compression/OodleDataCompression.h"
#include "Memory/CompositeBuffer.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool TryLoad_ArchiveFieldIntoAttachment(FCbAttachment& TargetAttachment, FCbField&& Field, FArchive& Ar, FCbBufferAllocator Allocator)
{
	if (const FCbObjectView ObjectView = Field.AsObjectView(); !Field.HasError())
	{
		// Is a null object or object not prefixed with a precomputed hash value
		TargetAttachment = FCbAttachment(FCbObject(ObjectView, MoveTemp(Field)), ObjectView.GetHash());
	}
	else if (const FIoHash ObjectAttachmentHash = Field.AsObjectAttachment(); !Field.HasError())
	{
		// Is an object
		Field = LoadCompactBinary(Ar, Allocator);
		if (!Field.IsObject())
		{
			return false;
		}
		TargetAttachment = FCbAttachment(MoveTemp(Field).AsObject(), ObjectAttachmentHash);
	}
	else if (const FIoHash BinaryAttachmentHash = Field.AsBinaryAttachment(); !Field.HasError())
	{
		// Is an uncompressed binary blob
		Field = LoadCompactBinary(Ar, Allocator);
		FSharedBuffer Buffer = Field.AsBinary();
		if (Field.HasError())
		{
			return false;
		}
		TargetAttachment = FCbAttachment(FCompositeBuffer(Buffer), BinaryAttachmentHash);
	}
	else if (FSharedBuffer Buffer = Field.AsBinary(); !Field.HasError())
	{
		if (Buffer.GetSize() > 0)
		{
			// Is a compressed binary blob
			TargetAttachment = FCbAttachment(FCompressedBuffer::FromCompressed(MoveTemp(Buffer)));
		}
		else
		{
			// Is an uncompressed empty binary blob
			TargetAttachment = FCbAttachment(FCompositeBuffer(Buffer), FIoHash::HashBuffer(nullptr, 0));
		}
	}
	else
	{
		return false;
	}

	return true;
}

FCbAttachment::FCbAttachment(const FCbObject& InValue, const FIoHash* const InHash)
{
	FMemoryView View;
	if (!InValue.IsOwned() || !InValue.TryGetView(View))
	{
		FCbObject ClonedValue = FCbObject::Clone(InValue);

		if (InHash)
		{
			checkSlow(*InHash == ClonedValue.GetHash());
			Value.Emplace<FObjectValue>(MoveTemp(ClonedValue), *InHash);
		}
		else
		{
			FIoHash ClonedValueHash = ClonedValue.GetHash();
			Value.Emplace<FObjectValue>(MoveTemp(ClonedValue), ClonedValueHash);
		}
	}
	else
	{
		if (InHash)
		{
			Value.Emplace<FObjectValue>(InValue, *InHash);
			checkSlow(*InHash == InValue.GetHash());
		}
		else
		{
			Value.Emplace<FObjectValue>(InValue, InValue.GetHash());
		}
	}

}

FIoHash FCbAttachment::GetHash() const
{
	if (const FCompressedBuffer* Buffer = Value.TryGet<FCompressedBuffer>())
	{
		return Buffer->GetRawHash();
	}
	else if (const FBinaryValue* BinaryValue = Value.TryGet<FBinaryValue>())
	{
		return BinaryValue->Hash;
	}
	else if (const FObjectValue* ObjectValue = Value.TryGet<FObjectValue>())
	{
		return ObjectValue->Hash;
	}
	else
	{
		return FIoHash::Zero;
	}
}

bool FCbAttachment::TryLoad(FCbFieldIterator& Fields)
{
	if (const FCbObjectView ObjectView = Fields.AsObjectView(); !Fields.HasError())
	{
		// Is a null object or object not prefixed with a precomputed hash value
		Value.Emplace<FObjectValue>(FCbObject(ObjectView, Fields.GetOuterBuffer()), ObjectView.GetHash());
		++Fields;
	}
	else if (const FIoHash ObjectAttachmentHash = Fields.AsObjectAttachment(); !Fields.HasError())
	{
		// Is an object
		++Fields;
		const FCbObjectView InnerObjectView = Fields.AsObjectView();
		if (Fields.HasError())
		{
			return false;
		}
		Value.Emplace<FObjectValue>(FCbObject(InnerObjectView, Fields.GetOuterBuffer()), ObjectAttachmentHash);
		++Fields;
	}
	else if (const FIoHash BinaryAttachmentHash = Fields.AsBinaryAttachment(); !Fields.HasError())
	{
		// Is an uncompressed binary blob
		++Fields;
		FMemoryView BinaryView = Fields.AsBinaryView();
		if (Fields.HasError())
		{
			return false;
		}
		Value.Emplace<FBinaryValue>(FSharedBuffer::MakeView(BinaryView, Fields.GetOuterBuffer()), BinaryAttachmentHash);
		++Fields;
	}
	else if (FMemoryView BinaryView = Fields.AsBinaryView(); !Fields.HasError())
	{
		if (BinaryView.GetSize() > 0)
		{
			// Is a compressed binary blob
			Value.Emplace<FCompressedBuffer>(FCompressedBuffer::FromCompressed(FSharedBuffer::MakeView(BinaryView, Fields.GetOuterBuffer())).MakeOwned());
			++Fields;
		}
		else
		{
			// Is an uncompressed empty binary blob
			Value.Emplace<FBinaryValue>(FSharedBuffer::MakeView(BinaryView, Fields.GetOuterBuffer()), FIoHash::HashBuffer(nullptr, 0));
			++Fields;
		}
	}
	else
	{
		return false;
	}

	return true;
}

bool FCbAttachment::TryLoad(FArchive& Ar, FCbBufferAllocator Allocator)
{
	FCbField Field = LoadCompactBinary(Ar, Allocator);
	return TryLoad_ArchiveFieldIntoAttachment(*this, MoveTemp(Field), Ar, Allocator);
}

void FCbAttachment::Save(FCbWriter& Writer) const
{
	if (const FObjectValue* ObjectValue = Value.TryGet<FObjectValue>())
	{
		if (ObjectValue->Object)
		{
			Writer.AddObjectAttachment(ObjectValue->Hash);
		}
		Writer.AddObject(ObjectValue->Object);
	}
	else if (const FBinaryValue* BinaryValue = Value.TryGet<FBinaryValue>())
	{
		if (BinaryValue->Buffer.GetSize() > 0)
		{
			Writer.AddBinaryAttachment(BinaryValue->Hash);
		}
		Writer.AddBinary(BinaryValue->Buffer);
	}
	else if (const FCompressedBuffer* BufferValue = Value.TryGet<FCompressedBuffer>())
	{
		Writer.AddBinary(BufferValue->GetCompressed());
	}
	else
	{
		checkf(false, TEXT("Null attachments cannot be serialized."))
	}
}

void FCbAttachment::Save(FArchive& Ar) const
{
	FCbWriter Writer;
	Save(Writer);
	Writer.Save(Ar);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCbPackage::SetObject(FCbObject InObject, const FIoHash* InObjectHash, FAttachmentResolver* InResolver)
{
	if (InObject)
	{
		Object = InObject.IsOwned() ? MoveTemp(InObject) : FCbObject::Clone(InObject);
		if (InObjectHash)
		{
			ObjectHash = *InObjectHash;
			checkSlow(ObjectHash == Object.GetHash());
		}
		else
		{
			ObjectHash = Object.GetHash();
		}
		if (InResolver)
		{
			GatherAttachments(Object, *InResolver);
		}
	}
	else
	{
		Object.Reset();
		ObjectHash.Reset();
	}
}

void FCbPackage::AddAttachment(const FCbAttachment& Attachment, FAttachmentResolver* Resolver)
{
	if (!Attachment.IsNull())
	{
		const int32 Index = Algo::LowerBound(Attachments, Attachment);
		if (Attachments.IsValidIndex(Index) && Attachments[Index] == Attachment)
		{
			FCbAttachment& Existing = Attachments[Index];
			Existing = Attachment;
		}
		else
		{
			Attachments.Insert(Attachment, Index);
		}
		if (Attachment.IsObject() && Resolver)
		{
			GatherAttachments(Attachment.AsObject(), *Resolver);
		}
	}
}

int32 FCbPackage::RemoveAttachment(const FIoHash& Hash)
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Hash,
		[](const FCbAttachment& Attachment) -> FIoHash { return Attachment.GetHash(); });
	if (Attachments.IsValidIndex(Index))
	{
		Attachments.RemoveAt(Index);
		return 1;
	}
	return 0;
}

bool FCbPackage::Equals(const FCbPackage& Package) const
{
	return ObjectHash == Package.ObjectHash && Attachments == Package.Attachments;
}

const FCbAttachment* FCbPackage::FindAttachment(const FIoHash& Hash) const
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Hash,
		[](const FCbAttachment& Attachment) -> FIoHash { return Attachment.GetHash(); });
	return Attachments.IsValidIndex(Index) ? &Attachments[Index] : nullptr;
}

void FCbPackage::GatherAttachments(const FCbObject& Value, FAttachmentResolver Resolver)
{
	Value.IterateAttachments([this, &Resolver](FCbFieldView Field)
		{
			const FIoHash& Hash = Field.AsAttachment();
			if (FSharedBuffer Buffer = Resolver(Hash))
			{
				if (Field.IsObjectAttachment())
				{
					AddAttachment(FCbAttachment(FCbObject(MoveTemp(Buffer)), Hash), &Resolver);
				}
				else
				{
					AddAttachment(FCbAttachment(MoveTemp(Buffer)));
				}
			}
		});
}

bool FCbPackage::TryLoad(FCbFieldIterator& Fields)
{
	*this = FCbPackage();

	while (Fields)
	{
		if (Fields.IsNull())
		{
			++Fields;
			break;
		}
		else if (FIoHash Hash = Fields.AsHash(); !Fields.HasError() && !Fields.IsAttachment())
		{
			++Fields;
			FCbObjectView ObjectView = Fields.AsObjectView();
			if (Fields.HasError() || Hash != ObjectView.GetHash())
			{
				return false;
			}
			Object = FCbObject(ObjectView, Fields.GetOuterBuffer());
			Object.MakeOwned();
			ObjectHash = Hash;
			++Fields;
		}
		else
		{
			FCbAttachment Attachment;
			if (!Attachment.TryLoad(Fields))
			{
				return false;
			}
			AddAttachment(Attachment);
		}
	}
	return true;
}

bool FCbPackage::TryLoad(FArchive& Ar, FCbBufferAllocator Allocator)
{
	*this = FCbPackage();
	for (;;)
	{
		FCbField Field = LoadCompactBinary(Ar, Allocator);
		if (!Field)
		{
			Ar.SetError();
			return false;
		}

		if (Field.IsNull())
		{
			return true;
		}
		else if (FIoHash Hash = Field.AsHash(); !Field.HasError() && !Field.IsAttachment())
		{
			Field = LoadCompactBinary(Ar, Allocator);
			FCbObjectView ObjectView = Field.AsObjectView();
			if (Field.HasError() || Hash != ObjectView.GetHash())
			{
				return false;
			}
			Object = FCbObject(ObjectView, Field.GetOuterBuffer());
			ObjectHash = Hash;
		}
		else
		{
			FCbAttachment Attachment;
			if (!TryLoad_ArchiveFieldIntoAttachment(Attachment, MoveTemp(Field), Ar, Allocator))
			{
				return false;
			}
			AddAttachment(Attachment);
		}
	}
}

void FCbPackage::Save(FCbWriter& Writer) const
{
	if (Object)
	{
		Writer.AddHash(ObjectHash);
		Writer.AddObject(Object);
	}
	for (const FCbAttachment& Attachment : Attachments)
	{
		Attachment.Save(Writer);
	}
	Writer.AddNull();
}

void FCbPackage::Save(FArchive& Ar) const
{
	FCbWriter Writer;
	Save(Writer);
	Writer.Save(Ar);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
