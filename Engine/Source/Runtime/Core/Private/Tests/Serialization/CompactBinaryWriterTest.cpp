// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryWriter.h"

#include "Misc/AutomationTest.h"
#include "Misc/Blake3.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryValidation.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags::Type CompactBinaryWriterTestFlags = EAutomationTestFlags::Type(EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterObjectTest, "System.Core.Serialization.CbWriter.Object", CompactBinaryWriterTestFlags)
bool FCbWriterObjectTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Empty Object
	{
		Writer.Reset();
		Writer.BeginObject();
		Writer.EndObject();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Object, Empty) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Object, Empty).IsObject()"), Field.IsObject());
			TestFalse(TEXT("FCbWriter(Object, Empty).AsObject()"), Field.AsObject().CreateIterator().HasValue());
		}
	}

	// Test Named Empty Object
	{
		Writer.Reset();
		Writer.Name("Object"_ASV);
		Writer.BeginObject();
		Writer.EndObject();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Object, Empty, Name) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Object, Empty, Name).IsObject()"), Field.IsObject());
			TestFalse(TEXT("FCbWriter(Object, Empty, Name).AsObject()"), Field.AsObject().CreateIterator().HasValue());
		}
	}

	// Test Basic Object
	{
		Writer.Reset();
		Writer.BeginObject();
		Writer.Name("Integer"_ASV).Integer(0);
		Writer.Name("Float"_ASV).Float(0.0f);
		Writer.EndObject();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Object, Basic) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Object, Basic).IsObject()"), Field.IsObject());
			FCbObject Object = Field.AsObject();
			TestTrue(TEXT("FCbWriter(Object, Basic).AsObject()[Integer]"), Object["Integer"_ASV].IsInteger());
			TestTrue(TEXT("FCbWriter(Object, Basic).AsObject()[Float]"), Object["Float"_ASV].IsFloat());
		}
	}

	// Test Uniform Object
	{
		Writer.Reset();
		Writer.BeginObject();
		Writer.Name("Field1"_ASV).Integer(0);
		Writer.Name("Field2"_ASV).Integer(1);
		Writer.EndObject();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Object, Uniform) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Object, Uniform).IsObject()"), Field.IsObject());
			FCbObject Object = Field.AsObject();
			TestTrue(TEXT("FCbWriter(Object, Uniform).AsObject()[Field1]"), Object["Field1"_ASV].IsInteger());
			TestTrue(TEXT("FCbWriter(Object, Uniform).AsObject()[Field2]"), Object["Field2"_ASV].IsInteger());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterArrayTest, "System.Core.Serialization.CbWriter.Array", CompactBinaryWriterTestFlags)
bool FCbWriterArrayTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Empty Array
	{
		Writer.Reset();
		Writer.BeginArray();
		Writer.EndArray();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Array, Empty) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Array, Empty).IsArray()"), Field.IsArray());
			TestEqual(TEXT("FCbWriter(Array, Empty).AsArray()"), Field.AsArray().Num(), uint64(0));
		}
	}

	// Test Named Empty Array
	{
		Writer.Reset();
		Writer.Name("Array"_ASV);
		Writer.BeginArray();
		Writer.EndArray();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Array, Empty, Name) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Array, Empty, Name).IsArray()"), Field.IsArray());
			TestEqual(TEXT("FCbWriter(Array, Empty, Name).AsArray()"), Field.AsArray().Num(), uint64(0));
		}
	}

	// Test Basic Array
	{
		Writer.Reset();
		Writer.BeginArray();
		Writer.Integer(0);
		Writer.Float(0.0f);
		Writer.EndArray();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Array, Basic) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Array, Basic).IsArray()"), Field.IsArray());
			FCbFieldIterator Iterator = Field.AsArray().CreateIterator();
			TestTrue(TEXT("FCbWriter(Array, Basic).AsArray()[Integer]"), Iterator.IsInteger());
			++Iterator;
			TestTrue(TEXT("FCbWriter(Array, Basic).AsArray()[Float]"), Iterator.IsFloat());
			++Iterator;
			TestFalse(TEXT("FCbWriter(Array, Basic).AsArray()[End]"), Iterator.HasValue());
		}
	}

	// Test Uniform Array
	{
		Writer.Reset();
		Writer.BeginArray();
		Writer.Integer(0);
		Writer.Integer(1);
		Writer.EndArray();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Array, Uniform) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Array, Uniform).IsArray()"), Field.IsArray());
			FCbFieldIterator Iterator = Field.AsArray().CreateIterator();
			TestTrue(TEXT("FCbWriter(Array, Basic).AsArray()[Field1]"), Iterator.IsInteger());
			++Iterator;
			TestTrue(TEXT("FCbWriter(Array, Basic).AsArray()[Field2]"), Iterator.IsInteger());
			++Iterator;
			TestFalse(TEXT("FCbWriter(Array, Basic).AsArray()[End]"), Iterator.HasValue());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterNullTest, "System.Core.Serialization.CbWriter.Null", CompactBinaryWriterTestFlags)
