// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "Misc/AutomationTest.h"
#include "Misc/Blake3.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/VarInt.h"

#if WITH_DEV_AUTOMATION_TESTS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr EAutomationTestFlags::Type CompactBinaryTestFlags =
	EAutomationTestFlags::Type(EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <ECbFieldType FieldType>
struct TCbFieldTypeAccessors;

template <ECbFieldType FieldType>
using TCbFieldValueType = typename TCbFieldTypeAccessors<FCbFieldType::GetType(FieldType)>::ValueType;

template <ECbFieldType FieldType>
constexpr bool (FCbField::*TCbFieldIsTypeFn)() const = TCbFieldTypeAccessors<FCbFieldType::GetType(FieldType)>::IsTypeFn;

template <ECbFieldType FieldType>
constexpr auto TCbFieldAsTypeFn = TCbFieldTypeAccessors<FCbFieldType::GetType(FieldType)>::AsTypeFn;

#define UE_CBFIELD_TYPE_ACCESSOR(FieldType, InIsTypeFn, InAsTypeFn, InValueType)                                      \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbField::*IsTypeFn)() const = &FCbField::InIsTypeFn;                                  \
		static constexpr auto AsTypeFn = &FCbField::InAsTypeFn;                                                       \
	};

#define UE_CBFIELD_TYPE_ACCESSOR_TYPED(FieldType, InIsTypeFn, InAsTypeFn, InValueType, InDefaultType)                 \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbField::*IsTypeFn)() const = &FCbField::InIsTypeFn;                                  \
		static constexpr InValueType (FCbField::*AsTypeFn)(InDefaultType) = &FCbField::InAsTypeFn;                    \
	};

