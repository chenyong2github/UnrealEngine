// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/BinarySearch.h"
#include "Memory/CompositeBuffer.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbAttachment::FCbAttachment(FCbObject InValue, const FIoHash* const InHash)
{
	FMemoryView View;
	if (!InValue.IsOwned() || !InValue.TryGetView(View))
	{
		InValue = FCbObject::Clone(InValue);
	}

	Object = InValue.AsFieldView();
	Buffer = MoveTemp(InValue).GetBuffer().Flatten();

	if (InHash)
	{
		Hash = *InHash;
		checkSlow(Hash == FIoHash::HashBuffer(Buffer));
	}
	else
	{
		Hash = FIoHash::HashBuffer(Buffer);
	}
}

FCbAttachment::FCbAttachment(FSharedBuffer InBuffer, const FIoHash* const InHash)
	: Buffer(MoveTemp(InBuffer).MakeOwned())
{
	if (InHash)
	{
		Hash = *InHash;
		checkSlow(Hash == FIoHash::HashBuffer(Buffer));
	}
	else
	{
		Hash = FIoHash::HashBuffer(Buffer);
	}
}

void FCbAttachment::Load(FCbFieldIterator& Fields)
{
	checkf(Fields.IsBinary(), TEXT("Attachments must start with a binary field."));
	const FMemoryView View = Fields.AsBinaryView();
	if (View.GetSize() > 0)
	{
		Buffer = FSharedBuffer::MakeView(View, Fields.GetOuterBuffer()).MakeOwned();
		Object = FCbFieldView();
		++Fields;

		Hash = Fields.AsAttachment();
		checkf(!Fields.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (Fields.IsObjectAttachment())
		{
			Object = FCbFieldView(Buffer.GetData());
		}
		++Fields;
	}
	else
	{
		++Fields;
		Buffer.Reset();
		Object = FCbFieldView();
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
		Buffer = FSharedBuffer::MakeView(View, BufferField.GetOuterBuffer()).MakeOwned();
		Object = FCbFieldView();

		TArray<uint8, TInlineAllocator<64>> HashBuffer;
		FCbField HashField = LoadCompactBinary(Ar,
			[&HashBuffer](uint64 Size) -> FUniqueBuffer
			{
				HashBuffer.SetNumUninitialized(int32(Size));
				return FUniqueBuffer::MakeView(HashBuffer.GetData(), Size);
			});
		Hash = HashField.AsAttachment();
		checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (HashField.IsObjectAttachment())
		{
			Object = FCbFieldView(Buffer.GetData());
		}
	}
	else
	{
		Buffer.Reset();
		Object = FCbFieldView();
		Hash.Reset();
	}
}

void FCbAttachment::Save(FCbWriter& Writer) const
{
	if (Object.IsObject())
	{
		Writer.AddBinary(Buffer);
		if (FCbFieldView(Object).AsObjectView())
		{
			Writer.AddObjectAttachment(Hash);
		}
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
			if (Attachment.IsObject() && !Existing.IsObject())
			{
				Existing = FCbAttachment(FCbObject(Existing.AsBinary()), Existing.GetHash());
			}
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
			if (Object)
			{
				ObjectHash = Fields.AsObjectAttachment();
				checkf(!Fields.HasError(), TEXT("Object must be followed by an ObjectAttachment with the object hash."));
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
				FSharedBuffer Buffer = FSharedBuffer::MakeView(View, ValueField.GetOuterBuffer()).MakeOwned();
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
				const FIoHash& Hash = HashField.AsAttachment();
				checkf(!HashField.HasError(), TEXT("Attachments must be a non-empty binary value with a content hash."));
				if (HashField.IsObjectAttachment())
				{
					AddAttachment(FCbAttachment(FCbObject(MoveTemp(Buffer)), Hash));
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
			if (Object)
			{
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
				ObjectHash = HashField.AsObjectAttachment();
				checkf(!HashField.HasError(), TEXT("Object must be followed by an ObjectAttachment with the object hash."));
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
	if (Object)
	{
		Writer.AddObject(Object);
		Writer.AddObjectAttachment(ObjectHash);
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
