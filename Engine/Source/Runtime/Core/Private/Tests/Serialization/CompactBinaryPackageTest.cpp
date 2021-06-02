// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/IsSorted.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_DEV_AUTOMATION_TESTS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr EAutomationTestFlags::Type CompactBinaryPackageTestFlags =
	EAutomationTestFlags::Type(EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbAttachmentTest, "System.Core.Serialization.CbAttachment", CompactBinaryPackageTestFlags)
bool FCbAttachmentTest::RunTest(const FString& Parameters)
{
	const auto TestSaveLoadValidate = [this](const TCHAR* Test, const FCbAttachment& Attachment)
	{
		TCbWriter<256> Writer;
		FBufferArchive WriteAr;
		Attachment.Save(Writer);
		Attachment.Save(WriteAr);
		FCbFieldIterator Fields = Writer.Save();

		TestTrue(FString::Printf(TEXT("FCbAttachment(%s).Save()->Equals"), Test),
			MakeMemoryView(WriteAr).EqualBytes(Fields.GetRangeBuffer().GetView()));
		TestEqual(FString::Printf(TEXT("FCbAttachment(%s).Save()->ValidateRange"), Test),
			ValidateCompactBinaryRange(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);
		TestEqual(FString::Printf(TEXT("FCbAttachment(%s).Save()->ValidateAttachment"), Test),
			ValidateCompactBinaryAttachment(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);

		FCbAttachment FromFields;
		FromFields.Load(Fields);
		TestTrue(FString::Printf(TEXT("FCbAttachment(%s).Load(Iterator)->AtEnd"), Test), !bool(Fields));
		TestEqual(FString::Printf(TEXT("FCbAttachment(%s).Load(Iterator)->Equals"), Test), FromFields, Attachment);

		FCbAttachment FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.Load(ReadAr);
		TestTrue(FString::Printf(TEXT("FCbAttachment(%s).Load(Archive)->AtEnd"), Test), ReadAr.AtEnd());
		TestEqual(FString::Printf(TEXT("FCbAttachment(%s).Load(Archive)->Equals"), Test), FromArchive, Attachment);
	};

	// Empty Attachment
	{
		FCbAttachment Attachment;
		TestTrue(TEXT("FCbAttachment(Null).IsNull()"), Attachment.IsNull());
		TestFalse(TEXT("FCbAttachment(Null) as bool"), bool(Attachment));
		TestFalse(TEXT("FCbAttachment(Null).AsBinary()"), bool(Attachment.AsBinary()));
		TestFalse(TEXT("FCbAttachment(Null).AsObject()"), bool(Attachment.AsObject()));
		TestFalse(TEXT("FCbAttachment(Null).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(Null).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(Null).GetHash()"), Attachment.GetHash(), FIoHash());
		TestSaveLoadValidate(TEXT("Null"), Attachment);
	}

	// Binary Attachment
	{
		FSharedBuffer Buffer = FSharedBuffer::Clone(MakeMemoryView<uint8>({0, 1, 2, 3}));
		FCbAttachment Attachment(Buffer);
		TestFalse(TEXT("FCbAttachment(Binary).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(Binary) as bool"), bool(Attachment));
		TestEqual(TEXT("FCbAttachment(Binary).AsBinary()"), Attachment.AsBinary(), Buffer);
		TestFalse(TEXT("FCbAttachment(Binary).AsObject()"), bool(Attachment.AsObject()));
		TestTrue(TEXT("FCbAttachment(Binary).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(Binary).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(Binary).GetHash()"), Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
		TestSaveLoadValidate(TEXT("Binary"), Attachment);
	}

	// Object Attachment
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "Name"_ASV << 42;
		Writer.EndObject();
		FCbObject Object = Writer.Save().AsObject();
		FCbAttachment Attachment(Object);
		TestFalse(TEXT("FCbAttachment(Object).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(Object) as bool"), bool(Attachment));
		TestEqual(TEXT("FCbAttachment(Object).AsBinary()"), Attachment.AsBinary(), Object.GetBuffer());
		TestTrue(TEXT("FCbAttachment(Object).AsObject()"), Attachment.AsObject().Equals(Object));
		TestTrue(TEXT("FCbAttachment(Object).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(Object).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(Object).GetHash()"), Attachment.GetHash(), FIoHash(Object.GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Attachment);
	}

	// Binary View
	{
		const uint8 Value[]{0, 1, 2, 3};
		FSharedBuffer Buffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FCbAttachment Attachment(Buffer);
		TestFalse(TEXT("FCbAttachment(BinaryView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(BinaryView) as bool"), bool(Attachment));
		TestNotEqual(TEXT("FCbAttachment(BinaryView).AsBinary()"), Attachment.AsBinary(), Buffer);
		TestTrue(TEXT("FCbAttachment(BinaryView).AsBinary()"), Attachment.AsBinary().GetView().EqualBytes(Buffer.GetView()));
		TestFalse(TEXT("FCbAttachment(BinaryView).AsObject()"), bool(Attachment.AsObject()));
		TestTrue(TEXT("FCbAttachment(BinaryView).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryView).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(BinaryView).GetHash()"), Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
	}

	// Object View
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "Name"_ASV << 42;
		Writer.EndObject();
		FCbObject Object = Writer.Save().AsObject();
		FCbObject ObjectView = FCbObject::MakeView(Object);
		FCbAttachment Attachment(ObjectView);
		TestFalse(TEXT("FCbAttachment(ObjectView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(ObjectView) as bool"), bool(Attachment));
		TestNotEqual(TEXT("FCbAttachment(ObjectView).AsBinary()"), Attachment.AsBinary(), ObjectView.GetBuffer());
		TestTrue(TEXT("FCbAttachment(ObjectView).AsObject()"), Attachment.AsObject().Equals(Object));
		TestTrue(TEXT("FCbAttachment(ObjectView).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(ObjectView).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(ObjectView).GetHash()"), Attachment.GetHash(), FIoHash(Object.GetHash()));
	}

	// Binary Load from View
	{
		const uint8 Value[]{0, 1, 2, 3};
		const FSharedBuffer Buffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FCbAttachment Attachment(Buffer);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.Load(FieldsView);
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(LoadBinaryView) as bool"), bool(Attachment));
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).AsBinary()->!InView"),
			FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsBinary().GetView()));
		TestTrue(TEXT("FCbAttachment(LoadBinaryView).AsBinary()->EqualBytes"),
			Attachment.AsBinary().GetView().EqualBytes(Buffer.GetView()));
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).AsObject()"), bool(Attachment.AsObject()));
		TestTrue(TEXT("FCbAttachment(LoadBinaryView).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(LoadBinaryView).GetHash()"),
			Attachment.GetHash(), FIoHash::HashBuffer(MakeMemoryView(Value)));
	}

	// Object Load from View
	{
		FCbWriter ValueWriter;
		ValueWriter.BeginObject();
		ValueWriter << "Name"_ASV << 42;
		ValueWriter.EndObject();
		const FCbObject Value = ValueWriter.Save().AsObject();
		TestEqual(TEXT("FCbAttachment(LoadObjectView).Validate"),
			ValidateCompactBinaryRange(Value.GetView(), ECbValidateMode::All), ECbValidateError::None);
		FCbAttachment Attachment(Value);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.Load(FieldsView);
		TestFalse(TEXT("FCbAttachment(LoadObjectView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(LoadObjectView) as bool"), bool(Attachment));
		TestTrue(TEXT("FCbAttachment(LoadObjectView).AsBinary()->EqualBytes"),
			Attachment.AsBinary().GetView().EqualBytes(Value.GetView()));
		TestFalse(TEXT("FCbAttachment(LoadObjectView).AsObject()->!InView"),
			FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsObject().GetView()));
		TestTrue(TEXT("FCbAttachment(LoadObjectView).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(LoadObjectView).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(LoadObjectView).GetHash()"), Attachment.GetHash(), FIoHash(Value.GetHash()));
	}

	// Binary Null
	{
		const FCbAttachment Attachment(FSharedBuffer{});
		TestTrue(TEXT("FCbAttachment(BinaryNull).IsNull()"), Attachment.IsNull());
		TestFalse(TEXT("FCbAttachment(BinaryNull).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryNull).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(BinaryNull).GetHash()"), Attachment.GetHash(), FIoHash::HashBuffer(FSharedBuffer{}));
	}

	// Binary Empty
	{
		const FCbAttachment Attachment(FUniqueBuffer::Alloc(0).MoveToShared());
		TestFalse(TEXT("FCbAttachment(BinaryEmpty).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(BinaryEmpty).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryEmpty).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(BinaryEmpty).GetHash()"), Attachment.GetHash(), FIoHash::HashBuffer(FSharedBuffer{}));
	}

	// Object Empty
	{
		const FCbAttachment Attachment(FCbObject{});
		TestFalse(TEXT("FCbAttachment(ObjectEmpty).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(ObjectEmpty).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(ObjectEmpty).IsObject()"), Attachment.IsObject());
		TestEqual(TEXT("FCbAttachment(ObjectEmpty).GetHash()"), Attachment.GetHash(), FIoHash(FCbObject().GetHash()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbPackageTest, "System.Core.Serialization.CbPackage", CompactBinaryPackageTestFlags)
bool FCbPackageTest::RunTest(const FString& Parameters)
{
	const auto TestSaveLoadValidate = [this](const TCHAR* Test, const FCbPackage& Package)
	{
		TCbWriter<256> Writer;
		FBufferArchive WriteAr;
		Package.Save(Writer);
		Package.Save(WriteAr);
		FCbFieldIterator Fields = Writer.Save();

		TestTrue(FString::Printf(TEXT("FCbPackage(%s).Save()->Equals"), Test),
			MakeMemoryView(WriteAr).EqualBytes(Fields.GetRangeBuffer().GetView()));
		TestEqual(FString::Printf(TEXT("FCbPackage(%s).Save()->ValidateRange"), Test),
			ValidateCompactBinaryRange(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);
		TestEqual(FString::Printf(TEXT("FCbPackage(%s).Save()->ValidatePackage"), Test),
			ValidateCompactBinaryPackage(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);

		FCbPackage FromFields;
		FromFields.Load(Fields);
		TestFalse(FString::Printf(TEXT("FCbPackage(%s).Load(Iterator)->AtEnd"), Test), bool(Fields));
		TestEqual(FString::Printf(TEXT("FCbPackage(%s).Load(Iterator)->Equals"), Test), FromFields, Package);

		FCbPackage FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.Load(ReadAr);
		TestTrue(FString::Printf(TEXT("FCbPackage(%s).Load(Archive)->AtEnd"), Test), ReadAr.AtEnd());
		TestEqual(FString::Printf(TEXT("FCbPackage(%s).Load(Archive)->Equals"), Test), FromArchive, Package);
	};

	// Empty
	{
		FCbPackage Package;
		TestTrue(TEXT("FCbPackage(Empty).IsNull()"), Package.IsNull());
		TestFalse(TEXT("FCbPackage(Empty) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Empty).GetAttachments()"), Package.GetAttachments().Num(), 0);
		TestSaveLoadValidate(TEXT("Empty"), Package);
	}

	// Object Only
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const FCbObject Object = Writer.Save().AsObject();
		FCbPackage Package(Object);
		TestFalse(TEXT("FCbPackage(Object).IsNull()"), Package.IsNull());
		TestTrue(TEXT("FCbPackage(Object) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Object).GetAttachments()"), Package.GetAttachments().Num(), 0);
		TestEqual(TEXT("FCbPackage(Object).GetObject()->IsClone"), Package.GetObject().GetOuterBuffer(), Object.GetOuterBuffer());
		TestEqual(TEXT("FCbPackage(Object).GetObject()"), Package.GetObject()["Field"].AsInt32(), 42);
		TestEqual(TEXT("FCbPackage(Object).GetObjectHash()"), Package.GetObjectHash(), FIoHash(Package.GetObject().GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Package);
	}

	// Object View Only
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const FCbObject Object = Writer.Save().AsObject();
		FCbPackage Package(FCbObject::MakeView(Object));
		TestFalse(TEXT("FCbPackage(Object).IsNull()"), Package.IsNull());
		TestTrue(TEXT("FCbPackage(Object) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Object).GetAttachments()"), Package.GetAttachments().Num(), 0);
		TestNotEqual(TEXT("FCbPackage(Object).GetObject()->IsClone"), Package.GetObject().GetOuterBuffer(), Object.GetOuterBuffer());
		TestEqual(TEXT("FCbPackage(Object).GetObject()"), Package.GetObject()["Field"].AsInt32(), 42);
		TestEqual(TEXT("FCbPackage(Object).GetObjectHash()"), Package.GetObjectHash(), FIoHash(Package.GetObject().GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Package);
	}

	// Attachment Only
	{
		FCbObject Object1;
		{
			TCbWriter<256> Writer;
			Writer.BeginObject();
			Writer << "Field1" << 42;
			Writer.EndObject();
			Object1 = Writer.Save().AsObject();
		}
		FCbObject Object2;
		{
			TCbWriter<256> Writer;
			Writer.BeginObject();
			Writer << "Field2" << 42;
			Writer.EndObject();
			Object2 = Writer.Save().AsObject();
		}

		FCbPackage Package;
		Package.AddAttachment(FCbAttachment(Object1));
		Package.AddAttachment(FCbAttachment(Object2.GetBuffer()));

		TestFalse(TEXT("FCbPackage(Attachments).IsNull()"), Package.IsNull());
		TestTrue(TEXT("FCbPackage(Attachments) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Attachments).GetAttachments()"), Package.GetAttachments().Num(), 2);
		TestTrue(TEXT("FCbPackage(Attachments).GetObject()"), Package.GetObject().Equals(FCbObject()));
		TestEqual(TEXT("FCbPackage(Attachments).GetObjectHash()"), Package.GetObjectHash(), FIoHash());
		TestSaveLoadValidate(TEXT("Attachments"), Package);

		const FCbAttachment* const Object1Attachment = Package.FindAttachment(Object1.GetHash());
		const FCbAttachment* const Object2Attachment = Package.FindAttachment(Object2.GetHash());

		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(Object1)"),
			Object1Attachment && Object1Attachment->AsObject().Equals(Object1));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(Object2)"),
			Object2Attachment && Object2Attachment->AsBinary() == Object2.GetBuffer());

		Package.AddAttachment(FCbAttachment(FSharedBuffer::Clone(Object1.GetView())));
		Package.AddAttachment(FCbAttachment(FCbObject::Clone(Object2)));

		TestEqual(TEXT("FCbPackage(Attachments).GetAttachments()"), Package.GetAttachments().Num(), 2);
		TestEqual(TEXT("FCbPackage(Attachments).FindAttachment(Object1, Re-Add)"),
			Package.FindAttachment(Object1.GetHash()), Object1Attachment);
		TestEqual(TEXT("FCbPackage(Attachments).FindAttachment(Object2, Re-Add)"),
			Package.FindAttachment(Object2.GetHash()), Object2Attachment);

		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(ObjectAsObject)"),
			Object1Attachment && Object1Attachment->AsObject().Equals(Object1));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(ObjectAsBinary)"),
			Object1Attachment && Object1Attachment->AsBinary() == Object1.GetBuffer());
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(FieldAsField)"),
			Object2Attachment && Object2Attachment->AsObject().Equals(Object2));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(FieldAsBinary)"),
			Object2Attachment && Object2Attachment->AsBinary() == Object2.GetBuffer());

		TestTrue(TEXT("FCbPackage(Attachments).GetAttachments()->Sorted"),
			Algo::IsSorted(Package.GetAttachments()));
	}

	// Shared Values
	const uint8 Level4Values[]{0, 1, 2, 3};
	FSharedBuffer Level4 = FSharedBuffer::MakeView(MakeMemoryView(Level4Values));
	const FIoHash Level4Hash = FIoHash::HashBuffer(Level4);

	FCbObject Level3;
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer.AddBinaryAttachment("Level4", Level4Hash);
		Writer.EndObject();
		Level3 = Writer.Save().AsObject();
	}
	const FIoHash Level3Hash = Level3.GetHash();

	FCbObject Level2;
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer.AddObjectAttachment("Level3", Level3Hash);
		Writer.EndObject();
		Level2 = Writer.Save().AsObject();
	}
	const FIoHash Level2Hash = Level2.GetHash();

	FCbObject Level1;
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer.AddObjectAttachment("Level2", Level2Hash);
		Writer.EndObject();
		Level1 = Writer.Save().AsObject();
	}
	const FIoHash Level1Hash = Level1.GetHash();

	const auto Resolver = [&Level2, &Level2Hash, &Level3, &Level3Hash, &Level4, &Level4Hash]
		(const FIoHash& Hash) -> FSharedBuffer
		{
			return
				Hash == Level2Hash ? Level2.GetBuffer() :
				Hash == Level3Hash ? Level3.GetBuffer() :
				Hash == Level4Hash ? Level4 :
				FSharedBuffer();
		};

	// Object + Attachments
	{
		FCbPackage Package;
		Package.SetObject(Level1, Level1Hash, Resolver);

		TestFalse(TEXT("FCbPackage(Object+Attachments).IsNull()"), Package.IsNull());
		TestTrue(TEXT("FCbPackage(Object+Attachments) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Object+Attachments).GetAttachments()"), Package.GetAttachments().Num(), 3);
		TestTrue(TEXT("FCbPackage(Object+Attachments).GetObject()"),
			Package.GetObject().GetBuffer() == Level1.GetBuffer());
		TestEqual(TEXT("FCbPackage(Object+Attachments).GetObjectHash()"), Package.GetObjectHash(), Level1Hash);
		TestSaveLoadValidate(TEXT("Object+Attachments"), Package);

		const FCbAttachment* const Level2Attachment = Package.FindAttachment(Level2Hash);
		const FCbAttachment* const Level3Attachment = Package.FindAttachment(Level3Hash);
		const FCbAttachment* const Level4Attachment = Package.FindAttachment(Level4Hash);
		TestTrue(TEXT("FCbPackage(Object+Attachments).FindAttachment(Level2)"),
			Level2Attachment && Level2Attachment->AsObject().Equals(Level2));
		TestTrue(TEXT("FCbPackage(Object+Attachments).FindAttachment(Level3)"),
			Level3Attachment && Level3Attachment->AsObject().Equals(Level3));
		TestTrue(TEXT("FCbPackage(Object+Attachments).FindAttachment(Level4)"),
			Level4Attachment &&
			Level4Attachment->AsBinary() != Level4 &&
			Level4Attachment->AsBinary().GetView().EqualBytes(Level4.GetView()));

		TestTrue(TEXT("FCbPackage(Object+Attachments).GetAttachments()->Sorted"),
			Algo::IsSorted(Package.GetAttachments()));

		const FCbPackage PackageCopy = Package;
		TestEqual(TEXT("FCbPackage(Object+Attachments).Equals(EqualCopied)"), PackageCopy, Package);

		TestEqual(TEXT("FCbPackage(Object+Attachments).RemoveAttachment(Level1)"),
			Package.RemoveAttachment(Level1Hash), 0);
		TestEqual(TEXT("FCbPackage(Object+Attachments).RemoveAttachment(Level2)"),
			Package.RemoveAttachment(Level2Hash), 1);
		TestEqual(TEXT("FCbPackage(Object+Attachments).RemoveAttachment(Level3)"),
			Package.RemoveAttachment(Level3Hash), 1);
		TestEqual(TEXT("FCbPackage(Object+Attachments).RemoveAttachment(Level4)"),
			Package.RemoveAttachment(Level4Hash), 1);
		TestEqual(TEXT("FCbPackage(Object+Attachments).RemoveAttachment(Level4, Again)"),
			Package.RemoveAttachment(Level4Hash), 0);
		TestEqual(TEXT("FCbPackage(Object+Attachments).GetAttachments(Removed)"), Package.GetAttachments().Num(), 0);

		TestNotEqual(TEXT("FCbPackage(Object+Attachments).Equals(AttachmentsNotEqual)"), PackageCopy, Package);
		Package = PackageCopy;
		TestEqual(TEXT("FCbPackage(Object+Attachments).Equals(EqualAssigned)"), PackageCopy, Package);
		Package.SetObject(FCbObject());
		TestNotEqual(TEXT("FCbPackage(Object+Attachments).Equals(ObjectNotEqual)"), PackageCopy, Package);
		TestEqual(TEXT("FCbPackage(Object+Attachments).GetObjectHash(Null)"), Package.GetObjectHash(), FIoHash());
	}

	// Out of Order
	{
		TCbWriter<384> Writer;
		Writer.AddBinary(Level2.GetBuffer());
		Writer.AddObjectAttachment(Level2Hash);
		Writer.AddBinary(Level4);
		Writer.AddBinaryAttachment(Level4Hash);
		Writer.AddObject(Level1);
		Writer.AddObjectAttachment(Level1Hash);
		Writer.AddBinary(Level3.GetBuffer());
		Writer.AddObjectAttachment(Level3Hash);
		Writer.AddNull();

		FCbFieldIterator Fields = Writer.Save();
		FCbPackage FromFields;
		FromFields.Load(Fields);

		const FCbAttachment* const Level2Attachment = FromFields.FindAttachment(Level2Hash);
		const FCbAttachment* const Level3Attachment = FromFields.FindAttachment(Level3Hash);
		const FCbAttachment* const Level4Attachment = FromFields.FindAttachment(Level4Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level1"), FromFields.GetObject().Equals(Level1));
		TestEqual(TEXT("FCbPackage(OutOfOrder).Load()->Level1Buffer"),
			FromFields.GetObject().GetOuterBuffer(), Fields.GetOuterBuffer());
		TestEqual(TEXT("FCbPackage(OutOfOrder).Load()->Level1Hash"), FromFields.GetObjectHash(), Level1Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level2"),
			Level2Attachment && Level2Attachment->AsObject().Equals(Level2));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level2Buffer"),
			Level2Attachment && Fields.GetOuterBuffer().GetView().Contains(Level2Attachment->AsBinary().GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level2Hash"),
			Level2Attachment && Level2Attachment->GetHash() == Level2Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3"),
			Level3Attachment && Level3Attachment->AsObject().Equals(Level3));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3Buffer"),
			Level3Attachment && Fields.GetOuterBuffer().GetView().Contains(Level3Attachment->AsBinary().GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3Hash"),
			Level3Attachment && Level3Attachment->GetHash() == Level3Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level4"),
			Level4Attachment && Level4Attachment->AsBinary().GetView().EqualBytes(Level4.GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level4Buffer"),
			Level4Attachment && Fields.GetOuterBuffer().GetView().Contains(Level4Attachment->AsBinary().GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level4Hash"),
			Level4Attachment && Level4Attachment->GetHash() == Level4Hash);

		FBufferArchive WriteAr;
		Writer.Save(WriteAr);
		FCbPackage FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.Load(ReadAr);

		Writer.Reset();
		FromArchive.Save(Writer);
		FCbFieldIterator Saved = Writer.Save();
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level1"), Saved.AsObject().Equals(Level1));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level1Hash"), Saved.AsObjectAttachment(), Level1Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level2"), Saved.AsBinaryView().EqualBytes(Level2.GetView()));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level2Hash"), Saved.AsObjectAttachment(), Level2Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level3"), Saved.AsBinaryView().EqualBytes(Level3.GetView()));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level3Hash"), Saved.AsObjectAttachment(), Level3Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level4"), Saved.AsBinaryView().EqualBytes(Level4.GetView()));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level4Hash"), Saved.AsBinaryAttachment(), Level4Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Null"), Saved.IsNull());
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->AtEnd"), !Saved);
	}

	// Null Attachment
	{
		const FCbAttachment NullAttachment;
		FCbPackage Package;
		Package.AddAttachment(NullAttachment);
		TestTrue(TEXT("FCbPackage(NullAttachment).IsNull()"), Package.IsNull());
		TestFalse(TEXT("FCbPackage(NullAttachment) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(NullAttachment).GetAttachments()"), Package.GetAttachments().Num(), 0);
		TestTrue(TEXT("FCbPackage(NullAttachment).FindAttachment()"), !Package.FindAttachment(NullAttachment));
	}

	// Resolve After Merge
	{
		bool bResolved = false;
		FCbPackage Package;
		Package.AddAttachment(FCbAttachment(Level3.GetBuffer()));
		Package.AddAttachment(FCbAttachment(Level3),
			[&bResolved](const FIoHash& Hash) -> FSharedBuffer
			{
				bResolved = true;
				return FSharedBuffer();
			});
		TestTrue(TEXT("FCbPackage(ResolveAfterMerge)->Resolved"), bResolved);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // WITH_DEV_AUTOMATION_TESTS