UE_CBFIELD_TYPE_ACCESSOR(Object, IsObject, AsObject, FCbObject);
UE_CBFIELD_TYPE_ACCESSOR(Array, IsArray, AsArray, FCbArray);
UE_CBFIELD_TYPE_ACCESSOR(Binary, IsBinary, AsBinary, FConstMemoryView);
UE_CBFIELD_TYPE_ACCESSOR(String, IsString, AsString, FAnsiStringView);
UE_CBFIELD_TYPE_ACCESSOR(IntegerPositive, IsInteger, AsUInt64, uint64);
UE_CBFIELD_TYPE_ACCESSOR(IntegerNegative, IsInteger, AsInt64, int64);
UE_CBFIELD_TYPE_ACCESSOR(Float32, IsFloat, AsFloat, float);
UE_CBFIELD_TYPE_ACCESSOR(Float64, IsFloat, AsDouble, double);
UE_CBFIELD_TYPE_ACCESSOR(BoolFalse, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR(BoolTrue, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR(Reference, IsReference, AsReference, FBlake3Hash);
UE_CBFIELD_TYPE_ACCESSOR(BinaryReference, IsBinaryReference, AsBinaryReference, FBlake3Hash);
UE_CBFIELD_TYPE_ACCESSOR(Hash, IsHash, AsHash, FBlake3Hash);
UE_CBFIELD_TYPE_ACCESSOR_TYPED(Uuid, IsUuid, AsUuid, FGuid, const FGuid&);
UE_CBFIELD_TYPE_ACCESSOR(DateTime, IsDateTime, AsDateTimeTicks, int64);
UE_CBFIELD_TYPE_ACCESSOR(TimeSpan, IsTimeSpan, AsTimeSpanTicks, int64);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbFieldTestBase : public FAutomationTestBase
{
protected:
	using FAutomationTestBase::FAutomationTestBase;
	using FAutomationTestBase::TestEqual;

	void TestEqualBytes(const TCHAR* What, FConstMemoryView Actual, TArrayView<const uint8> Expected)
	{
		TestTrue(What, Actual.EqualBytes(MakeMemoryView(Expected)));
	}

	template <typename T, typename Default>
	void TestFieldAsTypeNoClone(FCbField& Field, T (FCbField::*AsTypeFn)(Default), T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None)
	{
		TestTypeError(Field, ExpectedError);
		TestEqual(TEXT("FCbField::As[Type]()"), (Field.*AsTypeFn)(DefaultValue), ExpectedValue);
		TestEqual(TEXT("FCbField::As[Type]() -> HasError()"), Field.HasError(), ExpectedError != ECbFieldError::None);
		TestEqual(TEXT("FCbField::As[Type]() -> GetError()"), Field.GetError(), ExpectedError);
	}

	template <typename T, typename Default>
	void TestFieldAsType(FCbField& Field, T (FCbField::*AsTypeFn)(Default), T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None)
	{
		TestFieldAsTypeNoClone(Field, AsTypeFn, ExpectedValue, DefaultValue, ExpectedError);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		TestFieldAsTypeNoClone(FieldClone, AsTypeFn, ExpectedValue, DefaultValue, ExpectedError);
		TestTrue(TEXT("FCbField::Equals()"), Field.Equals(FieldClone));
	}

	template <typename T>
	void TestFieldAsTypeNoClone(FCbField& Field, T (FCbField::*AsTypeFn)(), T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None)
	{
		TestTypeError(Field, ExpectedError);
		(Field.*AsTypeFn)();
		TestEqual(TEXT("FCbField::As[Type]() -> HasError()"), Field.HasError(), ExpectedError != ECbFieldError::None);
		TestEqual(TEXT("FCbField::As[Type]() -> GetError()"), Field.GetError(), ExpectedError);
	}

	template <typename T>
	void TestFieldAsType(FCbField& Field, T (FCbField::*AsTypeFn)(), T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None)
	{
		TestFieldAsTypeNoClone(Field, AsTypeFn, ExpectedValue, DefaultValue, ExpectedError);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		TestFieldAsTypeNoClone(FieldClone, AsTypeFn, ExpectedValue, DefaultValue, ExpectedError);
		TestTrue(TEXT("FCbField::Equals()"), Field.Equals(FieldClone));
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>>
	void TestField(FCbField& Field, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None)
	{
		TestTrue(TEXT("FCbField::Is[Type]()"), (Field.*TCbFieldIsTypeFn<FieldType>)());
		TestFieldAsType(Field, TCbFieldAsTypeFn<FieldType>, ExpectedValue, DefaultValue, ExpectedError);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>>
	void TestField(TArrayView<const uint8> Payload, T ExpectedValue = T(), T DefaultValue = T())
	{
		FCbField Field(Payload.GetData(), FieldType);
		TestEqual(TEXT("FCbField::GetSize()"), Field.GetSize(), uint64(Payload.Num()));
		TestTrue(TEXT("FCbField::HasValue()"), Field.HasValue());
		TestFalse(TEXT("FCbField::HasError() == false"), Field.HasError());
		TestEqual(TEXT("FCbField::GetError() == None"), Field.GetError(), ECbFieldError::None);
		TestField<FieldType>(Field, ExpectedValue, DefaultValue);
	}

	template <typename T, typename Default>
	void TestFieldAsTypeError(FCbField& Field, T (FCbField::*AsTypeFn)(Default), ECbFieldError ExpectedError, T ExpectedValue = T())
	{
		TestFieldAsTypeNoClone(Field, AsTypeFn, ExpectedValue, ExpectedValue, ExpectedError);
	}

	template <typename T>
	void TestFieldAsTypeError(FCbField& Field, T (FCbField::*AsTypeFn)(), ECbFieldError ExpectedError, T ExpectedValue = T())
	{
		TestFieldAsTypeNoClone(Field, AsTypeFn, ExpectedValue, ExpectedValue, ExpectedError);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>>
	void TestFieldError(FCbField& Field, ECbFieldError ExpectedError, T ExpectedValue = T())
	{
		TestEqual(TEXT("FCbField::Is[Type]()"), (Field.*TCbFieldIsTypeFn<FieldType>)(), ExpectedError != ECbFieldError::TypeError);
		TestFieldAsTypeError(Field, TCbFieldAsTypeFn<FieldType>, ExpectedError, ExpectedValue);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>>
	void TestFieldError(TArrayView<const uint8> Payload, ECbFieldError ExpectedError, T ExpectedValue = T())
	{
		FCbField Field(Payload.GetData(), FieldType);
		TestFieldError<FieldType>(Field, ExpectedError, ExpectedValue);
	}

private:
	void TestTypeError(FCbField& Field, ECbFieldError ExpectedError)
	{
		if (ExpectedError == ECbFieldError::None && !Field.IsBool())
		{
			TestFalse(TEXT("FCbField::IsBool() == false"), Field.IsBool());
			TestFalse(TEXT("FCbField::AsBool() == false"), Field.AsBool());
			TestTrue(TEXT("FCbField::AsBool() -> HasError()"), Field.HasError());
			TestEqual(TEXT("FCbField::AsBool() -> GetError() == TypeError"), Field.GetError(), ECbFieldError::TypeError);
		}
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldNoneTest, FCbFieldTestBase, "System.Core.Serialization.CbField.None", CompactBinaryTestFlags)
bool FCbFieldNoneTest::RunTest(const FString& Parameters)
{
	// Test FCbField()
	{
		constexpr FCbField DefaultField;
		static_assert(!DefaultField.HasName(), "Error in HasName()");
		static_assert(!DefaultField.HasValue(), "Error in HasValue()");
		static_assert(!DefaultField.HasError(), "Error in HasError()");
		static_assert(DefaultField.GetError() == ECbFieldError::None, "Error in GetError()");
		TestEqual(TEXT("FCbField()::GetSize() == 0"), DefaultField.GetSize(), uint64(0));
		TestEqual(TEXT("FCbField()::GetName().Len() == 0"), DefaultField.GetName().Len(), 0);
		TestFalse(TEXT("!FCbField()::HasName()"), DefaultField.HasName());
		TestFalse(TEXT("!FCbField()::HasValue()"), DefaultField.HasValue());
		TestFalse(TEXT("!FCbField()::HasError()"), DefaultField.HasError());
		TestEqual(TEXT("FCbField()::GetError() == None"), DefaultField.GetError(), ECbFieldError::None);
	}

	// Test FCbField(None)
	{
		FCbField NoneField(nullptr, ECbFieldType::None);
		TestEqual(TEXT("FCbField(None)::GetSize() == 0"), NoneField.GetSize(), uint64(0));
		TestEqual(TEXT("FCbField(None)::GetName().Len() == 0"), NoneField.GetName().Len(), 0);
		TestFalse(TEXT("!FCbField(None)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None)::HasValue()"), NoneField.HasValue());
		TestFalse(TEXT("!FCbField(None)::HasError()"), NoneField.HasError());
		TestEqual(TEXT("FCbField(None)::GetError() == None"), NoneField.GetError(), ECbFieldError::None);
	}

	// Test FCbField(None|Type|Name)
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType), 4, 'N', 'a', 'm', 'e' };
		FCbField NoneField(NoneBytes);
		TestEqual(TEXT("FCbField(None|Type|Name)::GetSize()"), NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Type|Name)::GetName()"), NoneField.GetName(), "Name"_ASV);
		TestTrue(TEXT("FCbField(None|Type|Name)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None|Type|Name)::HasValue()"), NoneField.HasValue());
	}

	// Test FCbField(None|Type)
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType) };
		FCbField NoneField(NoneBytes);
		TestEqual(TEXT("FCbField(None|Type)::GetSize()"), NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Type)::GetName()"), NoneField.GetName().Len(), 0);
		TestFalse(TEXT("FCbField(None|Type)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None|Type)::HasValue()"), NoneField.HasValue());
	}

	// Test FCbField(None|Name)
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const ANSICHAR NoneBytes[] = { 4, 'N', 'a', 'm', 'e' };
		FCbField NoneField(NoneBytes, FieldType);
		TestEqual(TEXT("FCbField(None|Name)::GetSize()"), NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Name)::GetName()"), NoneField.GetName(), "Name"_ASV);
		TestTrue(TEXT("FCbField(None|Name)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None|Name)::HasValue()"), NoneField.HasValue());
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldNullTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Null", CompactBinaryTestFlags)
bool FCbFieldNullTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Null)
	{
		FCbField NullField(nullptr, ECbFieldType::Null);
		TestEqual(TEXT("FCbField(Null)::GetSize() == 0"), NullField.GetSize(), uint64(0));
		TestTrue(TEXT("FCbField(Null)::IsNull()"), NullField.IsNull());
		TestTrue(TEXT("FCbField(Null)::HasValue()"), NullField.HasValue());
		TestFalse(TEXT("!FCbField(Null)::HasError()"), NullField.HasError());
		TestEqual(TEXT("FCbField(Null)::GetError() == None"), NullField.GetError(), ECbFieldError::None);
	}

	// Test FCbField(None) as Null
	{
		FCbField Field;
		TestFalse(TEXT("FCbField(None)::IsNull()"), Field.IsNull());
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldObjectTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Object", CompactBinaryTestFlags)
bool FCbFieldObjectTest::RunTest(const FString& Parameters)
{
	auto TestIntObject = [this](const FCbObject& Object, int32 ExpectedNum, uint64 ExpectedPayloadSize)
	{
		TestEqual(TEXT("FCbField(Object)::AsObject().GetSize()"), Object.GetSize(), ExpectedPayloadSize + sizeof(ECbFieldType));

		int32 ActualNum = 0;
		for (FCbFieldIterator It = Object.CreateIterator(); It; ++It)
		{
			++ActualNum;
			TestNotEqual(TEXT("FCbField(Object) Iterator Name"), It->GetName().Len(), 0);
			TestEqual(TEXT("FCbField(Object) Iterator"), It->AsInt32(), ActualNum);
		}
		TestEqual(TEXT("FCbField(Object)::AsObject().CreateIterator() -> Count"), ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbField Field : Object)
		{
			++ActualNum;
			TestNotEqual(TEXT("FCbField(Object) Iterator Name"), Field.GetName().Len(), 0);
			TestEqual(TEXT("FCbField(Object) Range"), Field.AsInt32(), ActualNum);
		}
		TestEqual(TEXT("FCbField(Object)::AsObject() Range -> Count"), ActualNum, ExpectedNum);
	};

	// Test FCbField(Object, Empty)
	TestField<ECbFieldType::Object>({0});

	// Test FCbField(Object, Empty)
	{
		FCbObject Object;
		TestIntObject(Object, 0, 1);

		// Find fields that do not exist.
		TestFalse(TEXT("FCbObject()::Find(Missing)"), Object.Find("Field"_ASV).HasValue());
		TestFalse(TEXT("FCbObject()::FindIgnoreCase(Missing)"), Object.FindIgnoreCase("Field"_ASV).HasValue());
		TestFalse(TEXT("FCbObject()::operator[](Missing)"), Object["Field"_ASV].HasValue());

		// Advance an iterator past the last field.
		FCbFieldIterator It = Object.CreateIterator();
		TestFalse(TEXT("FCbObject()::CreateIterator() At End"), bool(It));
		TestTrue(TEXT("FCbObject()::CreateIterator() At End"), !It);
		for (int Count = 16; Count > 0; --Count)
		{
			++It;
			It->AsInt32();
		}
		TestFalse(TEXT("FCbObject()::CreateIterator() At End"), bool(It));
		TestTrue(TEXT("FCbObject()::CreateIterator() At End"), !It);
	}

	// Test FCbField(Object, NotEmpty)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 12, IntType, 1, 'A', 1, IntType, 1, 'B', 2, IntType, 1, 'C', 3 };
		FCbField Field(Payload, ECbFieldType::Object);
		TestFieldAsType(Field, &FCbField::AsObject);
		FCbObjectRef Object = FCbObjectRef::Clone(Field.AsObject());
		TestIntObject(Object, 3, sizeof(Payload));
		TestIntObject(Field.AsObject(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbObject::Equals()"), Object.Equals(Field.AsObject()));
		TestEqual(TEXT("FCbObject::Find()"), Object.Find("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject::Find()"), Object.Find("b"_ASV).AsInt32(4), 4);
		TestEqual(TEXT("FCbObject::FindIgnoreCase()"), Object.FindIgnoreCase("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject::FindIgnoreCase()"), Object.FindIgnoreCase("b"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject::operator[]"), Object["B"_ASV].AsInt32(), 2);
		TestEqual(TEXT("FCbObject::operator[]"), Object["b"_ASV].AsInt32(4), 4);
	}

	// Test FCbField(UniformObject, NotEmpty)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbField Field(Payload, ECbFieldType::UniformObject);
		TestFieldAsType(Field, &FCbField::AsObject);
		FCbObjectRef Object = FCbObjectRef::Clone(Field.AsObject());
		TestIntObject(Object, 3, sizeof(Payload));
		TestIntObject(Field.AsObject(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbObject{Uniform}::Equals()"), Object.Equals(Field.AsObject()));
		TestEqual(TEXT("FCbObject{Uniform}::Find()"), Object.Find("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::Find()"), Object.FindRef("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::Find()"), Object.Find("b"_ASV).AsInt32(4), 4);
		TestEqual(TEXT("FCbObject{Uniform}::Find()"), Object.FindRef("b"_ASV).AsInt32(4), 4);
		TestEqual(TEXT("FCbObject{Uniform}::FindIgnoreCase()"), Object.FindIgnoreCase("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::FindIgnoreCase()"), Object.FindRefIgnoreCase("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::FindIgnoreCase()"), Object.FindIgnoreCase("b"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::FindIgnoreCase()"), Object.FindRefIgnoreCase("b"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::operator[]"), Object["B"_ASV].AsInt32(), 2);
		TestEqual(TEXT("FCbObject{Uniform}::operator[]"), Object["b"_ASV].AsInt32(4), 4);

		TestTrue(TEXT("FCbObjectRef::AsFieldRef()"), Object.GetBuffer() == Object.AsFieldRef().AsObjectRef().GetBuffer());

		Object = FCbFieldRef::MakeView(Field).AsObjectRef();

		const uint8 NamedPayload[] = { 1, 'O', 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbField NamedField(NamedPayload, ECbFieldType::UniformObject | ECbFieldType::HasFieldName);
		TestTrue(TEXT("FCbObject::Equals()"), Object.Equals(NamedField.AsObject()));
		TestTrue(TEXT("FCbObject::AsField().Equals()"), Field.Equals(NamedField.AsObject().AsField()));
		TestFalse(TEXT("FCbObject::AsField().Equals()"), NamedField.Equals(NamedField.AsObject().AsField()));
	}

	// Test FCbField(None) as Object
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Object>(Field, ECbFieldError::TypeError);
		FCbFieldRef::MakeView(Field).AsObjectRef();
	}

	// Test FCbObject(ObjectWithName) and CreateRefIterator
	{
		const uint8 ObjectType = uint8(ECbFieldType::Object | ECbFieldType::HasFieldName);
		const uint8 Buffer[] = { ObjectType, 3, 'K', 'e', 'y', 4, uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive), 1, 'F', 8 };
		const FCbObject Object(Buffer);
		TestEqual(TEXT("FCbObject(ObjectWithName)::GetSize()"), Object.GetSize(), uint64(6));
		const FCbObjectRef ObjectClone = FCbObjectRef::Clone(Object);
		TestEqual(TEXT("FCbObjectRef(ObjectWithName)::GetSize()"), ObjectClone.GetSize(), uint64(6));
		TestTrue(TEXT("FCbObject::Equals()"), Object.Equals(ObjectClone));
		for (FCbFieldRefIterator It = ObjectClone.CreateRefIterator(); It; ++It)
		{
			FCbFieldRef Field = *It;
			TestEqual(TEXT("FCbObjectRef::CreateRefIterator().GetName()"), Field.GetName(), "F"_ASV);
			TestEqual(TEXT("FCbObjectRef::CreateRefIterator().AsInt32()"), Field.AsInt32(), 8);
			TestTrue(TEXT("FCbObjectRef::CreateRefIterator().IsOwned()"), Field.IsOwned());
		}
		for (FCbFieldRefIterator It = ObjectClone.CreateRefIterator(), End; It != End; ++It)
		{
		}
	}

	// Test FCbObject as FCbFieldIterator
	{
		uint32 Count = 0;
		FCbObject Object;
		for (FCbField Field : FCbFieldIterator(Object.AsField()))
		{
			TestTrue(TEXT("FCbObject::AsField() as Iterator"), Field.IsObject());
			++Count;
		}
		TestEqual(TEXT("FCbObject::AsField() as Iterator Count"), Count, 1u);
	}

	// Test FCbObjectRef as FCbFieldRefIterator
	{
		uint32 Count = 0;
		FCbObjectRef Object;
		Object.MakeOwned();
		for (FCbFieldRef Field : FCbFieldRefIterator(Object.AsFieldRef()))
		{
			TestTrue(TEXT("FCbObjectRef::AsField() as Iterator"), Field.IsObject());
			++Count;
		}
		TestEqual(TEXT("FCbObjectRef::AsField() as Iterator Count"), Count, 1u);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldArrayTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Array", CompactBinaryTestFlags)
bool FCbFieldArrayTest::RunTest(const FString& Parameters)
{
	auto TestIntArray = [this](FCbArray Array, int32 ExpectedNum, uint64 ExpectedPayloadSize)
	{
		TestEqual(TEXT("FCbField(Array)::AsArray().GetSize()"), Array.GetSize(), ExpectedPayloadSize + sizeof(ECbFieldType));
		TestEqual(TEXT("FCbField(Array)::AsArray().Num()"), Array.Num(), uint64(ExpectedNum));

		int32 ActualNum = 0;
		for (FCbFieldIterator It = Array.CreateIterator(); It; ++It)
		{
			++ActualNum;
			TestEqual(TEXT("FCbField(Array) Iterator"), It->AsInt32(), ActualNum);
		}
		TestEqual(TEXT("FCbField(Array)::AsArray().CreateIterator() -> Count"), ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbField Field : Array)
		{
			++ActualNum;
			TestEqual(TEXT("FCbField(Array) Range"), Field.AsInt32(), ActualNum);
		}
		TestEqual(TEXT("FCbField(Array)::AsArray() Range -> Count"), ActualNum, ExpectedNum);
	};

	// Test FCbField(Array, Empty)
	TestField<ECbFieldType::Array>({1, 0});

	// Test FCbField(Array, Empty)
	{
		FCbArray Array;
		TestIntArray(Array, 0, 2);

		// Advance an iterator past the last field.
		FCbFieldIterator It = Array.CreateIterator();
		TestFalse(TEXT("FCbArray()::CreateIterator() At End"), bool(It));
		TestTrue(TEXT("FCbArray()::CreateIterator() At End"), !It);
		for (int Count = 16; Count > 0; --Count)
		{
			++It;
			It->AsInt32();
		}
		TestFalse(TEXT("FCbArray()::CreateIterator() At End"), bool(It));
		TestTrue(TEXT("FCbArray()::CreateIterator() At End"), !It);
	}

	// Test FCbField(Array, NotEmpty)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 7, 3, IntType, 1, IntType, 2, IntType, 3 };
		FCbField Field(Payload, ECbFieldType::Array);
		TestFieldAsType(Field, &FCbField::AsArray);
		FCbArrayRef Array = FCbArrayRef::Clone(Field.AsArray());
		TestIntArray(Array, 3, sizeof(Payload));
		TestIntArray(Field.AsArray(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(Field.AsArray()));
	}

	// Test FCbField(UniformArray)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 5, 3, IntType, 1, 2, 3 };
		FCbField Field(Payload, ECbFieldType::UniformArray);
		TestFieldAsType(Field, &FCbField::AsArray);
		FCbArrayRef Array = FCbArrayRef::Clone(Field.AsArray());
		TestIntArray(Array, 3, sizeof(Payload));
		TestIntArray(Field.AsArray(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(Field.AsArray()));

		TestTrue(TEXT("FCbArrayRef::AsFieldRef()"), Array.GetBuffer() == Array.AsFieldRef().AsArrayRef().GetBuffer());

		Array = FCbFieldRef::MakeView(Field).AsArrayRef();

		const uint8 NamedPayload[] = { 1, 'A', 5, 3, IntType, 1, 2, 3 };
		FCbField NamedField(NamedPayload, ECbFieldType::UniformArray | ECbFieldType::HasFieldName);
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(NamedField.AsArray()));
		TestTrue(TEXT("FCbArray::AsField().Equals()"), Field.Equals(NamedField.AsArray().AsField()));
		TestFalse(TEXT("FCbArray::AsField().Equals()"), NamedField.Equals(NamedField.AsArray().AsField()));
	}

	// Test FCbField(None) as Array
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Array>(Field, ECbFieldError::TypeError);
		FCbFieldRef::MakeView(Field).AsArrayRef();
	}

	// Test FCbArray(ArrayWithName) and CreateRefIterator
	{
		const uint8 ArrayType = uint8(ECbFieldType::Array | ECbFieldType::HasFieldName);
		const uint8 Buffer[] = { ArrayType, 3, 'K', 'e', 'y', 3, 1, uint8(ECbFieldType::IntegerPositive), 8 };
		const FCbArray Array(Buffer);
		TestEqual(TEXT("Array(ArrayWithName)::GetSize()"), Array.GetSize(), uint64(5));
		const FCbArrayRef ArrayClone = FCbArrayRef::Clone(Array);
		TestEqual(TEXT("FCbArrayRef(ArrayWithName)::GetSize()"), ArrayClone.GetSize(), uint64(5));
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(ArrayClone));
		for (FCbFieldRefIterator It = ArrayClone.CreateRefIterator(); It; ++It)
		{
			FCbFieldRef Field = *It;
			TestEqual(TEXT("FCbArrayRef::CreateRefIterator().AsInt32()"), Field.AsInt32(), 8);
			TestTrue(TEXT("FCbArrayRef::CreateRefIterator().IsOwned()"), Field.IsOwned());
		}
		for (FCbFieldRefIterator It = ArrayClone.CreateRefIterator(), End; It != End; ++It)
		{
		}
	}

	// Test FCbArray as FCbFieldIterator
	{
		uint32 Count = 0;
		FCbArray Array;
		for (FCbField Field : FCbFieldIterator(Array.AsField()))
		{
			TestTrue(TEXT("FCbArray::AsField() as Iterator"), Field.IsArray());
			++Count;
		}
		TestEqual(TEXT("FCbArray::AsField() as Iterator Count"), Count, 1u);
	}

	// Test FCbArrayRef as FCbFieldRefIterator
	{
		uint32 Count = 0;
		FCbArrayRef Array;
		Array.MakeOwned();
		for (FCbFieldRef Field : FCbFieldRefIterator(Array.AsFieldRef()))
		{
			TestTrue(TEXT("FCbArrayRef::AsField() as Iterator"), Field.IsArray());
			++Count;
		}
		TestEqual(TEXT("FCbArrayRef::AsField() as Iterator Count"), Count, 1u);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldBinaryTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Binary", CompactBinaryTestFlags)
bool FCbFieldBinaryTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Binary, Empty)
	TestField<ECbFieldType::Binary>({0});

	// Test FCbField(Binary, Value)
	{
		const uint8 Payload[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FCbField Field(Payload, ECbFieldType::Binary);
		TestFieldAsTypeNoClone(Field, &FCbField::AsBinary, MakeMemoryView(Payload + 1, 3));
	}

	// Test FCbField(None) as Binary
	{
		FCbField Field;
		const uint8 Default[] = { 1, 2, 3 };
		TestFieldError<ECbFieldType::Binary>(Field, ECbFieldError::TypeError, MakeMemoryView(Default));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldStringTest, FCbFieldTestBase, "System.Core.Serialization.CbField.String", CompactBinaryTestFlags)
bool FCbFieldStringTest::RunTest(const FString& Parameters)
{
	// Test FCbField(String, Empty)
	TestField<ECbFieldType::String>({0});

	// Test FCbField(String, Value)
	{
		const uint8 Payload[] = { 3, 'A', 'B', 'C' }; // Size: 3, Data: ABC
		TestField<ECbFieldType::String>(Payload, FAnsiStringView(reinterpret_cast<const ANSICHAR*>(Payload) + 1, 3));
	}

	// Test FCbField(String, OutOfRangeSize)
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 31, Payload);
		TestFieldError<ECbFieldType::String>(Payload, ECbFieldError::RangeError, "ABC"_ASV);
	}

	// Test FCbField(None) as String
	{
		FCbField Field;
		TestFieldError<ECbFieldType::String>(Field, ECbFieldError::TypeError, "ABC"_ASV);
	}

	return true;
}

class FCbFieldIntegerTestBase : public FCbFieldTestBase
{
protected:
	using FCbFieldTestBase::FCbFieldTestBase;

	enum class EIntType : uint8
	{
		None   = 0x00,
		Int8   = 0x01,
		Int16  = 0x02,
		Int32  = 0x04,
		Int64  = 0x08,
		UInt8  = 0x10,
		UInt16 = 0x20,
		UInt32 = 0x40,
		UInt64 = 0x80,
		// Masks for positive values requiring the specified number of bits.
		Pos64 = UInt64,
		Pos63 = Pos64 |  Int64,
		Pos32 = Pos63 | UInt32,
		Pos31 = Pos32 |  Int32,
		Pos16 = Pos31 | UInt16,
		Pos15 = Pos16 |  Int16,
		Pos8  = Pos15 | UInt8,
		Pos7  = Pos8  |  Int8,
		// Masks for negative values requiring the specified number of bits.
		Neg63 = Int64,
		Neg31 = Neg63 | Int32,
		Neg15 = Neg31 | Int16,
		Neg7  = Neg15 | Int8,
	};

	void TestIntegerField(ECbFieldType FieldType, EIntType ExpectedMask, uint64 Magnitude)
	{
		uint8 Payload[9];
		const bool Negative = bool(uint8(FieldType) & 1);
		WriteVarUInt(Magnitude - Negative, Payload);
		constexpr uint64 DefaultValue = 8;
		const uint64 ExpectedValue = Negative ? uint64(-int64(Magnitude)) : Magnitude;
		FCbField Field(Payload, FieldType);
		TestFieldAsType(Field, &FCbField::AsInt8, int8(EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ExpectedValue : DefaultValue),
			int8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsInt16, int16(EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ExpectedValue : DefaultValue),
			int16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsInt32, int32(EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ExpectedValue : DefaultValue),
			int32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsInt64, int64(EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ExpectedValue : DefaultValue),
			int64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsUInt8, uint8(EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ExpectedValue : DefaultValue),
			uint8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsUInt16, uint16(EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ExpectedValue : DefaultValue),
			uint16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsUInt32, uint32(EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ExpectedValue : DefaultValue),
			uint32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ECbFieldError::None : ECbFieldError::RangeError);
		TestFieldAsType(Field, &FCbField::AsUInt64, uint64(EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ExpectedValue : DefaultValue),
			uint64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ECbFieldError::None : ECbFieldError::RangeError);
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldIntegerTest, FCbFieldIntegerTestBase, "System.Core.Serialization.CbField.Integer", CompactBinaryTestFlags)
bool FCbFieldIntegerTest::RunTest(const FString& Parameters)
{
	// Test FCbField(IntegerPositive)
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos7,  0x00);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos7,  0x7f);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos8,  0x80);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos8,  0xff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos15, 0x0100);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos15, 0x7fff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos16, 0x8000);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos16, 0xffff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos31, 0x0001'0000);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos31, 0x7fff'ffff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos32, 0x8000'0000);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos32, 0xffff'ffff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos63, 0x0000'0001'0000'0000);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos63, 0x7fff'ffff'ffff'ffff);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos64, 0x8000'0000'0000'0000);
	TestIntegerField(ECbFieldType::IntegerPositive, EIntType::Pos64, 0xffff'ffff'ffff'ffff);

	// Test FCbField(IntegerNegative)
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg7,  0x01);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg7,  0x80);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg15, 0x81);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg15, 0x8000);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg31, 0x8001);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg31, 0x8000'0000);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg63, 0x8000'0001);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::Neg63, 0x8000'0000'0000'0000);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::None,  0x8000'0000'0000'0001);
	TestIntegerField(ECbFieldType::IntegerNegative, EIntType::None,  0xffff'ffff'ffff'ffff);

	// Test FCbField(None) as Integer
	{
		FCbField Field;
		TestFieldError<ECbFieldType::IntegerPositive>(Field, ECbFieldError::TypeError, uint64(8));
		TestFieldError<ECbFieldType::IntegerNegative>(Field, ECbFieldError::TypeError, int64(8));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldFloatTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Float", CompactBinaryTestFlags)
bool FCbFieldFloatTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Float, 32-bit)
	{
		const uint8 Payload[] = { 0xc0, 0x12, 0x34, 0x56 }; // -2.28444433f
		TestField<ECbFieldType::Float32>(Payload, -2.28444433f);

		FCbField Field(Payload, ECbFieldType::Float32);
		TestFieldAsType(Field, &FCbField::AsDouble, -2.28444433);
	}

	// Test FCbField(Float, 64-bit)
	{
		const uint8 Payload[] = { 0xc1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }; // -631475.76888888876
		TestField<ECbFieldType::Float64>(Payload, -631475.76888888876);

		FCbField Field(Payload, ECbFieldType::Float64);
		TestFieldAsTypeError(Field, &FCbField::AsFloat, ECbFieldError::RangeError, 8.0f);
	}

	// Test FCbField(Integer+, MaxBinary32) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 1, Payload); // 16,777,215
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestField<ECbFieldType::Float32>(Field, 16'777'215.0f);
		TestField<ECbFieldType::Float64>(Field, 16'777'215.0);
	}

	// Test FCbField(Integer+, MaxBinary32+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 24, Payload); // 16,777,216
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(Field, 16'777'216.0);
	}

	// Test FCbField(Integer+, MaxBinary64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 1, Payload); // 9,007,199,254,740,991
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(Field, 9'007'199'254'740'991.0);
	}

	// Test FCbField(Integer+, MaxBinary64+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 53, Payload); // 9,007,199,254,740,992
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(Integer+, MaxUInt64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(-1), Payload); // Max uint64
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(Integer-, MaxBinary32) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 2, Payload); // -16,777,215
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestField<ECbFieldType::Float32>(Field, -16'777'215.0f);
		TestField<ECbFieldType::Float64>(Field, -16'777'215.0);
	}

	// Test FCbField(Integer-, MaxBinary32+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 1, Payload); // -16,777,216
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(Field, -16'777'216.0);
	}

	// Test FCbField(Integer-, MaxBinary64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 2, Payload); // -9,007,199,254,740,991
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(Field, -9'007'199'254'740'991.0);
	}

	// Test FCbField(Integer-, MaxBinary64+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 1, Payload); // -9,007,199,254,740,992
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(None) as Float
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Float32>(Field, ECbFieldError::TypeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(Field, ECbFieldError::TypeError, 8.0);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldBoolTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Bool", CompactBinaryTestFlags)
bool FCbFieldBoolTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Bool, False)
	TestField<ECbFieldType::BoolFalse>({}, false, true);

	// Test FCbField(Bool, True)
	TestField<ECbFieldType::BoolTrue>({}, true, false);

	// Test FCbField(None) as Bool
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::BoolFalse>(DefaultField, ECbFieldError::TypeError, false);
		TestFieldError<ECbFieldType::BoolTrue>(DefaultField, ECbFieldError::TypeError, true);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldReferenceTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Reference", CompactBinaryTestFlags)
