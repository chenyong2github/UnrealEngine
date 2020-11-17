// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryValidation.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr EAutomationTestFlags::Type CompactBinaryValidationTestFlags =
	EAutomationTestFlags::Type(EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbValidateTest, "System.Core.Serialization.ValidateCompactBinary", CompactBinaryValidationTestFlags)
bool FCbValidateTest::RunTest(const FString& Parameters)
{
	auto Validate = [this](std::initializer_list<uint8> Data, ECbFieldType Type = ECbFieldType::HasFieldType) -> ECbValidateError
	{
		return ValidateCompactBinary(MakeMemoryView(Data), ECbValidateMode::All, Type);
	};
	auto ValidateMode = [this](std::initializer_list<uint8> Data, ECbValidateMode Mode, ECbFieldType Type = ECbFieldType::HasFieldType) -> ECbValidateError
	{
		return ValidateCompactBinary(MakeMemoryView(Data), Mode, Type);
	};

	auto AddName = [](ECbFieldType Type) -> uint8 { return uint8(Type | ECbFieldType::HasFieldName); };

	constexpr uint8 NullNoName = uint8(ECbFieldType::Null);
	constexpr uint8 NullWithName = uint8(ECbFieldType::Null | ECbFieldType::HasFieldName);
	constexpr uint8 IntNoName = uint8(ECbFieldType::IntegerPositive);
	constexpr uint8 IntWithName = uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName);

	// Test OutOfBounds
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Empty)"), Validate({}), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Null)"), Validate({NullNoName}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Null, Name)"), Validate({NullWithName, 1, 'N'}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Null, Name)"), Validate({NullWithName}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Null, Name)"), Validate({NullWithName, 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Null, Name)"), Validate({NullWithName, 0x80}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Null, Name)"), Validate({NullWithName, 0x80, 128}), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Object, Empty)"), Validate({uint8(ECbFieldType::Object), 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Object, Empty, NoType)"), Validate({0}, ECbFieldType::Object), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Object, Field)"), Validate({uint8(ECbFieldType::Object), 7, NullWithName, 1, 'N', IntWithName, 1, 'I', 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Object, Field, NoType)"), Validate({7, NullWithName, 1, 'N', IntWithName, 1, 'I', 0}, ECbFieldType::Object), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object)"), Validate({uint8(ECbFieldType::Object)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object, NoType)"), Validate({}, ECbFieldType::Object), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object)"), Validate({uint8(ECbFieldType::Object), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object, NoType)"), Validate({1}, ECbFieldType::Object), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object, OOB Field)"), Validate({uint8(ECbFieldType::Object), 3, AddName(ECbFieldType::Float32), 1, 'N'}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Object, OOB Field, NoType)"), Validate({3, AddName(ECbFieldType::Float32), 1, 'N'}, ECbFieldType::Object), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, UniformObject, Field)"), Validate({uint8(ECbFieldType::UniformObject), 3, NullWithName, 1, 'N'}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, UniformObject, Field, NoType)"), Validate({3, NullWithName, 1, 'N'}, ECbFieldType::UniformObject), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject)"), Validate({uint8(ECbFieldType::UniformObject)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject, NoType)"), Validate({}, ECbFieldType::UniformObject), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject)"), Validate({uint8(ECbFieldType::UniformObject), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject, NoType)"), Validate({1}, ECbFieldType::UniformObject), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject, OOB Field)"), Validate({uint8(ECbFieldType::UniformObject), 3, AddName(ECbFieldType::Float32), 1, 'N'}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformObject, OOB Field, NoType)"), Validate({3, AddName(ECbFieldType::Float32), 1, 'N'}, ECbFieldType::UniformObject), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Array, Empty)"), Validate({uint8(ECbFieldType::Array), 1, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Array, Empty, NoType)"), Validate({1, 0}, ECbFieldType::Array), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Array, Field)"), Validate({uint8(ECbFieldType::Array), 4, 2, NullNoName, IntNoName, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Array, Field, NoType)"), Validate({4, 2, NullNoName, IntNoName, 0}, ECbFieldType::Array), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array)"), Validate({uint8(ECbFieldType::Array)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array, NoType)"), Validate({}, ECbFieldType::Array), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array)"), Validate({uint8(ECbFieldType::Array), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array, NoType)"), Validate({1}, ECbFieldType::Array), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array, OOB Field)"), Validate({uint8(ECbFieldType::Array), 2, 1, uint8(ECbFieldType::Float32)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Array, OOB Field, NoType)"), Validate({2, 1, uint8(ECbFieldType::Float32)}, ECbFieldType::Array), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, UniformArray, Field)"), Validate({uint8(ECbFieldType::UniformArray), 3, 1, IntNoName, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, UniformArray, Field, NoType)"), Validate({3, 1, IntNoName, 0}, ECbFieldType::UniformArray), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray)"), Validate({uint8(ECbFieldType::UniformArray)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray, NoType)"), Validate({}, ECbFieldType::UniformArray), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray)"), Validate({uint8(ECbFieldType::UniformArray), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray, NoType)"), Validate({1}, ECbFieldType::UniformArray), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray, OOB Field)"), Validate({uint8(ECbFieldType::UniformArray), 2, 1, uint8(ECbFieldType::Float32)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, UniformArray, OOB Field, NoType)"), Validate({2, 1, uint8(ECbFieldType::Float32)}, ECbFieldType::UniformArray), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Binary, Empty)"), Validate({uint8(ECbFieldType::Binary), 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Binary, Empty, NoType)"), Validate({0}, ECbFieldType::Binary), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Binary, Field)"), Validate({uint8(ECbFieldType::Binary), 1, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Binary, Field, NoType)"), Validate({1, 0}, ECbFieldType::Binary), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Binary)"), Validate({uint8(ECbFieldType::Binary)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Binary, NoType)"), Validate({}, ECbFieldType::Binary), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Binary)"), Validate({uint8(ECbFieldType::Binary), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Binary, NoType)"), Validate({1}, ECbFieldType::Binary), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, String, Empty)"), Validate({uint8(ECbFieldType::String), 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, String, Empty, NoType)"), Validate({0}, ECbFieldType::String), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, String, Field)"), Validate({uint8(ECbFieldType::String), 1, 'S'}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, String, Field, NoType)"), Validate({1, 'S'}, ECbFieldType::String), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, String)"), Validate({uint8(ECbFieldType::String)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, String, NoType)"), Validate({}, ECbFieldType::String), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, String)"), Validate({uint8(ECbFieldType::String), 1}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, String, NoType)"), Validate({1}, ECbFieldType::String), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerPositive, 1-byte)"), Validate({uint8(ECbFieldType::IntegerPositive), 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerPositive, 1-byte, NoType)"), Validate({0}, ECbFieldType::IntegerPositive), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerPositive, 2-byte)"), Validate({uint8(ECbFieldType::IntegerPositive), 0x80, 0x80}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerPositive, 2-byte, NoType)"), Validate({0x80, 0x80}, ECbFieldType::IntegerPositive), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 1-byte)"), Validate({uint8(ECbFieldType::IntegerPositive)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 1-byte, NoType)"), Validate({}, ECbFieldType::IntegerPositive), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 2-byte)"), Validate({uint8(ECbFieldType::IntegerPositive), 0x80}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 2-byte, NoType)"), Validate({0x80}, ECbFieldType::IntegerPositive), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 9-byte)"), Validate({uint8(ECbFieldType::IntegerPositive), 0xff, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerPositive, 9-byte, NoType)"), Validate({0xff, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::IntegerPositive), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerNegative, 1-byte)"), Validate({uint8(ECbFieldType::IntegerNegative), 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerNegative, 1-byte, NoType)"), Validate({0}, ECbFieldType::IntegerNegative), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerNegative, 2-byte)"), Validate({uint8(ECbFieldType::IntegerNegative), 0x80, 0x80}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, IntegerNegative, 2-byte, NoType)"), Validate({0x80, 0x80}, ECbFieldType::IntegerNegative), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 1-byte)"), Validate({uint8(ECbFieldType::IntegerNegative)}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 1-byte, NoType)"), Validate({}, ECbFieldType::IntegerNegative), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 2-byte)"), Validate({uint8(ECbFieldType::IntegerNegative), 0x80}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 2-byte, NoType)"), Validate({0x80}, ECbFieldType::IntegerNegative), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 9-byte)"), Validate({uint8(ECbFieldType::IntegerNegative), 0xff, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, IntegerNegative, 9-byte, NoType)"), Validate({0xff, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::IntegerNegative), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Float32)"), Validate({uint8(ECbFieldType::Float32), 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Float32, NoType)"), Validate({0, 0, 0, 0}, ECbFieldType::Float32), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Float32)"), Validate({uint8(ECbFieldType::Float32), 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Float32, NoType)"), Validate({0, 0, 0}, ECbFieldType::Float32), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Float64)"), Validate({uint8(ECbFieldType::Float64), 0x3f, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Float64, NoType)"), Validate({0x3f, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00}, ECbFieldType::Float64), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Float64)"), Validate({uint8(ECbFieldType::Float64), 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Float64, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Float64), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, BoolFalse)"), Validate({uint8(ECbFieldType::BoolFalse)}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, BoolTrue)"), Validate({uint8(ECbFieldType::BoolTrue)}), ECbValidateError::None);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Reference)"), Validate({uint8(ECbFieldType::Reference), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Reference, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Reference), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Reference)"), Validate({uint8(ECbFieldType::Reference), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Reference, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Reference), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, BinaryReference)"), Validate({uint8(ECbFieldType::BinaryReference), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, BinaryReference, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::BinaryReference), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, BinaryReference)"), Validate({uint8(ECbFieldType::BinaryReference), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, BinaryReference, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::BinaryReference), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Hash)"), Validate({uint8(ECbFieldType::Hash), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Hash, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Hash), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Hash)"), Validate({uint8(ECbFieldType::Hash), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Hash, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Hash), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, Uuid)"), Validate({uint8(ECbFieldType::Uuid), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, Uuid, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Uuid), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Uuid)"), Validate({uint8(ECbFieldType::Uuid), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, Uuid, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::Uuid), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, DateTime)"), Validate({uint8(ECbFieldType::DateTime), 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, DateTime, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::DateTime), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, DateTime)"), Validate({uint8(ECbFieldType::DateTime), 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, DateTime, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0}, ECbFieldType::DateTime), ECbValidateError::OutOfBounds);

	TestEqual(TEXT("ValidateCompactBinary(Valid, TimeSpan)"), Validate({uint8(ECbFieldType::TimeSpan), 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Valid, TimeSpan, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0, 0}, ECbFieldType::TimeSpan), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, TimeSpan)"), Validate({uint8(ECbFieldType::TimeSpan), 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::OutOfBounds);
	TestEqual(TEXT("ValidateCompactBinary(OutOfBounds, TimeSpan, NoType)"), Validate({0, 0, 0, 0, 0, 0, 0}, ECbFieldType::TimeSpan), ECbValidateError::OutOfBounds);

	// Test InvalidType
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, Unknown)"), Validate({uint8(ECbFieldType::TimeSpan) + 1}), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, Unknown)"), Validate({}, ECbFieldType(uint8(ECbFieldType::TimeSpan) + 1)), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, HasFieldType)"), Validate({uint8(ECbFieldType::Null | ECbFieldType::HasFieldType)}), ECbValidateError::InvalidType);

	TestEqual(TEXT("ValidateCompactBinary(InvalidType, ZeroSizeField)"), Validate({}, ECbFieldType::Null), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, ZeroSizeField, BoolFalse)"), Validate({}, ECbFieldType::BoolFalse), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, ZeroSizeField, BoolTrue)"), Validate({}, ECbFieldType::BoolTrue), ECbValidateError::InvalidType);

	TestEqual(TEXT("ValidateCompactBinary(InvalidType, ZeroSizeField, Array)"), Validate({uint8(ECbFieldType::UniformArray), 2, 2, NullNoName}), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinary(InvalidType, ZeroSizeField, Object)"), Validate({uint8(ECbFieldType::UniformObject), 2, NullNoName, 0}), ECbValidateError::InvalidType);

	// Test DuplicateName
	TestEqual(TEXT("ValidateCompactBinary(DuplicateName)"), Validate({uint8(ECbFieldType::UniformObject), 7, NullWithName, 1, 'A', 1, 'B', 1, 'A'}), ECbValidateError::DuplicateName);
	TestEqual(TEXT("ValidateCompactBinary(DuplicateName, CaseSensitive)"), Validate({uint8(ECbFieldType::UniformObject), 7, NullWithName, 1, 'A', 1, 'B', 1, 'a'}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(DuplicateName, Mode)"), ValidateMode({uint8(ECbFieldType::UniformObject), 7, NullWithName, 1, 'A', 1, 'B', 1, 'A'}, ECbValidateMode::All & ~ECbValidateMode::Names), ECbValidateError::None);

	// Test MissingName
	TestEqual(TEXT("ValidateCompactBinary(MissingName)"), Validate({uint8(ECbFieldType::Object), 3, NullNoName, IntNoName, 0}), ECbValidateError::MissingName);
	TestEqual(TEXT("ValidateCompactBinary(MissingName, Uniform)"), Validate({uint8(ECbFieldType::UniformObject), 3, IntNoName, 0, 0}), ECbValidateError::MissingName);
	TestEqual(TEXT("ValidateCompactBinary(MissingName, Mode)"), ValidateMode({uint8(ECbFieldType::Object), 3, NullNoName, IntNoName, 0}, ECbValidateMode::All & ~ECbValidateMode::Names), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(MissingName, Uniform, Mode)"), ValidateMode({uint8(ECbFieldType::UniformObject), 3, IntNoName, 0, 0}, ECbValidateMode::All & ~ECbValidateMode::Names), ECbValidateError::None);

	// Test ArrayName
	TestEqual(TEXT("ValidateCompactBinary(ArrayName)"), Validate({uint8(ECbFieldType::Array), 5, 2, NullNoName, NullWithName, 1, 'F'}), ECbValidateError::ArrayName);
	TestEqual(TEXT("ValidateCompactBinary(ArrayName, Uniform)"), Validate({uint8(ECbFieldType::UniformArray), 4, 1, NullWithName, 1, 'F'}), ECbValidateError::ArrayName);
	TestEqual(TEXT("ValidateCompactBinary(ArrayName, Mode)"), ValidateMode({uint8(ECbFieldType::Array), 5, 2, NullNoName, NullWithName, 1, 'F'}, ECbValidateMode::All & ~ECbValidateMode::Names), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(ArrayName, Uniform, Mode)"), ValidateMode({uint8(ECbFieldType::UniformArray), 4, 1, NullWithName, 1, 'F'}, ECbValidateMode::All & ~ECbValidateMode::Names), ECbValidateError::None);

	// Test InvalidString
	// Not tested or implemented yet because the engine does not provide enough UTF-8 functionality.

	// Test InvalidInteger
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, NameSize)"), Validate({NullWithName, 0x80, 1, 'N'}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ObjectSize)"), Validate({uint8(ECbFieldType::Object), 0xc0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ArraySize)"), Validate({uint8(ECbFieldType::Array), 0xe0, 0, 0, 1, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ArrayCount)"), Validate({uint8(ECbFieldType::Array), 5, 0xf0, 0, 0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, BinarySize)"), Validate({uint8(ECbFieldType::Binary), 0xf8, 0, 0, 0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, StringSize)"), Validate({uint8(ECbFieldType::String), 0xfc, 0, 0, 0, 0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, IntegerPositive)"), Validate({uint8(ECbFieldType::IntegerPositive), 0xfe, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, IntegerNegative)"), Validate({uint8(ECbFieldType::IntegerNegative), 0xff, 0, 0, 0, 0, 0, 0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ArraySize)"), Validate({uint8(ECbFieldType::Array), 0x80, 1, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ArrayCount)"), Validate({uint8(ECbFieldType::Array), 3, 0xc0, 0, 0}), ECbValidateError::InvalidInteger);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ObjectSize)"), Validate({uint8(ECbFieldType::Object), 0xe0, 0, 0, 0}), ECbValidateError::InvalidInteger);

	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, NameSize, Mode)"), ValidateMode({NullWithName, 0x80, 1, 'N'}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ArraySize, Mode)"), ValidateMode({uint8(ECbFieldType::Array), 0xc0, 0, 1, 0}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(InvalidInteger, ObjectSize, Mode)"), ValidateMode({uint8(ECbFieldType::Object), 0xe0, 0, 0, 0}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);

	// Test InvalidFloat
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxSignificant+1)"), Validate({uint8(ECbFieldType::Float64), 0x3f, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00}), ECbValidateError::None); // 1.9999999403953552
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxExponent+1)"), Validate({uint8(ECbFieldType::Float64), 0x47, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00}), ECbValidateError::None); // 6.8056469327705771e38
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxSignificand)"), Validate({uint8(ECbFieldType::Float64), 0x3f, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00}), ECbValidateError::InvalidFloat); // 1.9999998807907104
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxExponent)"), Validate({uint8(ECbFieldType::Float64), 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00}), ECbValidateError::InvalidFloat); // 3.4028234663852886e38
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxSignificand, Mode)"), ValidateMode({uint8(ECbFieldType::Float64), 0x3f, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None); // 1.9999998807907104
	TestEqual(TEXT("ValidateCompactBinary(InvalidFloat, MaxExponent, Mode)"), ValidateMode({uint8(ECbFieldType::Float64), 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None); // 3.4028234663852886e38

	// Test NonUniformObject
	TestEqual(TEXT("ValidateCompactBinary(NonUniformObject)"), Validate({uint8(ECbFieldType::Object), 3, NullWithName, 1, 'A'}), ECbValidateError::NonUniformObject);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformObject)"), Validate({uint8(ECbFieldType::Object), 6, NullWithName, 1, 'A', NullWithName, 1, 'B'}), ECbValidateError::NonUniformObject);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformObject, Mode)"), ValidateMode({uint8(ECbFieldType::Object), 3, NullWithName, 1, 'A'}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformObject, Mode)"), ValidateMode({uint8(ECbFieldType::Object), 6, NullWithName, 1, 'A', NullWithName, 1, 'B'}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);

	// Test NonUniformArray
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray)"), Validate({uint8(ECbFieldType::Array), 3, 1, IntNoName, 0}), ECbValidateError::NonUniformArray);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray)"), Validate({uint8(ECbFieldType::Array), 5, 2, IntNoName, 1, IntNoName, 2}), ECbValidateError::NonUniformArray);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray, Null)"), Validate({uint8(ECbFieldType::Array), 3, 2, NullNoName, NullNoName}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray, Bool)"), Validate({uint8(ECbFieldType::Array), 3, 2, uint8(ECbFieldType::BoolFalse), uint8(ECbFieldType::BoolFalse)}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray, Bool)"), Validate({uint8(ECbFieldType::Array), 3, 2, uint8(ECbFieldType::BoolTrue), uint8(ECbFieldType::BoolTrue)}), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray, Mode)"), ValidateMode({uint8(ECbFieldType::Array), 3, 1, IntNoName, 0}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(NonUniformArray, Mode)"), ValidateMode({uint8(ECbFieldType::Array), 5, 2, IntNoName, 1, IntNoName, 2}, ECbValidateMode::All & ~ECbValidateMode::Format), ECbValidateError::None);

	// Test Padding
	TestEqual(TEXT("ValidateCompactBinary(Padding)"), Validate({NullNoName, 0}), ECbValidateError::Padding);
	TestEqual(TEXT("ValidateCompactBinary(Padding)"), Validate({uint8(ECbFieldType::Array), 1, 0, 0}), ECbValidateError::Padding);
	TestEqual(TEXT("ValidateCompactBinary(Padding)"), Validate({uint8(ECbFieldType::Object), 0, 0}), ECbValidateError::Padding);
	TestEqual(TEXT("ValidateCompactBinary(Padding, Mode)"), ValidateMode({NullNoName, 0}, ECbValidateMode::All & ~ECbValidateMode::Padding), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Padding, Mode)"), ValidateMode({uint8(ECbFieldType::Array), 1, 0, 0}, ECbValidateMode::All & ~ECbValidateMode::Padding), ECbValidateError::None);
	TestEqual(TEXT("ValidateCompactBinary(Padding, Mode)"), ValidateMode({uint8(ECbFieldType::Object), 0, 0}, ECbValidateMode::All & ~ECbValidateMode::Padding), ECbValidateError::None);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbValidateRangeTest, "System.Core.Serialization.ValidateCompactBinaryRange", CompactBinaryValidationTestFlags)
bool FCbValidateRangeTest::RunTest(const FString& Parameters)
{
	auto Validate = [this](std::initializer_list<uint8> Data) -> ECbValidateError
	{
		return ValidateCompactBinaryRange(MakeMemoryView(Data), ECbValidateMode::All);
	};

	// Test Empty
	TestEqual(TEXT("ValidateCompactBinaryRange(Empty)"), Validate({}), ECbValidateError::None);

	// Test Valid
	TestEqual(TEXT("ValidateCompactBinaryRange(Null x2)"), Validate({uint8(ECbFieldType::Null), uint8(ECbFieldType::Null)}), ECbValidateError::None);

	// Test Padding
	TestEqual(TEXT("ValidateCompactBinaryRange(Padding InvalidType)"), Validate({uint8(ECbFieldType::Null), 0}), ECbValidateError::InvalidType);
	TestEqual(TEXT("ValidateCompactBinaryRange(Padding OutOfBounds)"), Validate({uint8(ECbFieldType::Null), uint8(ECbFieldType::Binary)}), ECbValidateError::OutOfBounds);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // WITH_DEV_AUTOMATION_TESTS
