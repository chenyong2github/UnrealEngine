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
		TestFalse(TEXT("FCbAttachment(Null).AsBinaryView()"), bool(Attachment.AsBinaryView()));
		TestFalse(TEXT("FCbAttachment(Null).AsCompactBinary()"), Attachment.AsCompactBinary().HasValue());
		TestFalse(TEXT("FCbAttachment(Null).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(Null).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(Null).GetHash()"), Attachment.GetHash(), FIoHash());
		TestSaveLoadValidate(TEXT("Null"), Attachment);
	}

	// Binary Attachment
	{
		FSharedBuffer Buffer = FSharedBuffer::Clone(MakeMemoryView<uint8>({0, 1, 2, 3}));
		FCbAttachment Attachment(Buffer);
		TestFalse(TEXT("FCbAttachment(Binary).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(Binary) as bool"), bool(Attachment));
		TestEqual(TEXT("FCbAttachment(Binary).AsBinaryView()"), Attachment.AsBinaryView(), Buffer);
		TestFalse(TEXT("FCbAttachment(Binary).AsCompactBinary()"), Attachment.AsCompactBinary().HasValue());
		TestTrue(TEXT("FCbAttachment(Binary).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(Binary).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(Binary).GetHash()"), Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
		TestSaveLoadValidate(TEXT("Binary"), Attachment);
	}

	// Compact Binary Attachment
	{
		FCbWriter Writer;
		Writer << "Name"_ASV << 42;
		FCbFieldIterator Fields = Writer.Save();
		FCbAttachment Attachment(Fields);
		TestFalse(TEXT("FCbAttachment(CompactBinary).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(CompactBinary) as bool"), bool(Attachment));
		TestEqual(TEXT("FCbAttachment(CompactBinary).AsBinaryView()"), Attachment.AsBinaryView(), Fields.GetRangeBuffer());
		TestEqual(TEXT("FCbAttachment(CompactBinary).AsCompactBinary()"), Attachment.AsCompactBinary(), Fields);
		TestTrue(TEXT("FCbAttachment(CompactBinary).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(CompactBinary).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(CompactBinary).GetHash()"), Attachment.GetHash(), Fields.GetRangeHash());
		TestSaveLoadValidate(TEXT("CompactBinary"), Attachment);
	}

	// Binary View
	{
		const uint8 Value[]{0, 1, 2, 3};
		FSharedBuffer Buffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FCbAttachment Attachment(Buffer);
		TestFalse(TEXT("FCbAttachment(BinaryView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(BinaryView) as bool"), bool(Attachment));
		TestNotEqual(TEXT("FCbAttachment(BinaryView).AsBinaryView()"), Attachment.AsBinaryView(), Buffer);
		TestTrue(TEXT("FCbAttachment(BinaryView).AsBinaryView()"),
			Attachment.AsBinaryView().GetView().EqualBytes(Buffer.GetView()));
		TestFalse(TEXT("FCbAttachment(BinaryView).AsCompactBinary()"), Attachment.AsCompactBinary().HasValue());
		TestTrue(TEXT("FCbAttachment(BinaryView).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryView).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(BinaryView).GetHash()"),
			Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
	}

	// Compact Binary View
	{
		FCbWriter Writer;
		Writer << "Name"_ASV << 42;
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));
		FCbAttachment Attachment(FieldsView);
		TestFalse(TEXT("FCbAttachment(CompactBinaryView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(CompactBinaryView) as bool"), bool(Attachment));
		TestNotEqual(TEXT("FCbAttachment(CompactBinaryView).AsBinaryView()"),
			Attachment.AsBinaryView(), FieldsView.GetRangeBuffer());
		TestTrue(TEXT("FCbAttachment(CompactBinaryView).AsCompactBinary()"),
			Attachment.AsCompactBinary().GetRangeView().EqualBytes(Fields.GetRangeView()));
		TestTrue(TEXT("FCbAttachment(CompactBinaryView).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(CompactBinaryView).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(CompactBinaryView).GetHash()"), Attachment.GetHash(), Fields.GetRangeHash());
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
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).AsBinaryView()->!InView"),
			FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsBinaryView().GetView()));
		TestTrue(TEXT("FCbAttachment(LoadBinaryView).AsBinaryView()->EqualBytes"),
			Attachment.AsBinaryView().GetView().EqualBytes(Buffer.GetView()));
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).AsCompactBinary()"), Attachment.AsCompactBinary().HasValue());
		TestTrue(TEXT("FCbAttachment(LoadBinaryView).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(LoadBinaryView).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(LoadBinaryView).GetHash()"),
			Attachment.GetHash(), FIoHash::HashBuffer(MakeMemoryView(Value)));
	}

	// Compact Binary Load from View
	{
		FCbWriter ValueWriter;
		ValueWriter << "Name"_ASV << 42;
		const FCbFieldIterator Value = ValueWriter.Save();
		TestEqual(TEXT("FCbAttachment(LoadCompactBinaryView).Validate"),
			ValidateCompactBinaryRange(Value.GetRangeView(), ECbValidateMode::All), ECbValidateError::None);
		FCbAttachment Attachment(Value);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.Load(FieldsView);
		TestFalse(TEXT("FCbAttachment(LoadCompactBinaryView).IsNull()"), Attachment.IsNull());
		TestTrue(TEXT("FCbAttachment(LoadCompactBinaryView) as bool"), bool(Attachment));
		TestTrue(TEXT("FCbAttachment(LoadCompactBinaryView).AsBinaryView()->EqualBytes"),
			Attachment.AsBinaryView().GetView().EqualBytes(Value.GetRangeView()));
		TestFalse(TEXT("FCbAttachment(LoadCompactBinaryView).AsCompactBinary()->!InView"),
			FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsCompactBinary().GetRangeBuffer().GetView()));
		TestTrue(TEXT("FCbAttachment(LoadCompactBinaryView).IsBinary()"), Attachment.IsBinary());
		TestTrue(TEXT("FCbAttachment(LoadCompactBinaryView).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(LoadCompactBinaryView).GetHash()"), Attachment.GetHash(), Value.GetRangeHash());
	}

	// Compact Binary Uniform Sub-View
	{
		const FSharedBuffer Buffer = FSharedBuffer::Clone(MakeMemoryView<uint8>({0, 1, 2, 3}));
		const FCbFieldViewIterator FieldViews = FCbFieldViewIterator::MakeRange(Buffer.GetView().RightChop(2), ECbFieldType::IntegerPositive);
		const FCbFieldIterator SavedFields = FCbFieldIterator::CloneRange(FieldViews);
		FCbFieldIterator Fields = FCbFieldIterator::MakeRangeView(FieldViews, Buffer);
		FCbAttachment Attachment(Fields);
		const FSharedBuffer Binary = Attachment.AsBinaryView();
		TestEqual(TEXT("FCbAttachment(CompactBinaryUniformSubView).AsCompactBinary()->Equals()"),
			Attachment.AsCompactBinary(), Fields);
		TestEqual(TEXT("FCbAttachment(CompactBinaryUniformSubView).AsBinaryView()->GetSize()"),
			Binary.GetSize(), SavedFields.GetRangeSize());
		TestTrue(TEXT("FCbAttachment(CompactBinaryUniformSubView).AsBinaryView()->EqualBytes()"),
			Binary.GetView().EqualBytes(SavedFields.GetRangeView()));
		TestEqual(TEXT("FCbAttachment(CompactBinaryUniformSubView).GetHash()"),
			Attachment.GetHash(), SavedFields.GetRangeHash());
		TestSaveLoadValidate(TEXT("CompactBinaryUniformSubView"), Attachment);
	}

	// Binary Null
	{
		const FCbAttachment Attachment(FSharedBuffer{});
		TestTrue(TEXT("FCbAttachment(BinaryNull).IsNull()"), Attachment.IsNull());
		TestFalse(TEXT("FCbAttachment(BinaryNull).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryNull).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(BinaryNull).GetHash()"), Attachment.GetHash(), FIoHash());
	}

	// Binary Empty
	{
		const FCbAttachment Attachment(FSharedBuffer(FUniqueBuffer::Alloc(0)));
		TestTrue(TEXT("FCbAttachment(BinaryEmpty).IsNull()"), Attachment.IsNull());
		TestFalse(TEXT("FCbAttachment(BinaryEmpty).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(BinaryEmpty).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(BinaryEmpty).GetHash()"), Attachment.GetHash(), FIoHash());
	}

	// Compact Binary Empty
	{
		const FCbAttachment Attachment(FCbFieldIterator{});
		TestTrue(TEXT("FCbAttachment(CompactBinaryEmpty).IsNull()"), Attachment.IsNull());
		TestFalse(TEXT("FCbAttachment(CompactBinaryEmpty).IsBinary()"), Attachment.IsBinary());
		TestFalse(TEXT("FCbAttachment(CompactBinaryEmpty).IsCompactBinary()"), Attachment.IsCompactBinary());
		TestEqual(TEXT("FCbAttachment(CompactBinaryEmpty).GetHash()"), Attachment.GetHash(), FIoHash());
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
		TestEqual(TEXT("FCbPackage(Object).GetObjectHash()"), Package.GetObjectHash(), Package.GetObject().GetHash());
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
		TestEqual(TEXT("FCbPackage(Object).GetObjectHash()"), Package.GetObjectHash(), Package.GetObject().GetHash());
		TestSaveLoadValidate(TEXT("Object"), Package);
	}

	// Attachment Only
	{
		FCbObject Object;
		{
			TCbWriter<256> Writer;
			Writer.BeginObject();
			Writer << "Field" << 42;
			Writer.EndObject();
			Object = Writer.Save().AsObject();
		}
		FCbField Field = FCbField::Clone(Object["Field"]);

		FCbPackage Package;
		Package.AddAttachment(FCbAttachment(FCbFieldIterator::MakeSingle(Object.AsField())));
		Package.AddAttachment(FCbAttachment(Field.GetBuffer()));

		TestFalse(TEXT("FCbPackage(Attachments).IsNull()"), Package.IsNull());
		TestTrue(TEXT("FCbPackage(Attachments) as bool"), bool(Package));
		TestEqual(TEXT("FCbPackage(Attachments).GetAttachments()"), Package.GetAttachments().Num(), 2);
		TestTrue(TEXT("FCbPackage(Attachments).GetObject()"), Package.GetObject().Equals(FCbObject()));
		TestEqual(TEXT("FCbPackage(Attachments).GetObjectHash()"), Package.GetObjectHash(), FIoHash());
		TestSaveLoadValidate(TEXT("Attachments"), Package);

		const FCbAttachment* const ObjectAttachment = Package.FindAttachment(Object.GetHash());
		const FCbAttachment* const FieldAttachment = Package.FindAttachment(Field.GetHash());

		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(Object)"),
			ObjectAttachment && ObjectAttachment->AsCompactBinary().AsObject().Equals(Object));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(Field)"),
			FieldAttachment && FieldAttachment->AsBinaryView() == Field.GetBuffer());

		Package.AddAttachment(FCbAttachment(FSharedBuffer::Clone(Object.GetView())));
		Package.AddAttachment(FCbAttachment(FCbFieldIterator::CloneRange(FCbFieldViewIterator::MakeSingle(Field))));

		TestEqual(TEXT("FCbPackage(Attachments).GetAttachments()"), Package.GetAttachments().Num(), 2);
		TestEqual(TEXT("FCbPackage(Attachments).FindAttachment(Object, Re-Add)"),
			Package.FindAttachment(Object.GetHash()), ObjectAttachment);
		TestEqual(TEXT("FCbPackage(Attachments).FindAttachment(Field, Re-Add)"),
			Package.FindAttachment(Field.GetHash()), FieldAttachment);

		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(ObjectAsObject)"),
			ObjectAttachment && ObjectAttachment->AsCompactBinary().AsObject().Equals(Object));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(ObjectAsBinary)"),
			ObjectAttachment && ObjectAttachment->AsBinaryView() == Object.GetBuffer());
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(FieldAsField)"),
			FieldAttachment && FieldAttachment->AsCompactBinary().Equals(Field));
		TestTrue(TEXT("FCbPackage(Attachments).FindAttachment(FieldAsBinary)"),
			FieldAttachment && FieldAttachment->AsBinaryView() == Field.GetBuffer());

		TestTrue(TEXT("FCbPackage(Attachments).GetAttachments()->Sorted"),
			Algo::IsSorted(Package.GetAttachments()));
	}

	// Shared Values
	const uint8 Level4Values[]{0, 1, 2, 3};
	FSharedBuffer Level4 = FSharedBuffer::MakeView(MakeMemoryView(Level4Values));
	const FIoHash Level4Hash = FIoHash::HashBuffer(Level4);

	FCbField Level3;
	{
		TCbWriter<256> Writer;
		Writer.SetName("Level4").AddBinaryAttachment(Level4Hash);
		Level3 = Writer.Save();
	}
	const FIoHash Level3Hash = Level3.GetHash();

	FCbArray Level2;
	{
		TCbWriter<256> Writer;
		Writer.SetName("Level3");
		Writer.BeginArray();
		Writer.AddCompactBinaryAttachment(Level3Hash);
		Writer.EndArray();
		Level2 = Writer.Save().AsArray();
	}
	const FIoHash Level2Hash = Level2.AsFieldView().GetHash();

	FCbObject Level1;
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer.SetName("Level2").AddCompactBinaryAttachment(Level2Hash);
		Writer.EndObject();
		Level1 = Writer.Save().AsObject();
	}
	const FIoHash Level1Hash = Level1.AsFieldView().GetHash();

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
			Level2Attachment && Level2Attachment->AsCompactBinary().AsArray().Equals(Level2));
		TestTrue(TEXT("FCbPackage(Object+Attachments).FindAttachment(Level3)"),
			Level3Attachment && Level3Attachment->AsCompactBinary().Equals(Level3));
		TestTrue(TEXT("FCbPackage(Object+Attachments).FindAttachment(Level4)"),
			Level4Attachment &&
			Level4Attachment->AsBinaryView() != Level4 &&
			Level4Attachment->AsBinaryView().GetView().EqualBytes(Level4.GetView()));

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
		Writer.AddCompactBinaryAttachment(Level2Hash);
		Writer.AddBinary(Level4);
		Writer.AddBinaryAttachment(Level4Hash);
		Writer.AddObject(Level1);
		Writer.AddCompactBinaryAttachment(Level1Hash);
		Writer.AddBinary(Level3.GetBuffer());
		Writer.AddCompactBinaryAttachment(Level3Hash);
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
			Level2Attachment && Level2Attachment->AsCompactBinary().AsArray().Equals(Level2));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level2Buffer"),
			Level2Attachment && Fields.GetOuterBuffer().GetView().Contains(Level2Attachment->AsBinaryView().GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level2Hash"),
			Level2Attachment && Level2Attachment->GetHash() == Level2Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3"),
			Level3Attachment && Level3Attachment->AsCompactBinary().Equals(Level3));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3Buffer"),
			Level3Attachment && Fields.GetOuterBuffer().GetView().Contains(Level3Attachment->AsBinaryView().GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level3Hash"),
			Level3Attachment && Level3Attachment->GetHash() == Level3Hash);

		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level4"),
			Level4Attachment && Level4Attachment->AsBinaryView().GetView().EqualBytes(Level4.GetView()));
		TestTrue(TEXT("FCbPackage(OutOfOrder).Load()->Level4Buffer"),
			Level4Attachment && Fields.GetOuterBuffer().GetView().Contains(Level4Attachment->AsBinaryView().GetView()));
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
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level1Hash"), Saved.AsCompactBinaryAttachment(), Level1Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level2"), Saved.AsBinaryView().EqualBytes(Level2.GetView()));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level2Hash"), Saved.AsCompactBinaryAttachment(), Level2Hash);
		++Saved;
		TestTrue(TEXT("FCbPackage(OutOfOrder).Save()->Level3"), Saved.AsBinaryView().EqualBytes(Level3.GetView()));
		++Saved;
		TestEqual(TEXT("FCbPackage(OutOfOrder).Save()->Level3Hash"), Saved.AsCompactBinaryAttachment(), Level3Hash);
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
		Package.AddAttachment(FCbAttachment(FCbFieldIterator::MakeSingle(Level3)),
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