bool FCbFieldReferenceTest::RunTest(const FString& Parameters)
{
	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	// Test FCbField(Reference, Zero)
	TestField<ECbFieldType::Reference>(ZeroBytes);

	// Test FCbField(Reference, NonZero)
	TestField<ECbFieldType::Reference>(SequentialBytes, FBlake3Hash(SequentialBytes));

	// Test FCbField(None) as Reference
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::Reference>(DefaultField, ECbFieldError::TypeError, FBlake3Hash(SequentialBytes));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldBinaryReferenceTest, FCbFieldTestBase, "System.Core.Serialization.CbField.BinaryReference", CompactBinaryTestFlags)
bool FCbFieldBinaryReferenceTest::RunTest(const FString& Parameters)
{
	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	// Test FCbField(BinaryReference, Zero)
	TestField<ECbFieldType::BinaryReference>(ZeroBytes);

	// Test FCbField(BinaryReference, NonZero)
	TestField<ECbFieldType::BinaryReference>(SequentialBytes, FBlake3Hash(SequentialBytes));

	// Test FCbField(None) as BinaryReference
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::BinaryReference>(DefaultField, ECbFieldError::TypeError, FBlake3Hash(SequentialBytes));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldHashTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Hash", CompactBinaryTestFlags)
bool FCbFieldHashTest::RunTest(const FString& Parameters)
{
	const FBlake3Hash::ByteArray ZeroBytes{};
	const FBlake3Hash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

	// Test FCbField(Hash, Zero)
	TestField<ECbFieldType::Hash>(ZeroBytes);

	// Test FCbField(Hash, NonZero)
	TestField<ECbFieldType::Hash>(SequentialBytes, FBlake3Hash(SequentialBytes));

	// Test FCbField(None) as Hash
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::Hash>(DefaultField, ECbFieldError::TypeError, FBlake3Hash(SequentialBytes));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldUuidTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Uuid", CompactBinaryTestFlags)
bool FCbFieldUuidTest::RunTest(const FString& Parameters)
{
	const uint8 ZeroBytes[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const uint8 SequentialBytes[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	const FGuid SequentialGuid(TEXT("00010203-0405-0607-0809-0a0b0c0d0e0f"));

	// Test FCbField(Uuid, Zero)
	TestField<ECbFieldType::Uuid>(ZeroBytes, FGuid(), SequentialGuid);

	// Test FCbField(Uuid, NonZero)
	TestField<ECbFieldType::Uuid>(SequentialBytes, SequentialGuid, FGuid());

	// Test FCbField(None) as Uuid
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::Uuid>(DefaultField, ECbFieldError::TypeError, FGuid::NewGuid());
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldDateTimeTest, FCbFieldTestBase, "System.Core.Serialization.CbField.DateTime", CompactBinaryTestFlags)
bool FCbFieldDateTimeTest::RunTest(const FString& Parameters)
{
	// Test FCbField(DateTime, Zero)
	TestField<ECbFieldType::DateTime>({0, 0, 0, 0, 0, 0, 0, 0});

	// Test FCbField(DateTime, 0x1020'3040'5060'7080)
	TestField<ECbFieldType::DateTime>({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, int64(0x1020'3040'5060'7080));

	// Test FCbField(DateTime, Zero) as FDateTime
	{
		const uint8 Payload[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbField Field(Payload, ECbFieldType::DateTime);
		TestEqual(TEXT("FCbField()::AsDateTime()"), Field.AsDateTime(), FDateTime(0));
	}

	// Test FCbField(None) as DateTime
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::DateTime>(DefaultField, ECbFieldError::TypeError);
		const FDateTime DefaultValue(0x1020'3040'5060'7080);
		TestEqual(TEXT("FCbField()::AsDateTime()"), DefaultField.AsDateTime(DefaultValue), DefaultValue);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldTimeSpanTest, FCbFieldTestBase, "System.Core.Serialization.CbField.TimeSpan", CompactBinaryTestFlags)
bool FCbFieldTimeSpanTest::RunTest(const FString& Parameters)
{
	// Test FCbField(TimeSpan, Zero)
	TestField<ECbFieldType::TimeSpan>({0, 0, 0, 0, 0, 0, 0, 0});

	// Test FCbField(TimeSpan, 0x1020'3040'5060'7080)
	TestField<ECbFieldType::TimeSpan>({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, int64(0x1020'3040'5060'7080));

	// Test FCbField(TimeSpan, Zero) as FTimeSpan
	{
		const uint8 Payload[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbField Field(Payload, ECbFieldType::TimeSpan);
		TestEqual(TEXT("FCbField()::AsTimeSpan()"), Field.AsTimeSpan(), FTimespan(0));
	}

	// Test FCbField(None) as TimeSpan
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::TimeSpan>(DefaultField, ECbFieldError::TypeError);
		const FTimespan DefaultValue(0x1020'3040'5060'7080);
		TestEqual(TEXT("FCbField()::AsTimeSpan()"), DefaultField.AsTimeSpan(DefaultValue), DefaultValue);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldRefTest, FCbFieldTestBase, "System.Core.Serialization.CbFieldRef", CompactBinaryTestFlags)
bool FCbFieldRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbFieldRef, const FSharedBufferRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FSharedBufferPtr&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FSharedBufferConstRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FSharedBufferConstPtr&>::value, "Missing constructor for FCbFieldRef");

	static_assert(std::is_constructible<FCbFieldRef, FSharedBufferRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, FSharedBufferPtr&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, FSharedBufferConstRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, FSharedBufferConstPtr&&>::value, "Missing constructor for FCbFieldRef");

	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FSharedBufferRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FSharedBufferPtr&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FSharedBufferConstRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FSharedBufferConstPtr&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbFieldRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbArrayRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbObjectRef&>::value, "Missing constructor for FCbFieldRef");

	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FSharedBufferRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FSharedBufferPtr&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FSharedBufferConstRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FSharedBufferConstPtr&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbFieldRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbArrayRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbObjectRef&&>::value, "Missing constructor for FCbFieldRef");

	// Test FCbFieldRef()
	{
		FCbFieldRef DefaultField;
		TestFalse(TEXT("FCbFieldRef().HasValue()"), DefaultField.HasValue());
		TestFalse(TEXT("FCbFieldRef().IsOwned()"), DefaultField.IsOwned());
		TestFalse(TEXT("FCbFieldRef().IsReadOnly()"), DefaultField.IsReadOnly());
		DefaultField.MakeOwned();
		TestTrue(TEXT("FCbFieldRef().MakeOwned().IsOwned()"), DefaultField.IsOwned());
		TestTrue(TEXT("FCbFieldRef().MakeOwned().IsReadOnly()"), DefaultField.IsReadOnly());
	}

	// Test Field w/ Type from Shared Buffer
	{
		uint8 Payload[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FSharedBufferPtr ViewBuffer = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		FSharedBufferPtr OwnedBuffer = FSharedBuffer::MakeOwned(ViewBuffer);
		FSharedBufferConstPtr ViewBufferPtr = ViewBuffer;
		FSharedBufferConstPtr OwnedBufferPtr = OwnedBuffer;

		FCbFieldRef ViewRef(ViewBuffer);
		FCbFieldRef ViewPtr(ViewBufferPtr);
		FCbFieldRef ViewPtrMove{FSharedBufferConstPtr(ViewBufferPtr)};
		FCbFieldRef ViewOuterFieldRef(ImplicitConv<FCbField>(ViewPtr), ViewPtrMove);
		FCbFieldRef ViewOuterBufferRef(ImplicitConv<FCbField>(ViewRef), ViewPtr);
		FCbFieldRef OwnedRef(OwnedBuffer);
		FCbFieldRef OwnedPtr(OwnedBufferPtr);
		FCbFieldRef OwnedPtrMove{FSharedBufferConstPtr(OwnedBufferPtr)};
		FCbFieldRef OwnedOuterFieldRef(ImplicitConv<FCbField>(OwnedPtr), OwnedPtrMove);
		FCbFieldRef OwnedOuterBufferRef(ImplicitConv<FCbField>(OwnedRef), OwnedPtr);

		// These lines are expected to assert when uncommented.
		//FCbFieldRef InvalidOuterBuffer(ImplicitConv<FCbField>(OwnedRef), ViewBufferPtr);
		//FCbFieldRef InvalidOuterBufferMove(ImplicitConv<FCbField>(OwnedRef), FSharedBufferConstPtr(ViewBufferPtr));

		Payload[UE_ARRAY_COUNT(Payload) - 1] = 4;

		TestEqualBytes(TEXT("FCbFieldRef(ViewBufferRef)"), ViewRef.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef(ViewBufferPtr)"), ViewPtr.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef(ViewBufferPtr&&)"), ViewPtrMove.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef(ViewOuterFieldRef)"), ViewOuterFieldRef.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef(ViewOuterBufferRef)"), ViewOuterBufferRef.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef(OwnedBufferRef)"), OwnedRef.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef(OwnedBufferPtr)"), OwnedPtr.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef(OwnedBufferPtr&&)"), OwnedPtrMove.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef(OwnedOuterFieldRef)"), OwnedOuterFieldRef.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef(OwnedOuterBufferRef)"), OwnedOuterBufferRef.AsBinary(), {4, 5, 6});

		TestFalse(TEXT("FCbFieldRef(ViewBufferRef).IsOwned()"), ViewRef.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewBufferPtr).IsOwned()"), ViewPtr.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewBufferPtr&&).IsOwned()"), ViewPtrMove.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewOuterFieldRef).IsOwned()"), ViewOuterFieldRef.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewOuterBufferRef).IsOwned()"), ViewOuterBufferRef.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedBufferRef).IsOwned()"), OwnedRef.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedBufferPtr).IsOwned()"), OwnedPtr.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedBufferPtr&&).IsOwned()"), OwnedPtrMove.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedOuterFieldRef).IsOwned()"), OwnedOuterFieldRef.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedOuterBufferRef).IsOwned()"), OwnedOuterBufferRef.IsOwned());

		ViewRef.MakeOwned();
		OwnedRef.MakeOwned();
		static_cast<uint8*>(OwnedBuffer->GetData())[UE_ARRAY_COUNT(Payload) - 1] = 5;
		TestEqualBytes(TEXT("FCbFieldRef(View).MakeOwned()"), ViewRef.AsBinary(), {4, 5, 4});
		TestTrue(TEXT("FCbFieldRef(View).MakeOwned().IsOwned()"), ViewRef.IsOwned());
		TestTrue(TEXT("FCbFieldRef(View).MakeOwned().IsReadOnly()"), ViewRef.IsReadOnly());
		TestEqualBytes(TEXT("FCbFieldRef(Owned).MakeOwned()"), OwnedRef.AsBinary(), {4, 5, 5});
		TestTrue(TEXT("FCbFieldRef(Owned).MakeOwned().IsOwned()"), OwnedRef.IsOwned());
		TestFalse(TEXT("FCbFieldRef(Owned).MakeOwned().IsReadOnly()"), OwnedRef.IsReadOnly());

		OwnedRef.MakeReadOnly();
		TestEqualBytes(TEXT("FCbFieldRef(Owned).MakeReadOnly()"), OwnedRef.AsBinary(), {4, 5, 5});
		TestTrue(TEXT("FCbFieldRef(Owned).MakeReadOnly().IsOwned()"), OwnedRef.IsOwned());
		TestTrue(TEXT("FCbFieldRef(Owned).MakeReadOnly().IsReadOnly()"), OwnedRef.IsReadOnly());
	}

	// Test Field w/ Type
	{
		uint8 Payload[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		uint8* PayloadCopy = static_cast<uint8*>(FMemory::Malloc(sizeof(Payload)));
		FMemory::Memcpy(PayloadCopy, Payload, sizeof(Payload));

		FCbField Field(Payload);

		FCbFieldRef VoidTake = FCbFieldRef::TakeOwnership(ImplicitConv<const void*>(PayloadCopy), FMemory::Free);
		FCbFieldRef VoidView = FCbFieldRef::MakeView(Payload);
		FCbFieldRef VoidClone = FCbFieldRef::Clone(Payload);
		FCbFieldRef FieldView = FCbFieldRef::MakeView(Field);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		FCbFieldRef FieldRefClone = FCbFieldRef::Clone(FieldView);

		Payload[UE_ARRAY_COUNT(Payload) - 1] = 4;

		TestEqualBytes(TEXT("FCbFieldRef::TakeOwnership(Void)"), VoidTake.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef::MakeView(Void)"), VoidView.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef::Clone(Void)"), VoidClone.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef::MakeView(Field)"), FieldView.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef::Clone(Field)"), FieldClone.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef::Clone(FieldRef)"), FieldRefClone.AsBinary(), {4, 5, 6});

		TestTrue(TEXT("FCbFieldRef::TakeOwnership(Void).IsOwned()"), VoidTake.IsOwned());
		TestTrue(TEXT("FCbFieldRef::TakeOwnership(Void).IsReadOnly()"), VoidTake.IsReadOnly());
		TestFalse(TEXT("FCbFieldRef::MakeView(Void).IsOwned()"), VoidView.IsOwned());
		TestFalse(TEXT("FCbFieldRef::MakeView(Void).IsReadOnly()"), VoidView.IsReadOnly());
		TestTrue(TEXT("FCbFieldRef::Clone(Void).IsOwned()"), VoidClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Void).IsReadOnly()"), VoidClone.IsReadOnly());
		TestFalse(TEXT("FCbFieldRef::MakeView(Field).IsOwned()"), FieldView.IsOwned());
		TestFalse(TEXT("FCbFieldRef::MakeView(Field).IsReadOnly()"), FieldView.IsReadOnly());
		TestTrue(TEXT("FCbFieldRef::Clone(Field).IsOwned()"), FieldClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Field).IsReadOnly()"), FieldClone.IsReadOnly());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef).IsOwned()"), FieldRefClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef).IsReadOnly()"), FieldRefClone.IsReadOnly());
	}

	// Test Field w/o Type
	{
		uint8 Payload[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FCbField Field(Payload, ECbFieldType::Binary);

		FCbFieldRef FieldView = FCbFieldRef::MakeView(Field);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		FCbFieldRef FieldRefClone = FCbFieldRef::Clone(FieldView);

		Payload[UE_ARRAY_COUNT(Payload) - 1] = 4;

		TestEqualBytes(TEXT("FCbFieldRef::MakeView(Field, NoType)"), FieldView.AsBinary(), {4, 5, 4});
		TestEqualBytes(TEXT("FCbFieldRef::Clone(Field, NoType)"), FieldClone.AsBinary(), {4, 5, 6});
		TestEqualBytes(TEXT("FCbFieldRef::Clone(FieldRef, NoType)"), FieldRefClone.AsBinary(), {4, 5, 6});

		TestFalse(TEXT("FCbFieldRef::MakeView(Field, NoType).IsOwned()"), FieldView.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Field, NoType).IsOwned()"), FieldClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef, NoType).IsOwned()"), FieldRefClone.IsOwned());

		TestFalse(TEXT("FCbFieldRef::MakeView(Field, NoType).IsReadOnly()"), FieldView.IsReadOnly());
		TestTrue(TEXT("FCbFieldRef::Clone(Field, NoType).IsReadOnly()"), FieldClone.IsReadOnly());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef, NoType).IsReadOnly()"), FieldRefClone.IsReadOnly());

		FieldView.MakeOwned();
		TestEqualBytes(TEXT("FCbFieldRef::MakeView(NoType).MakeOwned()"), FieldView.AsBinary(), {4, 5, 4});
		TestTrue(TEXT("FCbFieldRef::MakeView(NoType).MakeOwned().IsOwned()"), FieldView.IsOwned());
		TestTrue(TEXT("FCbFieldRef::MakeView(NoType).MakeOwned().IsReadOnly()"), FieldView.IsReadOnly());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbArrayRefTest, "System.Core.Serialization.CbArrayRef", CompactBinaryTestFlags)
