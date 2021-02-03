// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/BinarySearch.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbAttachment::FCbAttachment(FCbFieldRefIterator InValue, const FIoHash* const InHash)
{
	if (InValue)
	{
		if (!InValue.IsOwned())
		{
			InValue = FCbFieldRefIterator::CloneRange(InValue);
		}

		CompactBinary = FCbFieldIterator(InValue);
		Buffer = MoveTemp(InValue).GetOuterBuffer();
	}

	if (InHash)
	{
		Hash = *InHash;
		if (CompactBinary)
		{
			checkSlow(Hash == CompactBinary.GetRangeHash());
		}
		else
		{
			checkfSlow(Hash.IsZero(), TEXT("A null or empty field range must use a hash of zero."));
		}
	}
	else if (CompactBinary)
	{
		Hash = CompactBinary.GetRangeHash();
	}
}

FCbAttachment::FCbAttachment(FSharedBuffer InBuffer, const FIoHash* const InHash)
	: Buffer(MoveTemp(InBuffer))
{
	Buffer.MakeOwned();
	if (InHash)
	{
		Hash = *InHash;
		if (Buffer.GetSize())
		{
			checkSlow(Hash == FIoHash::HashBuffer(Buffer));
		}
		else
		{
			checkfSlow(Hash.IsZero(), TEXT("A null or empty buffer must use a hash of zero."));
		}
	}
	else if (Buffer.GetSize())
	{
		Hash = FIoHash::HashBuffer(Buffer);
	}
	else
	{
		Buffer.Reset();
	}
}

FSharedBuffer FCbAttachment::AsBinary() const
{
	if (!CompactBinary)
	{
		return Buffer;
	}

	FMemoryView SerializedView;
	if (CompactBinary.TryGetSerializedRangeView(SerializedView))
	{
		return SerializedView == Buffer.GetView() ? Buffer : FSharedBuffer::MakeView(SerializedView, Buffer);
	}

	return FCbFieldRefIterator::CloneRange(CompactBinary).GetRangeBuffer();
}

FCbFieldRefIterator FCbAttachment::AsCompactBinary() const
{
	return CompactBinary ? FCbFieldRefIterator::MakeRangeView(CompactBinary, Buffer) : FCbFieldRefIterator();
}

void FCbAttachment::Load(FCbFieldRefIterator& Fields)
{
	checkf(Fields.IsBinary(), TEXT("Attachments must start with a binary field."));
	const FMemoryView View = Fields.AsBinary();
	if (View.GetSize() > 0)
	{
		Buffer = FSharedBuffer::MakeView(View, Fields.GetOuterBuffer());
		Buffer.MakeOwned();
		++Fields;
		Hash = Fields.AsAttachment();
		checkf(!Fields.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (Fields.IsCompactBinaryAttachment())
		{
			CompactBinary = FCbFieldIterator::MakeRange(Buffer);
		}
		++Fields;
	}
	else
	{
		++Fields;
		Buffer.Reset();
		CompactBinary.Reset();
		Hash.Reset();
	}
}

void FCbAttachment::Load(FArchive& Ar, FCbBufferAllocator Allocator)
{
	FCbFieldRef BufferField = LoadCompactBinary(Ar, Allocator);
	checkf(BufferField.IsBinary(), TEXT("Attachments must start with a binary field."));
	const FMemoryView View = BufferField.AsBinary();
	if (View.GetSize() > 0)
	{
		Buffer = FSharedBuffer::MakeView(View, BufferField.GetOuterBuffer());
		Buffer.MakeOwned();
		CompactBinary = FCbFieldIterator();

		TArray<uint8, TInlineAllocator<64>> HashBuffer;
		FCbFieldRef HashField = LoadCompactBinary(Ar,
			[&HashBuffer](uint64 Size) -> FUniqueBuffer
			{
				HashBuffer.SetNumUninitialized(int32(Size));
				return FUniqueBuffer::MakeView(HashBuffer.GetData(), Size);
			});
		Hash = HashField.AsAttachment();
		checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (HashField.IsCompactBinaryAttachment())
		{
			CompactBinary = FCbFieldIterator::MakeRange(Buffer);
		}
	}
	else
	{
		Buffer.Reset();
		CompactBinary.Reset();
		Hash.Reset();
	}
}

void FCbAttachment::Save(FCbWriter& Writer) const
{
	if (CompactBinary)
	{
		FMemoryView SerializedView;
		if (CompactBinary.TryGetSerializedRangeView(SerializedView))
		{
			Writer.Binary(SerializedView);
		}
		else
		{
			Writer.Binary(AsBinary());
		}
		Writer.CompactBinaryAttachment(Hash);
	}
	else if (Buffer.GetSize())
	{
		Writer.Binary(Buffer);
		Writer.BinaryAttachment(Hash);
	}
	else // Null
	{
		Writer.Binary(FMemoryView());
	}
}

void FCbAttachment::Save(FArchive& Ar) const
{
	FCbWriter Writer;
	Save(Writer);
	Writer.Save(Ar);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCbPackage::SetObject(FCbObjectRef InObject, const FIoHash* InObjectHash, FAttachmentResolver* InResolver)
{
	if (InObject.CreateIterator())
	{
		Object = InObject.IsOwned() ? MoveTemp(InObject) : FCbObjectRef::Clone(InObject);
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
			GatherAttachments(Object.CreateIterator(), *InResolver);
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
			if (Attachment.IsCompactBinary() && !Existing.IsCompactBinary())
			{
				Existing = FCbAttachment(FCbFieldRefIterator::MakeRange(Existing.AsBinary()));
			}
		}
		else
		{
			Attachments.Insert(Attachment, Index);
		}
		if (Attachment.IsCompactBinary() && Resolver)
		{
			GatherAttachments(Attachment.AsCompactBinary(), *Resolver);
		}
	}
}

int32 FCbPackage::RemoveAttachment(const FIoHash& Hash)
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Hash,
		[](const FCbAttachment& Attachment) -> const FIoHash& { return Attachment.GetHash(); });
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
		[](const FCbAttachment& Attachment) -> const FIoHash& { return Attachment.GetHash(); });
	return Attachments.IsValidIndex(Index) ? &Attachments[Index] : nullptr;
}

