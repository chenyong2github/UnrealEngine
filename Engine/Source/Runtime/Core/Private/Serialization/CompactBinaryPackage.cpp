// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/BinarySearch.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbAttachment::FCbAttachment(FCbFieldIterator InValue, const FIoHash* const InHash)
{
	if (InValue)
	{
		if (!InValue.IsOwned())
		{
			InValue = FCbFieldIterator::CloneRange(InValue);
		}

		CompactBinary = FCbFieldViewIterator(InValue);
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

FSharedBuffer FCbAttachment::AsBinaryView() const
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

	return FCbFieldIterator::CloneRange(CompactBinary).GetRangeBuffer();
}

FCbFieldIterator FCbAttachment::AsCompactBinary() const
{
	return CompactBinary ? FCbFieldIterator::MakeRangeView(CompactBinary, Buffer) : FCbFieldIterator();
}

void FCbAttachment::Load(FCbFieldIterator& Fields)
{
	checkf(Fields.IsBinary(), TEXT("Attachments must start with a binary field."));
	const FMemoryView View = Fields.AsBinaryView();
	if (View.GetSize() > 0)
	{
		Buffer = FSharedBuffer::MakeView(View, Fields.GetOuterBuffer());
		Buffer.MakeOwned();
		++Fields;
		Hash = Fields.AsAttachment();
		checkf(!Fields.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (Fields.IsCompactBinaryAttachment())
		{
			CompactBinary = FCbFieldViewIterator::MakeRange(Buffer);
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
	FCbField BufferField = LoadCompactBinary(Ar, Allocator);
	checkf(BufferField.IsBinary(), TEXT("Attachments must start with a binary field."));
	const FMemoryView View = BufferField.AsBinaryView();
	if (View.GetSize() > 0)
	{
		Buffer = FSharedBuffer::MakeView(View, BufferField.GetOuterBuffer());
		Buffer.MakeOwned();
		CompactBinary = FCbFieldViewIterator();

		TArray<uint8, TInlineAllocator<64>> HashBuffer;
		FCbField HashField = LoadCompactBinary(Ar,
			[&HashBuffer](uint64 Size) -> FUniqueBuffer
			{
				HashBuffer.SetNumUninitialized(int32(Size));
				return FUniqueBuffer::MakeView(HashBuffer.GetData(), Size);
			});
		Hash = HashField.AsAttachment();
		checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (HashField.IsCompactBinaryAttachment())
		{
			CompactBinary = FCbFieldViewIterator::MakeRange(Buffer);
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
			Writer.AddBinary(SerializedView);
		}
		else
		{
			Writer.AddBinary(AsBinaryView());
		}
		Writer.AddCompactBinaryAttachment(Hash);
	}
	else if (Buffer.GetSize())
	{
		Writer.AddBinary(Buffer);
		Writer.AddBinaryAttachment(Hash);
	}
	else // Null
	{
		Writer.AddBinary(FMemoryView());
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
	if (InObject.CreateViewIterator())
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
			GatherAttachments(Object.CreateViewIterator(), *InResolver);
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
				Existing = FCbAttachment(FCbFieldIterator::MakeRange(Existing.AsBinaryView()));
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

void FCbPackage::GatherAttachments(const FCbFieldViewIterator& Fields, FAttachmentResolver Resolver)
{
	Fields.IterateRangeAttachments([this, &Resolver](FCbFieldView Field)
		{
			const FIoHash& Hash = Field.AsAttachment();
			if (FSharedBuffer Buffer = Resolver(Hash))
			{
				if (Field.IsCompactBinaryAttachment())
				{
					AddAttachment(FCbAttachment(FCbFieldIterator::MakeRange(MoveTemp(Buffer)), Hash), &Resolver);
				}
				else
				{
					AddAttachment(FCbAttachment(MoveTemp(Buffer), Hash));
				}
			}
		});
}

void FCbPackage::Load(FCbFieldIterator& Fields)
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
			Object = Fields.AsObject();
			Object.MakeOwned();
			++Fields;
			if (Object.CreateViewIterator())
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
		FCbField ValueField = LoadCompactBinary(Ar, StackAllocator);
		if (ValueField.IsNull())
		{
			break;
		}
		else if (ValueField.IsBinary())
		{
			const FMemoryView View = ValueField.AsBinaryView();
			if (View.GetSize() > 0)
			{
				FSharedBuffer Buffer = FSharedBuffer::MakeView(View, ValueField.GetOuterBuffer());
				Buffer.MakeOwned();
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
				const FIoHash& Hash = HashField.AsAttachment();
				checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
				if (HashField.IsCompactBinaryAttachment())
				{
					AddAttachment(FCbAttachment(FCbFieldIterator::MakeRange(MoveTemp(Buffer)), Hash));
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
			Object = ValueField.AsObject();
			Object.MakeOwned();
			if (Object.CreateViewIterator())
			{
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
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
	if (Object.CreateViewIterator())
	{
		Writer.AddObject(Object);
		Writer.AddCompactBinaryAttachment(ObjectHash);
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
