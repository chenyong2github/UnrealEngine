// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryPackage.h"

#include "Algo/IsSorted.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/MemoryReader.h"
#include "TestFixtures/CoreTestFixture.h"

TEST_CASE_METHOD(FCoreTestFixture, "Core::Serialization::FCbAttachment::CbAttachment", "[Core][Serialization][Smoke]")
{
	const auto TestSaveLoadValidate = [this](const TCHAR* Test, const FCbAttachment& Attachment)
	{
		TCbWriter<256> Writer;
		FBufferArchive WriteAr;
		Attachment.Save(Writer);
		Attachment.Save(WriteAr);
		FCbFieldIterator Fields = Writer.Save();

		INFO(Test);
		CHECK(MakeMemoryView(WriteAr).EqualBytes(Fields.GetOuterBuffer().GetView()));
		CHECK_EQUAL(ValidateCompactBinaryRange(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);
		CHECK_EQUAL(ValidateCompactBinaryAttachment(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);

		FCbAttachment FromFields;
		FromFields.TryLoad(Fields);
		CHECK(!bool(Fields));
		CHECK_EQUAL(FromFields, Attachment);

		FCbAttachment FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.TryLoad(ReadAr);
		CHECK(ReadAr.AtEnd());
		CHECK_EQUAL(FromArchive, Attachment);
	};

	SECTION("Empty Attachment")
	{
		FCbAttachment Attachment;
		CHECK(Attachment.IsNull());
		CHECK_FALSE(bool(Attachment));
		CHECK_FALSE(bool(Attachment.AsBinary()));
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash());
	}

	SECTION("Binary Attachment")
	{
		FSharedBuffer Buffer = FSharedBuffer::Clone(MakeMemoryView<uint8>({0, 1, 2, 3}));
		FCbAttachment Attachment(Buffer);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK_EQUAL(Attachment.AsBinary(), Buffer);
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
		TestSaveLoadValidate(TEXT("Binary"), Attachment);
	}

	SECTION("Compressed Binary Attachment")
	{
		FCompressedBuffer Buffer = FCompressedBuffer::Compress(FSharedBuffer::Clone(MakeMemoryView<uint8>({0, 1, 2, 3})));
		FCbAttachment Attachment(Buffer);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsCompressedBinary().GetCompressed().ToShared().GetView().EqualBytes(Buffer.GetCompressed().ToShared().GetView()));
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash(Buffer.GetRawHash()));
		TestSaveLoadValidate(TEXT("CompressedBinary"), Attachment);
	}

	SECTION("Object Attachment")
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "Name"_ASV << 42;
		Writer.EndObject();
		FCbObject Object = Writer.Save().AsObject();
		FCbAttachment Attachment(Object);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK_EQUAL(Attachment.AsBinary(), FSharedBuffer());
		CHECK(Attachment.AsObject().Equals(Object));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK(Attachment.IsObject());
		CHECK_EQUAL( Attachment.GetHash(), FIoHash(Object.GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Attachment);
	}

	SECTION("Binary View")
	{
		const uint8 Value[]{0, 1, 2, 3};
		FSharedBuffer Buffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FCbAttachment Attachment(Buffer);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK_NOT_EQUAL(Attachment.AsBinary(), Buffer);
		CHECK(Attachment.AsBinary().GetView().EqualBytes(Buffer.GetView()));
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(Buffer));
	}

	SECTION("Object View")
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "Name"_ASV << 42;
		Writer.EndObject();
		FCbObject Object = Writer.Save().AsObject();
		FCbObject ObjectView = FCbObject::MakeView(Object);
		FCbAttachment Attachment(ObjectView);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsObject().Equals(Object));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK(Attachment.IsObject());
		CHECK_EQUAL( Attachment.GetHash(), FIoHash(Object.GetHash()));
	}

	SECTION("Binary Load from View")
	{
		const uint8 Value[]{0, 1, 2, 3};
		const FSharedBuffer Buffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FCbAttachment Attachment(Buffer);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.TryLoad(FieldsView);
		CHECK_FALSE(Attachment.IsNull());
		CHECK( bool(Attachment));
		CHECK_FALSE(FieldsView.GetOuterBuffer().GetView().Contains(Attachment.AsBinary().GetView()));
		CHECK(Attachment.AsBinary().GetView().EqualBytes(Buffer.GetView()));
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(MakeMemoryView(Value)));
	}

	SECTION("Compressed Binary Load from View")
	{
		const uint8 Value[]{0, 1, 2, 3};
		FCompressedBuffer Buffer = FCompressedBuffer::Compress(FSharedBuffer::MakeView(MakeMemoryView(Value)));
		FCbAttachment Attachment(Buffer);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.TryLoad(FieldsView);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK_FALSE(FieldsView.GetOuterBuffer().GetView().Contains(Attachment.AsCompressedBinary().GetCompressed().ToShared().GetView()));
		CHECK(Attachment.AsCompressedBinary().GetCompressed().ToShared().GetView().EqualBytes(Buffer.GetCompressed().ToShared().GetView()));
		CHECK_FALSE(bool(Attachment.AsObject()));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(MakeMemoryView(Value)));
	}

	SECTION("Object Load from View")
	{
		FCbWriter ValueWriter;
		ValueWriter.BeginObject();
		ValueWriter << "Name"_ASV << 42;
		ValueWriter.EndObject();
		const FCbObject Value = ValueWriter.Save().AsObject();
		CHECK_EQUAL(ValidateCompactBinaryRange(Value.GetOuterBuffer(), ECbValidateMode::All), ECbValidateError::None);
		FCbAttachment Attachment(Value);

		FCbWriter Writer;
		Attachment.Save(Writer);
		FCbFieldIterator Fields = Writer.Save();
		FCbFieldIterator FieldsView = FCbFieldIterator::MakeRangeView(FCbFieldViewIterator(Fields));

		Attachment.TryLoad(FieldsView);
		FMemoryView View;
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsBinary().GetView().EqualBytes(FMemoryView()));
		CHECK_FALSE((!Attachment.AsObject().TryGetView(View) || FieldsView.GetOuterBuffer().GetView().Contains(View)));
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash(Value.GetHash()));
	}

	SECTION("Binary Null")
	{
		const FCbAttachment Attachment(FSharedBuffer{});
		CHECK(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL( Attachment.GetHash(), FIoHash::Zero);
	}

	SECTION("Binary Empty")
	{
		const FCbAttachment Attachment(FUniqueBuffer::Alloc(0).MoveToShared());
		CHECK_FALSE(Attachment.IsNull());
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(FSharedBuffer{}));
	}

	SECTION("Compressed Binary Empty")
	{
		const FCbAttachment Attachment(FCompressedBuffer::Compress(FUniqueBuffer::Alloc(0).MoveToShared()));
		CHECK_FALSE(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK(Attachment.IsCompressedBinary());
		CHECK_FALSE(Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash::HashBuffer(FSharedBuffer{}));
	}

	SECTION("Object Empty")
	{
		const FCbAttachment Attachment(FCbObject{});
		CHECK_FALSE(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompressedBinary());
		CHECK( Attachment.IsObject());
		CHECK_EQUAL(Attachment.GetHash(), FIoHash(FCbObject().GetHash()));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbPackage::CbPackage", "[Core][Serialization][Smoke]")
{
	const auto TestSaveLoadValidate = [this](const TCHAR* Test, const FCbPackage& Package)
	{
		TCbWriter<256> Writer;
		FBufferArchive WriteAr;
		Package.Save(Writer);
		Package.Save(WriteAr);
		FCbFieldIterator Fields = Writer.Save();

		INFO(Test);
		CHECK(MakeMemoryView(WriteAr).EqualBytes(Fields.GetOuterBuffer().GetView()));
		CHECK_EQUAL(ValidateCompactBinaryRange(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);
		CHECK_EQUAL(ValidateCompactBinaryPackage(MakeMemoryView(WriteAr), ECbValidateMode::All), ECbValidateError::None);

		FCbPackage FromFields;
		FromFields.TryLoad(Fields);
		CHECK_FALSE(bool(Fields));
		CHECK_EQUAL(FromFields, Package);

		FCbPackage FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.TryLoad(ReadAr);
		CHECK(ReadAr.AtEnd());
		CHECK_EQUAL(FromArchive, Package);
	};

	SECTION("Empty")
	{
		FCbPackage Package;
		CHECK(Package.IsNull());
		CHECK_FALSE(bool(Package));
		CHECK_EQUAL(Package.GetAttachments().Num(), 0);
		TestSaveLoadValidate(TEXT("Empty"), Package);
	}

	SECTION("Object Only")
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const FCbObject Object = Writer.Save().AsObject();
		FCbPackage Package(Object);
		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK_EQUAL(Package.GetAttachments().Num(), 0);
		CHECK_EQUAL(Package.GetObject().GetOuterBuffer(), Object.GetOuterBuffer());
		CHECK_EQUAL(Package.GetObject()["Field"].AsInt32(), 42);
		CHECK_EQUAL(Package.GetObjectHash(), FIoHash(Package.GetObject().GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Package);
	}

	SECTION("Object View Only")
	{
		TCbWriter<256> Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const FCbObject Object = Writer.Save().AsObject();
		FCbPackage Package(FCbObject::MakeView(Object));
		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK_EQUAL(Package.GetAttachments().Num(), 0);
		CHECK_NOT_EQUAL(Package.GetObject().GetOuterBuffer(), Object.GetOuterBuffer());
		CHECK_EQUAL(Package.GetObject()["Field"].AsInt32(), 42);
		CHECK_EQUAL(Package.GetObjectHash(), FIoHash(Package.GetObject().GetHash()));
		TestSaveLoadValidate(TEXT("Object"), Package);
	}

	SECTION("Attachment Only")
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
		Package.AddAttachment(FCbAttachment(Object2.GetOuterBuffer()));

		CHECK(!Package.IsNull());
		CHECK(bool(Package));
		CHECK_EQUAL(Package.GetAttachments().Num(), 2);
		CHECK(Package.GetObject().Equals(FCbObject()));
		CHECK_EQUAL(Package.GetObjectHash(), FIoHash());
		TestSaveLoadValidate(TEXT("Attachments"), Package);

		const FCbAttachment* const Object1Attachment = Package.FindAttachment(Object1.GetHash());
		const FCbAttachment* const Object2Attachment = Package.FindAttachment(Object2.GetHash());

		REQUIRE(Object1Attachment);
		CHECK(Object1Attachment->AsObject().Equals(Object1));
		REQUIRE(Object2Attachment);
		CHECK(Object2Attachment->AsBinary() == Object2.GetOuterBuffer());

		FSharedBuffer Object1ClonedBuffer = FSharedBuffer::Clone(Object1.GetOuterBuffer());
		Package.AddAttachment(FCbAttachment(Object1ClonedBuffer));
		Package.AddAttachment(FCbAttachment(FCbObject::Clone(Object2)));

		CHECK_EQUAL(Package.GetAttachments().Num(), 2);
		CHECK_EQUAL(Package.FindAttachment(Object1.GetHash()), Object1Attachment);
		CHECK_EQUAL(Package.FindAttachment(Object2.GetHash()), Object2Attachment);

		REQUIRE(Object1Attachment);
		CHECK(Object1Attachment->AsBinary() == Object1ClonedBuffer);
		REQUIRE(Object2Attachment);
		CHECK(Object2Attachment->AsObject().Equals(Object2));

		CHECK(Algo::IsSorted(Package.GetAttachments()));
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
				Hash == Level2Hash ? Level2.GetOuterBuffer() :
				Hash == Level3Hash ? Level3.GetOuterBuffer() :
				Hash == Level4Hash ? Level4 :
				FSharedBuffer();
		};

	SECTION("Object + Attachments")
	{
		FCbPackage Package;
		Package.SetObject(Level1, Level1Hash, Resolver);

		CHECK_FALSE(Package.IsNull());
		CHECK_EQUAL(Package.GetAttachments().Num(), 3);
		CHECK(Package.GetObject().GetOuterBuffer() == Level1.GetOuterBuffer());
		CHECK_EQUAL(Package.GetObjectHash(), Level1Hash);
		TestSaveLoadValidate(TEXT("Object+Attachments"), Package);

		const FCbAttachment* const Level2Attachment = Package.FindAttachment(Level2Hash);
		const FCbAttachment* const Level3Attachment = Package.FindAttachment(Level3Hash);
		const FCbAttachment* const Level4Attachment = Package.FindAttachment(Level4Hash);

		REQUIRE(Level2Attachment);
		CHECK(Level2Attachment->AsObject().Equals(Level2));

		REQUIRE(Level3Attachment);
		CHECK(Level3Attachment->AsObject().Equals(Level3));

		REQUIRE(Level4Attachment);
		CHECK(Level4Attachment->AsBinary() != Level4);
		CHECK(Level4Attachment->AsBinary().GetView().EqualBytes(Level4.GetView()));

		CHECK(Algo::IsSorted(Package.GetAttachments()));

		const FCbPackage PackageCopy = Package;
		CHECK_EQUAL(PackageCopy, Package);

		CHECK_EQUAL(Package.RemoveAttachment(Level1Hash), 0);
		CHECK_EQUAL(Package.RemoveAttachment(Level2Hash), 1);
		CHECK_EQUAL(Package.RemoveAttachment(Level3Hash), 1);
		CHECK_EQUAL(Package.RemoveAttachment(Level4Hash), 1);
		CHECK_EQUAL(Package.RemoveAttachment(Level4Hash), 0);
		CHECK_EQUAL(Package.GetAttachments().Num(), 0);

		CHECK_NOT_EQUAL(PackageCopy, Package);
		Package = PackageCopy;
		CHECK_EQUAL(PackageCopy, Package);
		Package.SetObject(FCbObject());
		CHECK_NOT_EQUAL(PackageCopy, Package);
		CHECK_EQUAL(Package.GetObjectHash(), FIoHash());
	}

	SECTION("Out of Order")
	{
		TCbWriter<384> Writer;
		FCbAttachment Attachment2(Level2, Level2Hash);
		Attachment2.Save(Writer);
		FCbAttachment Attachment4(Level4);
		Attachment4.Save(Writer);
		Writer.AddHash(Level1Hash);
		Writer.AddObject(Level1);
		FCbAttachment Attachment3(Level3, Level3Hash);
		Attachment3.Save(Writer);
		Writer.AddNull();

		FCbFieldIterator Fields = Writer.Save();
		FCbPackage FromFields;
		FromFields.TryLoad(Fields);

		const FCbAttachment* const Level2Attachment = FromFields.FindAttachment(Level2Hash);
		const FCbAttachment* const Level3Attachment = FromFields.FindAttachment(Level3Hash);
		const FCbAttachment* const Level4Attachment = FromFields.FindAttachment(Level4Hash);

		CHECK(FromFields.GetObject().Equals(Level1));
		CHECK_EQUAL(FromFields.GetObject().GetOuterBuffer(), Fields.GetOuterBuffer());
		CHECK_EQUAL(FromFields.GetObjectHash(), Level1Hash);

		REQUIRE(Level2Attachment);
		
		CHECK(Level2Attachment->AsObject().Equals(Level2));
		CHECK(Level2Attachment->GetHash() == Level2Hash);
		
		REQUIRE(Level3Attachment);
		CHECK(Level3Attachment->AsObject().Equals(Level3));
		CHECK(Level3Attachment->GetHash() == Level3Hash);

		REQUIRE(Level4Attachment);
		CHECK(Level4Attachment->AsBinary().GetView().EqualBytes(Level4.GetView()));
		CHECK(Fields.GetOuterBuffer().GetView().Contains(Level4Attachment->AsBinary().GetView()));
		CHECK(Level4Attachment->GetHash() == Level4Hash);

		FBufferArchive WriteAr;
		Writer.Save(WriteAr);
		FCbPackage FromArchive;
		FMemoryReader ReadAr(WriteAr);
		FromArchive.TryLoad(ReadAr);

		Writer.Reset();
		FromArchive.Save(Writer);
		FCbFieldIterator Saved = Writer.Save();
		FMemoryView View;
		CHECK_EQUAL(Saved.AsHash(), Level1Hash);
		++Saved;
		CHECK(Saved.AsObject().Equals(Level1));
		++Saved;
		CHECK_EQUAL(Saved.AsObjectAttachment(), Level2Hash);
		++Saved;
		CHECK(Saved.AsObject().Equals(Level2));
		++Saved;
		CHECK_EQUAL(Saved.AsObjectAttachment(), Level3Hash);
		++Saved;
		CHECK(Saved.AsObject().Equals(Level3));
		++Saved;
		CHECK_EQUAL(Saved.AsBinaryAttachment(), Level4Hash);
		++Saved;
		FSharedBuffer SavedLevel4Buffer = FSharedBuffer::MakeView(Saved.AsBinaryView());
		CHECK(SavedLevel4Buffer.GetView().EqualBytes(Level4.GetView()));
		++Saved;
		CHECK(Saved.IsNull());
		++Saved;
		CHECK(!Saved);
	}

	SECTION("Null Attachment")
	{
		const FCbAttachment NullAttachment;
		FCbPackage Package;
		Package.AddAttachment(NullAttachment);
		CHECK(Package.IsNull());
		CHECK_FALSE(bool(Package));
		CHECK_EQUAL(Package.GetAttachments().Num(), 0);
		CHECK(!Package.FindAttachment(NullAttachment));
	}

	SECTION("Resolve After Merge")
	{
		bool bResolved = false;
		FCbPackage Package;
		Package.AddAttachment(FCbAttachment(Level3.GetOuterBuffer()));
		Package.AddAttachment(FCbAttachment(Level3),
			[&bResolved](const FIoHash& Hash) -> FSharedBuffer
			{
				bResolved = true;
				return FSharedBuffer();
			});
		CHECK(bResolved);
	}
}