void FCbPackage::GatherAttachments(const FCbFieldIterator& Fields, FAttachmentResolver Resolver)
{
	Fields.IterateRangeAttachments([this, &Resolver](FCbField Field)
		{
			const FIoHash& Hash = Field.AsAttachment();
			if (FSharedBuffer Buffer = Resolver(Hash))
			{
				if (Field.IsCompactBinaryAttachment())
				{
					AddAttachment(FCbAttachment(FCbFieldRefIterator::MakeRange(MoveTemp(Buffer)), Hash), &Resolver);
				}
				else
				{
					AddAttachment(FCbAttachment(MoveTemp(Buffer), Hash));
				}
			}
		});
}

void FCbPackage::Load(FCbFieldRefIterator& Fields)
{
	*this = FCbPackage();
	while (Fields)
	{
		if (Fields.IsNull())
		{
			++Fields;
			break;
		}
		else if (Fields.IsBinary())
		{
			FCbAttachment Attachment;
			Attachment.Load(Fields);
			AddAttachment(Attachment);
		}
		else
		{
			checkf(Fields.IsObject(), TEXT("Expected Object, Binary, or Null field when loading a package."));
			Object = Fields.AsObjectRef();
			Object.MakeOwned();
			++Fields;
			if (Object.CreateIterator())
			{
				ObjectHash = Fields.AsCompactBinaryAttachment();
				checkf(!Fields.HasError(), TEXT("Object must be followed by a CompactBinaryAttachment with the object hash."));
				++Fields;
			}
			else
			{
				Object.Reset();
			}
		}
	}
}

void FCbPackage::Load(FArchive& Ar, FCbBufferAllocator Allocator)
{
	uint8 StackBuffer[64];
	const auto StackAllocator = [&Allocator, &StackBuffer](uint64 Size) -> FUniqueBuffer
	{
		return Size <= sizeof(StackBuffer) ? FUniqueBuffer::MakeView(StackBuffer, Size) : Allocator(Size);
	};

	*this = FCbPackage();
	for (;;)
	{
		FCbFieldRef ValueField = LoadCompactBinary(Ar, StackAllocator);
		if (ValueField.IsNull())
		{
			break;
		}
		else if (ValueField.IsBinary())
		{
			const FMemoryView View = ValueField.AsBinary();
			if (View.GetSize() > 0)
			{
				FSharedBuffer Buffer = FSharedBuffer::MakeView(View, ValueField.GetOuterBuffer());
				Buffer.MakeOwned();
				FCbFieldRef HashField = LoadCompactBinary(Ar, StackAllocator);
				const FIoHash& Hash = HashField.AsAttachment();
				checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
				if (HashField.IsCompactBinaryAttachment())
				{
					AddAttachment(FCbAttachment(FCbFieldRefIterator::MakeRange(MoveTemp(Buffer)), Hash));
				}
				else
				{
					AddAttachment(FCbAttachment(MoveTemp(Buffer), Hash));
				}
			}
		}
		else
		{
			checkf(ValueField.IsObject(), TEXT("Expected Object, Binary, or Null field when loading a package."));
			Object = ValueField.AsObjectRef();
			Object.MakeOwned();
			if (Object.CreateIterator())
			{
				FCbFieldRef HashField = LoadCompactBinary(Ar, StackAllocator);
				ObjectHash = HashField.AsCompactBinaryAttachment();
				checkf(!HashField.HasError(), TEXT("Object must be followed by a CompactBinaryAttachment with the object hash."));
			}
			else
			{
				Object.Reset();
			}
		}
	}
}

void FCbPackage::Save(FCbWriter& Writer) const
{
	if (Object.CreateIterator())
	{
		Writer.Object(Object);
		Writer.CompactBinaryAttachment(ObjectHash);
	}
	for (const FCbAttachment& Attachment : Attachments)
	{
		Attachment.Save(Writer);
	}
	Writer.Null();
}

void FCbPackage::Save(FArchive& Ar) const
{
	FCbWriter Writer;
	Save(Writer);
	Writer.Save(Ar);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