bool FCbArrayRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FSharedBufferConstPtr&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbFieldRef&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbArrayRef&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbObjectRef&>::value, "Missing constructor for FCbArrayRef");

	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FSharedBufferConstPtr&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbFieldRef&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbArrayRef&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbObjectRef&&>::value, "Missing constructor for FCbArrayRef");

	// Test FCbArrayRef()
	{
		FCbArrayRef DefaultArray;
		TestFalse(TEXT("FCbArrayRef().IsOwned()"), DefaultArray.IsOwned());
		TestFalse(TEXT("FCbArrayRef().IsReadOnly()"), DefaultArray.IsReadOnly());
		DefaultArray.MakeOwned();
		TestTrue(TEXT("FCbArrayRef().MakeOwned().IsOwned()"), DefaultArray.IsOwned());
		TestTrue(TEXT("FCbArrayRef().MakeOwned().IsReadOnly()"), DefaultArray.IsReadOnly());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbObjectRefTest, "System.Core.Serialization.CbObjectRef", CompactBinaryTestFlags)
bool FCbObjectRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FSharedBufferConstPtr&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbFieldRef&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbArrayRef&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbObjectRef&>::value, "Missing constructor for FCbObjectRef");

	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FSharedBufferConstPtr&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbFieldRef&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbArrayRef&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbObjectRef&&>::value, "Missing constructor for FCbObjectRef");

	// Test FCbObjectRef()
	{
		FCbObjectRef DefaultObject;
		TestFalse(TEXT("FCbObjectRef().IsOwned()"), DefaultObject.IsOwned());
		TestFalse(TEXT("FCbObjectRef().IsReadOnly()"), DefaultObject.IsReadOnly());
		DefaultObject.MakeOwned();
		TestTrue(TEXT("FCbObjectRef().MakeOwned().IsOwned()"), DefaultObject.IsOwned());
		TestTrue(TEXT("FCbObjectRef().MakeOwned().IsReadOnly()"), DefaultObject.IsReadOnly());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbFieldRefIteratorTest, "System.Core.Serialization.CbFieldRefIterator", CompactBinaryTestFlags)
bool FCbFieldRefIteratorTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbFieldIterator, const FCbField&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, const FCbFieldRef&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbField&&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbFieldRef&&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbField&&, const void*>::value, "Missing constructor for FCbFieldIterator");

	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldRef&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, FCbFieldRef&&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, FCbFieldRef&&, const void*>::value, "Missing constructor for FCbFieldRefIterator");

	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, const FSharedBufferRef&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, const FSharedBufferPtr&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, const FSharedBufferConstRef&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, const FSharedBufferConstPtr&>::value, "Missing constructor for FCbFieldRefIterator");

	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, FSharedBufferRef&&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, FSharedBufferPtr&&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, FSharedBufferConstRef&&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldIterator&, FSharedBufferConstPtr&&>::value, "Missing constructor for FCbFieldRefIterator");

	const auto GetCount = [](auto It) -> uint32
	{
		uint32 Count = 0;
		for (; It; ++It)
		{
			++Count;
		}
		return Count;
	};

	// Test FCbField[Ref]Iterator()
	{
		TestEqual(TEXT("FCbFieldIterator()"), GetCount(FCbFieldIterator()), 0);
		TestEqual(TEXT("FCbFieldRefIterator()"), GetCount(FCbFieldRefIterator()), 0);
	}

	// Test FCbField[Ref]Iterator(Range)
	{
		constexpr uint8 T = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { T, 0, T, 1, T, 2, T, 3 };
		const uint8* const PayloadEnd = Payload + UE_ARRAY_COUNT(Payload);

		const FSharedBufferConstPtr View = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		const void* const ViewEnd = View->GetView().GetDataEnd();
		const FSharedBufferConstPtr Clone = FSharedBuffer::Clone(MakeMemoryView(Payload));
		const void* const CloneEnd = Clone->GetView().GetDataEnd();

		const FConstMemoryView EmptyView;
		const FSharedBufferConstPtr NullBuffer;

		const FCbFieldIterator FieldIt(FCbField(View->GetData()), ViewEnd);
		const FCbFieldRefIterator FieldRefIt(FCbFieldRef(View), ViewEnd);

		TestEqual(TEXT("FCbFieldIterator(EmptyView)"), GetCount(FCbFieldIterator(EmptyView)), 0);
		TestEqual(TEXT("FCbFieldRefIterator(BufferNullL)"), GetCount(FCbFieldRefIterator(NullBuffer)), 0);
		TestEqual(TEXT("FCbFieldRefIterator(BufferNullR)"), GetCount(FCbFieldRefIterator(FSharedBufferConstPtr(NullBuffer))), 0);

		TestEqual(TEXT("FCbFieldIterator(View)"), GetCount(FCbFieldIterator(MakeMemoryView(Payload))), 4);
		TestEqual(TEXT("FCbFieldRefIterator(BufferCloneL)"), GetCount(FCbFieldRefIterator(Clone)), 4);
		TestEqual(TEXT("FCbFieldRefIterator(BufferCloneR)"), GetCount(FCbFieldRefIterator(FSharedBufferConstPtr(Clone))), 4);

		TestEqual(TEXT("FCbFieldIterator(Field, End)"), GetCount(FCbFieldIterator(FCbField(Payload), PayloadEnd)), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldRef, End)"), GetCount(FCbFieldRefIterator(FCbFieldRef(View), PayloadEnd)), 4);

		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferNullL)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), NullBuffer)), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferNullR)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), FSharedBufferConstPtr(NullBuffer))), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferViewL)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), View)), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferViewR)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), FSharedBufferConstPtr(View))), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferCloneL)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(Clone->GetData()), CloneEnd), Clone)), 4);
		TestEqual(TEXT("FCbFieldRefIterator(FieldIt, BufferCloneR)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(Clone->GetData()), CloneEnd), FSharedBufferConstPtr(Clone))), 4);

		TestEqual(TEXT("FCbFieldIterator(FieldRefItL)"), GetCount(FCbFieldIterator(FieldRefIt)), 4);
		TestEqual(TEXT("FCbFieldIterator(FieldRefItR)"), GetCount(FCbFieldIterator(FCbFieldRefIterator(FieldRefIt))), 4);

		// These lines are expected to assert when uncommented.
		//const FSharedBufferConstPtr ShortView = FSharedBuffer::MakeView(MakeMemoryView(Payload).LeftChop(2));
		//TestEqual(TEXT("FCbFieldRefIterator(FieldIt, InvalidBufferL)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), ShortView)), 4);
		//TestEqual(TEXT("FCbFieldRefIterator(FieldIt, InvalidBufferR)"), GetCount(FCbFieldRefIterator(FCbFieldIterator(FCbField(View->GetData()), ViewEnd), FSharedBufferConstPtr(ShortView))), 4);
	}

	// Test FCbField[Ref]Iterator(Scalar)
	{
		constexpr uint8 T = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { T, 0 };
		const uint8* const PayloadEnd = Payload + UE_ARRAY_COUNT(Payload);

		const FSharedBufferConstPtr View = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		const void* const ViewEnd = View->GetView().GetDataEnd();
		const FSharedBufferConstPtr Clone = FSharedBuffer::Clone(MakeMemoryView(Payload));
		const void* const CloneEnd = Clone->GetView().GetDataEnd();
		const FSharedBufferConstPtr NullBuffer;

		const FCbField Field(Payload);
		const FCbFieldRef FieldRef(View);

		TestEqual(TEXT("FCbFieldIterator(FieldL)"), GetCount(FCbFieldIterator(Field)), 1);
		TestEqual(TEXT("FCbFieldIterator(FieldR)"), GetCount(FCbFieldIterator(FCbField(Field))), 1);
		TestEqual(TEXT("FCbFieldRefIterator(FieldRefL)"), GetCount(FCbFieldRefIterator(FieldRef)), 1);
		TestEqual(TEXT("FCbFieldRefIterator(FieldRefR)"), GetCount(FCbFieldRefIterator(FCbFieldRef(FieldRef))), 1);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbMeasureTest, "System.Core.Serialization.MeasureCompactBinary", CompactBinaryTestFlags)
bool FCbMeasureTest::RunTest(const FString& Parameters)
{
	auto Measure = [this](std::initializer_list<uint8> Data, ECbFieldType Type = ECbFieldType::HasFieldType) -> uint64
	{
		return MeasureCompactBinary(MakeMemoryView(Data), Type);
	};

	TestEqual(TEXT("MeasureCompactBinary(Empty)"), Measure({}), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(Null, NoType)"), Measure({}, ECbFieldType::Null), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize1B, NoType)"), Measure({30}, ECbFieldType::Null | ECbFieldType::HasFieldName), uint64(31));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize2B, NoType)"), Measure({0x80, 0x80}, ECbFieldType::Null | ECbFieldType::HasFieldName), uint64(130));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize2BShort, NoType)"), Measure({0x80}, ECbFieldType::Null | ECbFieldType::HasFieldName), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Null, MissingName, NoType)"), Measure({}, ECbFieldType::Null | ECbFieldType::HasFieldName), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(Null)"), Measure({uint8(ECbFieldType::Null)}), uint64(1));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize1B)"), Measure({uint8(ECbFieldType::Null | ECbFieldType::HasFieldName), 30}), uint64(32));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize2B)"), Measure({uint8(ECbFieldType::Null | ECbFieldType::HasFieldName), 0x80, 0x80}), uint64(131));
	TestEqual(TEXT("MeasureCompactBinary(Null, NameSize2BShort)"), Measure({uint8(ECbFieldType::Null | ECbFieldType::HasFieldName), 0x80}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Null, MissingName)"), Measure({uint8(ECbFieldType::Null | ECbFieldType::HasFieldName)}), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(Object, NoSize)"), Measure({uint8(ECbFieldType::Object)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Object, Size1B)"), Measure({uint8(ECbFieldType::Object), 30}), uint64(32));
	TestEqual(TEXT("MeasureCompactBinary(UniformObject, NoSize)"), Measure({uint8(ECbFieldType::UniformObject)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(UniformObject, Size1B)"), Measure({uint8(ECbFieldType::UniformObject), 30}), uint64(32));

	TestEqual(TEXT("MeasureCompactBinary(Array, NoSize)"), Measure({uint8(ECbFieldType::Array)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Array, Size1B)"), Measure({uint8(ECbFieldType::Array), 30}), uint64(32));
	TestEqual(TEXT("MeasureCompactBinary(UniformArray, NoSize)"), Measure({uint8(ECbFieldType::UniformArray)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(UniformArray, Size1B)"), Measure({uint8(ECbFieldType::UniformArray), 30}), uint64(32));

	TestEqual(TEXT("MeasureCompactBinary(Binary, NoSize)"), Measure({uint8(ECbFieldType::Binary)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Binary, Size1B)"), Measure({uint8(ECbFieldType::Binary), 30}), uint64(32));

	TestEqual(TEXT("MeasureCompactBinary(String, NoSize)"), Measure({uint8(ECbFieldType::String)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(String, Size1B)"), Measure({uint8(ECbFieldType::String), 30}), uint64(32));
	TestEqual(TEXT("MeasureCompactBinary(String, Size2B)"), Measure({uint8(ECbFieldType::String), 0x80, 0x80}), uint64(131));
	TestEqual(TEXT("MeasureCompactBinary(String, Size2BShort)"), Measure({uint8(ECbFieldType::String), 0x80}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(String, MissingNameSize)"), Measure({uint8(ECbFieldType::String | ECbFieldType::HasFieldName)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(String, MissingName)"), Measure({uint8(ECbFieldType::String | ECbFieldType::HasFieldName), 1}), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(IntegerPositive, NoValue)"), Measure({uint8(ECbFieldType::IntegerPositive)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(IntegerPositive, Value1B)"), Measure({uint8(ECbFieldType::IntegerPositive), 0x7f}), uint64(2));
	TestEqual(TEXT("MeasureCompactBinary(IntegerPositive, Value2B)"), Measure({uint8(ECbFieldType::IntegerPositive), 0x80}), uint64(3));

	TestEqual(TEXT("MeasureCompactBinary(IntegerNegative, NoValue)"), Measure({uint8(ECbFieldType::IntegerNegative)}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(IntegerNegative, Value1B)"), Measure({uint8(ECbFieldType::IntegerNegative), 0x7f}), uint64(2));
	TestEqual(TEXT("MeasureCompactBinary(IntegerNegative, Value2B)"), Measure({uint8(ECbFieldType::IntegerNegative), 0x80}), uint64(3));

	TestEqual(TEXT("MeasureCompactBinary(Float32, NoType)"), Measure({}, ECbFieldType::Float32), uint64(4));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize1B, NoType)"), Measure({30}, ECbFieldType::Float32 | ECbFieldType::HasFieldName), uint64(35));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize2B, NoType)"), Measure({0x80, 0x80}, ECbFieldType::Float32 | ECbFieldType::HasFieldName), uint64(134));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize2BShort, NoType)"), Measure({0x80}, ECbFieldType::Float32 | ECbFieldType::HasFieldName), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Float32, MissingName, NoType)"), Measure({}, ECbFieldType::Float32 | ECbFieldType::HasFieldName), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(Float32)"), Measure({uint8(ECbFieldType::Float32)}), uint64(5));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize1B)"), Measure({uint8(ECbFieldType::Float32 | ECbFieldType::HasFieldName), 30}), uint64(36));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize2B)"), Measure({uint8(ECbFieldType::Float32 | ECbFieldType::HasFieldName), 0x80, 0x80}), uint64(135));
	TestEqual(TEXT("MeasureCompactBinary(Float32, NameSize2BShort)"), Measure({uint8(ECbFieldType::Float32 | ECbFieldType::HasFieldName), 0x80}), uint64(0));
	TestEqual(TEXT("MeasureCompactBinary(Float32, MissingName)"), Measure({uint8(ECbFieldType::Float32 | ECbFieldType::HasFieldName)}), uint64(0));

	TestEqual(TEXT("MeasureCompactBinary(Float64)"), Measure({uint8(ECbFieldType::Float64)}), uint64(9));

	TestEqual(TEXT("MeasureCompactBinary(BoolFalse)"), Measure({uint8(ECbFieldType::BoolFalse)}), uint64(1));
	TestEqual(TEXT("MeasureCompactBinary(BoolTrue)"), Measure({uint8(ECbFieldType::BoolTrue)}), uint64(1));

	TestEqual(TEXT("MeasureCompactBinary(Reference)"), Measure({uint8(ECbFieldType::Reference)}), uint64(33));
	TestEqual(TEXT("MeasureCompactBinary(BinaryReference)"), Measure({uint8(ECbFieldType::BinaryReference)}), uint64(33));

	TestEqual(TEXT("MeasureCompactBinary(Hash)"), Measure({uint8(ECbFieldType::Hash)}), uint64(33));
	TestEqual(TEXT("MeasureCompactBinary(Uuid)"), Measure({uint8(ECbFieldType::Uuid)}), uint64(17));

	TestEqual(TEXT("MeasureCompactBinary(DateTime)"), Measure({uint8(ECbFieldType::DateTime)}), uint64(9));
	TestEqual(TEXT("MeasureCompactBinary(TimeSpan)"), Measure({uint8(ECbFieldType::TimeSpan)}), uint64(9));

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbFieldParseTest, "System.Core.Serialization.CbFieldParseTest", CompactBinaryTestFlags)
bool FCbFieldParseTest::RunTest(const FString& Parameters)
{
	// Test the optimal object parsing loop because it is expected to be required for high performance.
	// Under ideal conditions, when the fields are in the expected order and there are no extra fields,
	// the loop will execute once and only one comparison will be performed for each field name. Either
	// way, each field will only be visited once even if the loop needs to execute several times.
	auto ParseObject = [this](const FCbObject& Object, uint32& A, uint32& B, uint32& C, uint32& D)
	{
		for (FCbFieldIterator It = Object.CreateIterator(); It;)
		{
			const FCbFieldIterator Last = It;
			if (It.GetName().Equals("A"_ASV))
			{
				A = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("B"_ASV))
			{
				B = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("C"_ASV))
			{
				C = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("D"_ASV))
			{
				D = It.AsUInt32();
				++It;
			}
			if (Last == It)
			{
				++It;
			}
		}
	};

	auto TestParseObject = [this, &ParseObject](std::initializer_list<uint8> Data, uint32 A, uint32 B, uint32 C, uint32 D) -> bool
	{
		uint32 ParsedA = 0, ParsedB = 0, ParsedC = 0, ParsedD = 0;
		ParseObject(FCbObject(GetData(Data), ECbFieldType::Object), ParsedA, ParsedB, ParsedC, ParsedD);
		return A == ParsedA && B == ParsedB && C == ParsedC && D == ParsedD;
	};

	constexpr uint8 T = uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName);
	TestTrue(TEXT("FCbObject Parse(None)"), TestParseObject({0}, 0, 0, 0, 0));
	TestTrue(TEXT("FCbObject Parse(ABCD)"), TestParseObject({16, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 1, 2, 3, 4));
	TestTrue(TEXT("FCbObject Parse(BCDA)"), TestParseObject({16, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4, T, 1, 'A', 1}, 1, 2, 3, 4));
	TestTrue(TEXT("FCbObject Parse(BCD)"), TestParseObject({12, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 0, 2, 3, 4));
	TestTrue(TEXT("FCbObject Parse(BC)"), TestParseObject({8, T, 1, 'B', 2, T, 1, 'C', 3}, 0, 2, 3, 0));
	TestTrue(TEXT("FCbObject Parse(ABCDE)"), TestParseObject({20, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4, T, 1, 'E', 5}, 1, 2, 3, 4));
	TestTrue(TEXT("FCbObject Parse(EABCD)"), TestParseObject({20, T, 1, 'E', 5, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 1, 2, 3, 4));
	TestTrue(TEXT("FCbObject Parse(DCBA)"), TestParseObject({16, T, 1, 'D', 4, T, 1, 'C', 3, T, 1, 'B', 2, T, 1, 'A', 1}, 1, 2, 3, 4));

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // WITH_DEV_AUTOMATION_TESTS