bool FCbWriterNullTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Null
	{
		Writer.Reset();
		Writer.Null();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Null) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestFalse(TEXT("FCbWriter(Null).HasName()"), Field.HasName());
			TestTrue(TEXT("FCbWriter(Null).IsNull()"), Field.IsNull());
		}
	}

	// Test Null with Name
	{
		Writer.Reset();
		Writer.Name("Null"_ASV);
		Writer.Null();
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Null, Name) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Null, Name).GetName()"), Field.GetName(), "Null"_ASV);
			TestTrue(TEXT("FCbWriter(Null, Name).HasName()"), Field.HasName());
			TestTrue(TEXT("FCbWriter(Null, Name).IsNull()"), Field.IsNull());
		}
	}

	// Test Null Array/Object Uniformity
	{
		Writer.Reset();

		Writer.BeginArray();
		Writer.Null();
		Writer.Null();
		Writer.Null();
		Writer.EndArray();

		Writer.BeginObject();
		Writer.Name("N1"_ASV).Null();
		Writer.Name("N2"_ASV).Null();
		Writer.Name("N3"_ASV).Null();
		Writer.EndObject();

		FCbFieldRefIterator Fields = Writer.Save();
		TestEqual(TEXT("FCbWriter(Null, Uniform) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None);
	}

	// Test Null with SaveToMemory
	{
		Writer.Reset();
		constexpr int NullCount = 3;
		for (int Index = 0; Index < NullCount; ++Index)
		{
			Writer.Null();
		}
		uint8 Buffer[NullCount]{};
		FCbFieldIterator Fields = Writer.SaveToMemory(MakeMemoryView(Buffer));
		if (TestEqual(TEXT("FCbWriter(Null, Memory) Validate"), ValidateCompactBinaryRange(MakeMemoryView(Buffer), ECbValidateMode::All), ECbValidateError::None))
		{
			for (int Index = 0; Index < NullCount; ++Index)
			{
				TestTrue(TEXT("FCbWriter(Null, Memory) IsNull"), Fields.IsNull());
				++Fields;
			}
			TestFalse(TEXT("FCbWriter(Null, Memory) HasValue"), Fields.HasValue());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbWriterBinaryTestBase : public FAutomationTestBase
{
protected:
	using FAutomationTestBase::FAutomationTestBase;
	using FAutomationTestBase::TestEqual;

	template <typename T, typename Size>
	void TestEqual(const TCHAR* What, TArrayView<T, Size> Actual, TArrayView<T, Size> Expected)
	{
		TestTrue(What, Actual.Num() == Expected.Num() && CompareItems(Actual.GetData(), Expected.GetData(), Actual.Num()));
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbWriterBinaryTest, FCbWriterBinaryTestBase, "System.Core.Serialization.CbWriter.Binary", CompactBinaryWriterTestFlags)
bool FCbWriterBinaryTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Empty Binary
	{
		Writer.Reset();
		Writer.Binary(nullptr, 0);
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Binary, Empty) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestFalse(TEXT("FCbWriter(Binary, Empty).HasName()"), Field.HasName());
			TestTrue(TEXT("FCbWriter(Binary, Empty).IsBinary()"), Field.IsBinary());
			TestTrue(TEXT("FCbWriter(Binary, Empty).AsBinary()"), Field.AsBinary().IsEmpty());
		}
	}

	// Test Basic Binary
	{
		Writer.Reset();
		const uint8 BinaryValue[] = { 1, 2, 3, 4, 5, 6 };
		Writer.Name("Binary"_ASV);
		Writer.Binary(BinaryValue, sizeof(BinaryValue));
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Binary, Array) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Binary, Array).GetName()"), Field.GetName(), "Binary"_ASV);
			TestTrue(TEXT("FCbWriter(Binary, Array).HasName()"), Field.HasName());
			TestTrue(TEXT("FCbWriter(Binary, Array).IsBinary()"), Field.IsBinary());
			TestTrue(TEXT("FCbWriter(Binary, Array).AsBinary()"), Field.AsBinary().EqualBytes(MakeMemoryView(BinaryValue)));
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterStringTest, "System.Core.Serialization.CbWriter.String", CompactBinaryWriterTestFlags)
bool FCbWriterStringTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Empty Strings
	{
		Writer.Reset();
		Writer.String(FAnsiStringView());
		Writer.String(FWideStringView());
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(String, Empty) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			for (FCbField Field : Fields)
			{
				TestFalse(TEXT("FCbWriter(String, Empty).HasName()"), Field.HasName());
				TestTrue(TEXT("FCbWriter(String, Empty).IsString()"), Field.IsString());
				TestTrue(TEXT("FCbWriter(String, Empty).AsString()"), Field.AsString().IsEmpty());
			}
		}
	}

	// Test Basic Strings
	{
		Writer.Reset();
		Writer.Name("String"_ASV).String("Value"_ASV);
		Writer.Name("String"_ASV).String(TEXT("Value"_SV));
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(String, Basic) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			for (FCbField Field : Fields)
			{
				TestEqual(TEXT("FCbWriter(String, Basic).GetName()"), Field.GetName(), "String"_ASV);
				TestTrue(TEXT("FCbWriter(String, Basic).HasName()"), Field.HasName());
				TestTrue(TEXT("FCbWriter(String, Basic).IsString()"), Field.IsString());
				TestEqual(TEXT("FCbWriter(String, Basic).AsString()"), Field.AsString(), "Value"_ASV);
			}
		}
	}

	// Test Long Strings
	{
		Writer.Reset();
		constexpr int DotCount = 256;
		TAnsiStringBuilder<DotCount + 1> Dots;
		for (int Index = 0; Index < DotCount; ++Index)
		{
			Dots << '.';
		}
		Writer.String(Dots);
		Writer.String(FString::ChrN(DotCount, TEXT('.')));
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(String, Long) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			for (FCbField Field : Fields)
			{
				TestEqual(TEXT("FCbWriter(String, Long).AsString()"), Field.AsString(), FAnsiStringView(Dots));
			}
		}
	}

	// Test Non-ASCII String
	{
		Writer.Reset();
		WIDECHAR Value[2] = { 0xd83d, 0xde00 };
		Writer.String("\xf0\x9f\x98\x80"_ASV);
		Writer.String(FWideStringView(Value, UE_ARRAY_COUNT(Value)));
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(String, Unicode) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			for (FCbField Field : Fields)
			{
				TestEqual(TEXT("FCbWriter(String, Unicode).AsString()"), Field.AsString(), "\xf0\x9f\x98\x80"_ASV);
			}
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterIntegerTest, "System.Core.Serialization.CbWriter.Integer", CompactBinaryWriterTestFlags)
bool FCbWriterIntegerTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	auto TestInt32 = [this, &Writer](int32 Value)
	{
		Writer.Reset();
		Writer.Integer(Value);
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Integer, Int32) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Integer, Int32) Value"), Field.AsInt32(), Value);
			TestFalse(TEXT("FCbWriter(Integer, Int32) Error"), Field.HasError());
		}
	};

	auto TestUInt32 = [this, &Writer](uint32 Value)
	{
		Writer.Reset();
		Writer.Integer(Value);
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Integer, UInt32) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Integer, UInt32) Value"), Field.AsUInt32(), Value);
			TestFalse(TEXT("FCbWriter(Integer, UInt32) Error"), Field.HasError());
		}
	};

	auto TestInt64 = [this, &Writer](int64 Value)
	{
		Writer.Reset();
		Writer.Integer(Value);
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Integer, Int64) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Integer, Int64) Value"), Field.AsInt64(), Value);
			TestFalse(TEXT("FCbWriter(Integer, Int64) Error"), Field.HasError());
		}
	};

	auto TestUInt64 = [this, &Writer](uint64 Value)
	{
		Writer.Reset();
		Writer.Integer(Value);
		FCbFieldRef Field = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Integer, UInt64) Validate"), ValidateCompactBinary(*Field.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestEqual(TEXT("FCbWriter(Integer, UInt64) Value"), Field.AsUInt64(), Value);
			TestFalse(TEXT("FCbWriter(Integer, UInt64) Error"), Field.HasError());
		}
	};

	TestUInt32(uint32(0x00));
	TestUInt32(uint32(0x7f));
	TestUInt32(uint32(0x80));
	TestUInt32(uint32(0xff));
	TestUInt32(uint32(0x0100));
	TestUInt32(uint32(0x7fff));
	TestUInt32(uint32(0x8000));
	TestUInt32(uint32(0xffff));
	TestUInt32(uint32(0x0001'0000));
	TestUInt32(uint32(0x7fff'ffff));
	TestUInt32(uint32(0x8000'0000));
	TestUInt32(uint32(0xffff'ffff));

	TestUInt64(uint64(0x0000'0001'0000'0000));
	TestUInt64(uint64(0x7fff'ffff'ffff'ffff));
	TestUInt64(uint64(0x8000'0000'0000'0000));
	TestUInt64(uint64(0xffff'ffff'ffff'ffff));

	TestInt32(int32(0x01));
	TestInt32(int32(0x80));
	TestInt32(int32(0x81));
	TestInt32(int32(0x8000));
	TestInt32(int32(0x8001));
	TestInt32(int32(0x7fff'ffff));
	TestInt32(int32(0x8000'0000));
	TestInt32(int32(0x8000'0001));

	TestInt64(int64(0x0000'0001'0000'0000));
	TestInt64(int64(0x8000'0000'0000'0000));
	TestInt64(int64(0x7fff'ffff'ffff'ffff));
	TestInt64(int64(0x8000'0000'0000'0001));
	TestInt64(int64(0xffff'ffff'ffff'ffff));

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterFloatTest, "System.Core.Serialization.CbWriter.Float", CompactBinaryWriterTestFlags)
bool FCbWriterFloatTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Float32
	{
		Writer.Reset();
		constexpr float Values[] = { 0.0f, 1.0f, -1.0f, PI };
		for (float Value : Values)
		{
			Writer.Float(Value);
		}
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Float, Single) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			const float* CheckValue = Values;
			for (FCbField Field : Fields)
			{
				TestEqual(TEXT("FCbWriter(Float, Single).AsFloat()"), Field.AsFloat(), *CheckValue++);
				TestFalse(TEXT("FCbWriter(Float, Single) Error"), Field.HasError());
			}
		}
	}

	// Test Float64
	{
		Writer.Reset();
		constexpr double Values[] = { 0.0f, 1.0f, -1.0f, PI, 1.9999998807907104, 1.9999999403953552, 3.4028234663852886e38, 6.8056469327705771e38 };
		for (double Value : Values)
		{
			Writer.Float(Value);
		}
		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Float, Double) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			const double* CheckValue = Values;
			for (FCbField Field : Fields)
			{
				TestEqual(TEXT("FCbWriter(Float, Double).AsDouble()"), Field.AsDouble(), *CheckValue++);
				TestFalse(TEXT("FCbWriter(Float, Double) Error"), Field.HasError());
			}
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterBoolTest, "System.Core.Serialization.CbWriter.Bool", CompactBinaryWriterTestFlags)
bool FCbWriterBoolTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	// Test Bool Values
	{
		Writer.Bool(true);
		Writer.Bool(false);

		FCbFieldRefIterator Fields = Writer.Save();
		if (TestEqual(TEXT("FCbWriter(Bool) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
		{
			TestTrue(TEXT("FCbWriter(Bool).AsBool()"), Fields.AsBool());
			TestFalse(TEXT("FCbWriter(Bool) Error"), Fields.HasError());
			++Fields;
			TestFalse(TEXT("FCbWriter(Bool).AsBool()"), Fields.AsBool());
			TestFalse(TEXT("FCbWriter(Bool) Error"), Fields.HasError());
		}
	}

	// Test Bool Array/Object Uniformity
	{
		Writer.Reset();

		Writer.BeginArray();
		Writer.Bool(false);
		Writer.Bool(false);
		Writer.Bool(false);
		Writer.EndArray();

		Writer.BeginObject();
		Writer.Name("B1"_ASV).Bool(false);
		Writer.Name("B2"_ASV).Bool(false);
		Writer.Name("B3"_ASV).Bool(false);
		Writer.EndObject();

		FCbFieldRefIterator Fields = Writer.Save();
		TestEqual(TEXT("FCbWriter(Bool, Uniform) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterReferenceTest, "System.Core.Serialization.CbWriter.Reference", CompactBinaryWriterTestFlags)
bool FCbWriterReferenceTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	const FBlake3Hash Values[] = { FBlake3Hash(ZeroBytes), FBlake3Hash(SequentialBytes) };
	for (const FBlake3Hash& Value : Values)
	{
		Writer.Reference(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(Reference) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FBlake3Hash* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(Reference).AsReference()"), Field.AsReference(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(Reference) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterBinaryReferenceTest, "System.Core.Serialization.CbWriter.BinaryReference", CompactBinaryWriterTestFlags)
bool FCbWriterBinaryReferenceTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	const FBlake3Hash Values[] = { FBlake3Hash(ZeroBytes), FBlake3Hash(SequentialBytes) };
	for (const FBlake3Hash& Value : Values)
	{
		Writer.BinaryReference(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(BinaryReference) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FBlake3Hash* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(BinaryReference).AsBinaryReference()"), Field.AsBinaryReference(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(BinaryReference) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterHashTest, "System.Core.Serialization.CbWriter.Hash", CompactBinaryWriterTestFlags)
bool FCbWriterHashTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	const FBlake3Hash Values[] = { FBlake3Hash(ZeroBytes), FBlake3Hash(SequentialBytes) };
	for (const FBlake3Hash& Value : Values)
	{
		Writer.Hash(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(Hash) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FBlake3Hash* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(Hash).AsHash()"), Field.AsHash(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(Hash) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterUuidTest, "System.Core.Serialization.CbWriter.Uuid", CompactBinaryWriterTestFlags)
bool FCbWriterUuidTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FGuid Values[] = { FGuid(), FGuid::NewGuid() };
	for (const FGuid& Value : Values)
	{
		Writer.Uuid(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(Uuid) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FGuid* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(Uuid).AsUuid()"), Field.AsUuid(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(Uuid) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterDateTimeTest, "System.Core.Serialization.CbWriter.DateTime", CompactBinaryWriterTestFlags)
bool FCbWriterDateTimeTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FDateTime Values[] = { FDateTime(0), FDateTime(2020, 5, 13, 15, 10) };
	for (FDateTime Value : Values)
	{
		Writer.DateTime(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(DateTime) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FDateTime* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(DateTime).AsDateTime()"), Field.AsDateTime(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(DateTime) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterTimeSpanTest, "System.Core.Serialization.CbWriter.TimeSpan", CompactBinaryWriterTestFlags)
bool FCbWriterTimeSpanTest::RunTest(const FString& Parameters)
{
	TCbWriter<256> Writer;

	const FTimespan Values[] = { FTimespan(0), FTimespan(1, 2, 4, 8) };
	for (FTimespan Value : Values)
	{
		Writer.TimeSpan(Value);
	}

	FCbFieldRefIterator Fields = Writer.Save();
	if (TestEqual(TEXT("FCbWriter(TimeSpan) Validate"), ValidateCompactBinaryRange(*Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None))
	{
		const FTimespan* CheckValue = Values;
		for (FCbField Field : Fields)
		{
			TestEqual(TEXT("FCbWriter(TimeSpan).AsTimeSpan()"), Field.AsTimeSpan(), *CheckValue++);
			TestFalse(TEXT("FCbWriter(TimeSpan) Error"), Field.HasError());
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterComplexTest, "System.Core.Serialization.CbWriter.Complex", CompactBinaryWriterTestFlags)
bool FCbWriterComplexTest::RunTest(const FString& Parameters)
{
	FCbObjectRef Object;
	{
		FCbWriter Writer;
		Writer.BeginObject();

		const uint8 LocalField[] = { uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName), 1, 'I', 42 };
		Writer.Name("FieldCopy"_ASV).Field(FCbField(LocalField));

		const uint8 LocalObject[] = { uint8(ECbFieldType::Object | ECbFieldType::HasFieldName), 1, 'O', 7,
			uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName), 1, 'I', 42,
			uint8(ECbFieldType::Null | ECbFieldType::HasFieldName), 1, 'N' };
		Writer.Name("ObjectCopy"_ASV).Object(FCbObject(LocalObject));

		const uint8 LocalArray[] = { uint8(ECbFieldType::UniformArray | ECbFieldType::HasFieldName), 1, 'A', 4, 2,
			uint8(ECbFieldType::IntegerPositive), 42, 21 };
		Writer.Name("ArrayCopy"_ASV).Array(FCbArray(LocalArray));

		Writer.Name("Null"_ASV).Null();

		Writer.Name("Binary"_ASV);
		Writer.BeginObject();
		{
			Writer.Name("Empty"_ASV).Binary(FConstMemoryView());
			Writer.Name("Value"_ASV).Binary(MakeMemoryView("BinaryValue"));
			Writer.Name("LargeValue"_ASV).Binary(MakeMemoryView(FString::ChrN(256, TEXT('.'))));
		}
		Writer.EndObject();

		Writer.Name("Strings"_ASV);
		Writer.BeginObject();
		{
			Writer.Name("AnsiString"_ASV).String("AnsiValue"_ASV);
			Writer.Name("WideString"_ASV).String(FString::ChrN(256, TEXT('.')));
			Writer.Name("EmptyAnsiString"_ASV).String(FAnsiStringView());
			Writer.Name("EmptyWideString"_ASV).String(FWideStringView());
		}
		Writer.EndObject();

		Writer.Name("Integers"_ASV);
		Writer.BeginArray();
		{
			Writer.Integer(int32(-1));
			Writer.Integer(int64(-1));
			Writer.Integer(uint32(1));
			Writer.Integer(uint64(1));
			Writer.Integer(MIN_int32);
			Writer.Integer(MAX_int32);
			Writer.Integer(MAX_uint32);
			Writer.Integer(MIN_int64);
			Writer.Integer(MAX_int64);
			Writer.Integer(MAX_uint64);
		}
		Writer.EndArray();

		Writer.Name("UniformIntegers"_ASV);
		Writer.BeginArray();
		{
			Writer.Integer(0);
			Writer.Integer(MAX_int32);
			Writer.Integer(MAX_uint32);
			Writer.Integer(MAX_int64);
			Writer.Integer(MAX_uint64);
		}
		Writer.EndArray();

		Writer.Name("Float32"_ASV).Float(1.0f);
		Writer.Name("Float64as32"_ASV).Float(2.0);
		Writer.Name("Float64"_ASV).Float(3.0e100);

		Writer.Name("False"_ASV).Bool(false);
		Writer.Name("True"_ASV).Bool(true);

		Writer.Name("Reference"_ASV).Reference(FBlake3Hash());
		Writer.Name("BinaryReference"_ASV).BinaryReference(FBlake3Hash());

		Writer.Name("Hash"_ASV).Hash(FBlake3Hash());
		Writer.Name("Uuid"_ASV).Uuid(FGuid());

		Writer.Name("DateTimeZero"_ASV).DateTimeTicks(0);
		Writer.Name("DateTime2020"_ASV).DateTime(FDateTime(2020, 5, 13, 15, 10));

		Writer.Name("TimeSpanZero"_ASV).TimeSpanTicks(0);
		Writer.Name("TimeSpan"_ASV).TimeSpan(FTimespan(1, 2, 4, 8));

		Writer.Name("NestedObjects"_ASV);
		Writer.BeginObject();
		{
			Writer.Name("Empty"_ASV);
			Writer.BeginObject();
			Writer.EndObject();

			Writer.Name("Null"_ASV);
			Writer.BeginObject();
			Writer.Name("Null"_ASV).Null();
			Writer.EndObject();
		}
		Writer.EndObject();

		Writer.Name("NestedArrays"_ASV);
		Writer.BeginArray();
		{
			Writer.BeginArray();
			Writer.EndArray();

			Writer.BeginArray();
			Writer.Null();
			Writer.Null();
			Writer.Null();
			Writer.EndArray();

			Writer.BeginArray();
			Writer.Bool(false);
			Writer.Bool(false);
			Writer.Bool(false);
			Writer.EndArray();

			Writer.BeginArray();
			Writer.Bool(true);
			Writer.Bool(true);
			Writer.Bool(true);
			Writer.EndArray();
		}
		Writer.EndArray();

		Writer.Name("ArrayOfObjects"_ASV);
		Writer.BeginArray();
		{
			Writer.BeginObject();
			Writer.EndObject();

			Writer.BeginObject();
			Writer.Name("Null"_ASV).Null();
			Writer.EndObject();
		}
		Writer.EndArray();

		Writer.Name("LargeArray"_ASV);
		Writer.BeginArray();
		for (int Index = 0; Index < 256; ++Index)
		{
			Writer.Integer(Index - 128);
		}
		Writer.EndArray();

		Writer.Name("LargeUniformArray"_ASV);
		Writer.BeginArray();
		for (int Index = 0; Index < 256; ++Index)
		{
			Writer.Integer(Index);
		}
		Writer.EndArray();

		Writer.Name("NestedUniformArray"_ASV);
		Writer.BeginArray();
		for (int Index = 0; Index < 16; ++Index)
		{
			Writer.BeginArray();
			for (int Value = 0; Value < 4; ++Value)
			{
				Writer.Integer(Value);
			}
			Writer.EndArray();
		}
		Writer.EndArray();

		Writer.EndObject();
		Object = Writer.Save().AsObjectRef();
	}

	TestEqual(TEXT("FCbWriter(Complex) Validate"), ValidateCompactBinary(*Object.GetBuffer(), ECbValidateMode::All), ECbValidateError::None);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterOwnedReadOnlyTest, "System.Core.Serialization.CbWriter.OwnedReadOnly", CompactBinaryWriterTestFlags)
bool FCbWriterOwnedReadOnlyTest::RunTest(const FString& Parameters)
{
	FCbWriter Writer;
	Writer.BeginObject();
	Writer.EndObject();
	FCbObjectRef Object = Writer.Save().AsObjectRef();
	TestTrue(TEXT("FCbWriter().Save().IsOwned()"), Object.IsOwned());
	TestTrue(TEXT("FCbWriter().Save().IsReadOnly()"), Object.IsReadOnly());

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterStreamTest, "System.Core.Serialization.CbWriter.Stream", CompactBinaryWriterTestFlags)
bool FCbWriterStreamTest::RunTest(const FString& Parameters)
{
	FCbObjectRef Object;
	{
		FCbWriter Writer;
		Writer.BeginObject();

		const uint8 LocalField[] = { uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName), 1, 'I', 42 };
		Writer << "FieldCopy"_ASV << FCbField(LocalField);

		const uint8 LocalObject[] = { uint8(ECbFieldType::Object | ECbFieldType::HasFieldName), 1, 'O', 7,
			uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName), 1, 'I', 42,
			uint8(ECbFieldType::Null | ECbFieldType::HasFieldName), 1, 'N' };
		Writer << "ObjectCopy"_ASV << FCbObject(LocalObject);

		const uint8 LocalArray[] = { uint8(ECbFieldType::UniformArray | ECbFieldType::HasFieldName), 1, 'A', 4, 2,
			uint8(ECbFieldType::IntegerPositive), 42, 21 };
		Writer << "ArrayCopy"_ASV << FCbArray(LocalArray);

		Writer << "Null"_ASV << nullptr;

		Writer << "Strings"_ASV;
		Writer.BeginObject();
		Writer
			<< "AnsiString"_ASV << "AnsiValue"_ASV
			<< "WideString"_ASV << TEXT("WideValue"_SV);
		Writer.EndObject();

		Writer << "Integers"_ASV;
		Writer.BeginArray();
		Writer << int32(-1) << int64(-1) << uint32(1) << uint64(1);
		Writer.EndArray();

		Writer << "Float32"_ASV << 1.0f;
		Writer << "Float64"_ASV << 2.0;

		Writer << "False"_ASV << false << "True"_ASV << true;

		Writer << "Hash"_ASV << FBlake3Hash();
		Writer << "Uuid"_ASV << FGuid();

		Writer << "DateTime"_ASV << FDateTime(2020, 5, 13, 15, 10);
		Writer << "TimeSpan"_ASV << FTimespan(1, 2, 4, 8);

		Writer << "LiteralName" << nullptr;

		Writer.EndObject();
		Object = Writer.Save().AsObjectRef();
	}

	TestEqual(TEXT("FCbWriter(Stream) Validate"), ValidateCompactBinary(*Object.GetBuffer(), ECbValidateMode::All), ECbValidateError::None);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbWriterStateTest, "System.Core.Serialization.CbWriter.State", CompactBinaryWriterTestFlags)
bool FCbWriterStateTest::RunTest(const FString& Parameters)
{
	FCbWriter Writer;

	// Assert on saving an empty writer.
	//uint8 EmptyField[1];
	//Writer.Reset();
	//Writer.SaveToMemory(MakeMemoryView(EmptyField));

	// Assert on under-sized save buffer.
	//uint8 ZeroFieldSmall[1];
	//Writer.Reset();
	//Writer.Integer(0);
	//Writer.SaveToMemory(MakeMemoryView(ZeroFieldSmall));

	// Assert on over-sized save buffer.
	//uint8 ZeroFieldLarge[3];
	//Writer.Reset();
	//Writer.Integer(0);
	//Writer.SaveToMemory(MakeMemoryView(ZeroFieldLarge));

	// Assert on empty name.
	//Writer.Name(""_ASV);

	// Assert on name after name.
	//Writer.Name("Field"_ASV).Name("Field"_ASV);

	// Assert on missing name.
	//Writer.BeginObject();
	//Writer.Null();
	//Writer.EndObject();

	// Assert on name in array.
	//Writer.BeginArray();
	//Writer.Name("Field"_ASV);
	//Writer.EndArray();

	// Assert on save in object.
	//uint8 InvalidObject[1];
	//Writer.Reset();
	//Writer.BeginObject();
	//Writer.SaveToMemory(MakeMemoryView(InvalidObject));
	//Writer.EndObject();

	// Assert on save in array.
	//uint8 InvalidArray[1];
	//Writer.Reset();
	//Writer.BeginArray();
	//Writer.SaveToMemory(MakeMemoryView(InvalidArray));
	//Writer.EndArray();

	// Assert on object end with no begin.
	//Writer.EndObject();

	// Assert on array end with no begin.
	//Writer.EndArray();

	// Assert on object end after name with no value.
	//Writer.BeginObject();
	//Writer.Name("Field"_ASV);
	//Writer.EndObject();

	// Assert on writing a field with no value.
	//Writer.Field(FCbField());

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // WITH_DEV_AUTOMATION_TESTS
