// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenSerialization.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"

#if UE_WITH_ZEN

namespace UE::Zen {

void SaveCbAttachment(const FCbAttachment& Attachment, FCbWriter& Writer)
{
	if (Attachment.IsCompressedBinary())
	{
		Writer.AddBinary(Attachment.AsCompressedBinary().GetCompressed());
		Writer.AddBinaryAttachment(Attachment.GetHash());
	}
	else if (Attachment.IsNull())
	{
		Writer.AddBinary(FMemoryView());
	}
	else
	{
		// NOTE: All attachments needs to be compressed
		checkNoEntry();
	}
}

void SaveCbPackage(const FCbPackage& Package, FCbWriter& Writer)
{
	if (const FCbObject& RootObject = Package.GetObject())
	{
		Writer.AddObject(RootObject);
		Writer.AddObjectAttachment(Package.GetObjectHash());
	}
	for (const FCbAttachment& Attachment : Package.GetAttachments())
	{
		SaveCbAttachment(Attachment, Writer);
	}
	Writer.AddNull();
}

void SaveCbPackage(const FCbPackage& Package, FArchive& Ar)
{
	FCbWriter Writer;
	SaveCbPackage(Package, Writer);
	Writer.Save(Ar);
}

bool TryLoadCbPackage(FCbPackage& Package, FArchive& Ar, FCbBufferAllocator Allocator)
{
	uint8 StackBuffer[64];
	const auto StackAllocator = [&Allocator, &StackBuffer](uint64 Size) -> FUniqueBuffer
	{
		return Size <= sizeof(StackBuffer) ? FUniqueBuffer::MakeView(StackBuffer, Size) : Allocator(Size);
	};

	Package = FCbPackage();
	for (;;)
	{
		FCbField ValueField = LoadCompactBinary(Ar, StackAllocator);
		if (!ValueField)
		{
			Ar.SetError();
			return false;
		}
		if (ValueField.IsNull())
		{
			return true;
		}
		else if (ValueField.IsBinary())
		{
			const FMemoryView View = ValueField.AsBinaryView();
			if (View.GetSize() > 0)
			{
				FSharedBuffer Buffer = FSharedBuffer::MakeView(View, ValueField.GetOuterBuffer()).MakeOwned();
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
				const FIoHash& Hash = HashField.AsAttachment();
				if (HashField.HasError() || FIoHash::HashBuffer(Buffer) != Hash)
				{
					Ar.SetError();
					return false;
				}
				if (HashField.IsObjectAttachment())
				{
					Package.AddAttachment(FCbAttachment(FCbObject(MoveTemp(Buffer)), Hash));
				}
				else
				{
					Package.AddAttachment(FCbAttachment(FCompositeBuffer(MoveTemp(Buffer)), Hash));
				}
			}
		}
		else
		{
			FCbObject Object = ValueField.AsObject();
			if (ValueField.HasError())
			{
				Ar.SetError();
				return false;
			}

			if (Object)
			{
				FCbField HashField = LoadCompactBinary(Ar, StackAllocator);
				FIoHash ObjectHash = HashField.AsObjectAttachment();
				if (HashField.HasError() || Object.GetHash() != ObjectHash)
				{
					Ar.SetError();
					return false;
				}
				Package.SetObject(Object, ObjectHash);
			}
		}
	}
}

}
#endif // UE_WITH_ZEN
