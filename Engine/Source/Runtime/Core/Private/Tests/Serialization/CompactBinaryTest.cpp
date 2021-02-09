// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "IO/IoHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
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

#define UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(FieldType, InIsTypeFn, InAsTypeFn, InValueType)                           \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbField::*IsType)() const = &FCbField::InIsTypeFn;                                    \
		static auto AsType(FCbField& Field, ValueType) { return Field.InAsTypeFn(); }                                 \
	};

#define UE_CBFIELD_TYPE_ACCESSOR_EX(FieldType, InIsTypeFn, InAsTypeFn, InValueType, InDefaultType)                    \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbField::*IsType)() const = &FCbField::InIsTypeFn;                                    \
		static constexpr InValueType (FCbField::*AsType)(InDefaultType) = &FCbField::InAsTypeFn;                      \
	};

#define UE_CBFIELD_TYPE_ACCESSOR(FieldType, InIsTypeFn, InAsTypeFn, InValueType)                                      \
	UE_CBFIELD_TYPE_ACCESSOR_EX(FieldType, InIsTypeFn, InAsTypeFn, InValueType, InValueType)

UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(Object, IsObject, AsObject, FCbObject);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(UniformObject, IsObject, AsObject, FCbObject);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(Array, IsArray, AsArray, FCbArray);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(UniformArray, IsArray, AsArray, FCbArray);
UE_CBFIELD_TYPE_ACCESSOR(Binary, IsBinary, AsBinary, FMemoryView);
UE_CBFIELD_TYPE_ACCESSOR(String, IsString, AsString, FAnsiStringView);
UE_CBFIELD_TYPE_ACCESSOR(IntegerPositive, IsInteger, AsUInt64, uint64);
UE_CBFIELD_TYPE_ACCESSOR(IntegerNegative, IsInteger, AsInt64, int64);
UE_CBFIELD_TYPE_ACCESSOR(Float32, IsFloat, AsFloat, float);
UE_CBFIELD_TYPE_ACCESSOR(Float64, IsFloat, AsDouble, double);
UE_CBFIELD_TYPE_ACCESSOR(BoolFalse, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR(BoolTrue, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR_EX(CompactBinaryAttachment, IsCompactBinaryAttachment, AsCompactBinaryAttachment, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(BinaryAttachment, IsBinaryAttachment, AsBinaryAttachment, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(Hash, IsHash, AsHash, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(Uuid, IsUuid, AsUuid, FGuid, const FGuid&);
UE_CBFIELD_TYPE_ACCESSOR(DateTime, IsDateTime, AsDateTimeTicks, int64);
UE_CBFIELD_TYPE_ACCESSOR(TimeSpan, IsTimeSpan, AsTimeSpanTicks, int64);

struct FCbAttachmentAccessors
{
	static constexpr bool (FCbField::*IsType)() const = &FCbField::IsAttachment;
	static constexpr FIoHash (FCbField::*AsType)(const FIoHash&) = &FCbField::AsAttachment;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbFieldTestBase : public FAutomationTestBase
{
protected:
	using FAutomationTestBase::FAutomationTestBase;
	using FAutomationTestBase::TestEqual;

	bool TestEqual(const FString& What, const FCbArray& Actual, const FCbArray& Expected)
	{
		return TestTrue(What, Actual.Equals(Expected));
	}

	bool TestEqual(const FString& What, const FCbObject& Actual, const FCbObject& Expected)
	{
		return TestTrue(What, Actual.Equals(Expected));
	}

	bool TestEqualBytes(const TCHAR* What, FMemoryView Actual, TArrayView<const uint8> Expected)
	{
		return TestTrue(What, Actual.EqualBytes(MakeMemoryView(Expected)));
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
	void TestFieldNoClone(const TCHAR* What, FCbField& Field, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
	{
		TestEqual(FString::Printf(TEXT("FCbField::Is[Type](%s)"), What), Invoke(Accessors.IsType, Field), ExpectedError != ECbFieldError::TypeError);
		if (ExpectedError == ECbFieldError::None && !Field.IsBool())
		{
			TestFalse(FString::Printf(TEXT("FCbField::AsBool(%s) == false"), What), Field.AsBool());
			TestTrue(FString::Printf(TEXT("FCbField::AsBool(%s) -> HasError()"), What), Field.HasError());
			TestEqual(FString::Printf(TEXT("FCbField::AsBool(%s) -> GetError() == TypeError"), What), Field.GetError(), ECbFieldError::TypeError);
		}
		TestEqual(FString::Printf(TEXT("FCbField::As[Type](%s) -> Equal"), What), Invoke(Accessors.AsType, Field, DefaultValue), ExpectedValue);
		TestEqual(FString::Printf(TEXT("FCbField::As[Type](%s) -> HasError()"), What), Field.HasError(), ExpectedError != ECbFieldError::None);
		TestEqual(FString::Printf(TEXT("FCbField::As[Type](%s) -> GetError()"), What), Field.GetError(), ExpectedError);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
	void TestField(const TCHAR* What, FCbField& Field, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
	{
		TestFieldNoClone<FieldType>(What, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		TestFieldNoClone<FieldType>(*FString::Printf(TEXT("%s, Clone"), What), FieldClone, ExpectedValue, DefaultValue, ExpectedError, Accessors);
		TestTrue(FString::Printf(TEXT("FCbField::Equals(%s)"), What), Field.Equals(FieldClone));
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
	void TestField(const TCHAR* What, TArrayView<const uint8> Payload, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
	{
		FCbField Field(Payload.GetData(), FieldType);
		TestEqual(FString::Printf(TEXT("FCbField::GetSize(%s)"), What), Field.GetSize(), uint64(Payload.Num() + !FCbFieldType::HasFieldType(FieldType)));
		TestTrue(FString::Printf(TEXT("FCbField::HasValue(%s)"), What), Field.HasValue());
		TestFalse(FString::Printf(TEXT("FCbField::HasError(%s) == false"), What), Field.HasError());
		TestEqual(FString::Printf(TEXT("FCbField::GetError(%s) == None"), What), Field.GetError(), ECbFieldError::None);
		TestField<FieldType>(What, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
	void TestFieldError(const TCHAR* What, FCbField& Field, ECbFieldError ExpectedError, T ExpectedValue = T(), const AccessorsType& Accessors = AccessorsType())
	{
		TestFieldNoClone<FieldType>(What, Field, ExpectedValue, ExpectedValue, ExpectedError, Accessors);
	}

	template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
	void TestFieldError(const TCHAR* What, TArrayView<const uint8> Payload, ECbFieldError ExpectedError, T ExpectedValue = T(), const AccessorsType& Accessors = AccessorsType())
	{
		FCbField Field(Payload.GetData(), FieldType);
		TestFieldError<FieldType>(What, Field, ExpectedError, ExpectedValue, Accessors);
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
		TestEqual(TEXT("FCbField()::GetSize() == 1"), DefaultField.GetSize(), uint64(1));
		TestEqual(TEXT("FCbField()::GetName().Len() == 0"), DefaultField.GetName().Len(), 0);
		TestFalse(TEXT("!FCbField()::HasName()"), DefaultField.HasName());
		TestFalse(TEXT("!FCbField()::HasValue()"), DefaultField.HasValue());
		TestFalse(TEXT("!FCbField()::HasError()"), DefaultField.HasError());
		TestEqual(TEXT("FCbField()::GetError() == None"), DefaultField.GetError(), ECbFieldError::None);
		TestEqual(TEXT("FCbField()::GetHash()"), DefaultField.GetHash(), FIoHash::HashBuffer(MakeMemoryView<uint8>({uint8(ECbFieldType::None)})));
		TestEqual(TEXT("FCbField()::GetView()"), DefaultField.GetView(), FMemoryView());
		FMemoryView SerializedView;
		TestFalse(TEXT("FCbField()::TryGetSerializedView()"), DefaultField.TryGetSerializedView(SerializedView));
	}

	// Test FCbField(None)
	{
		FCbField NoneField(nullptr, ECbFieldType::None);
		TestEqual(TEXT("FCbField(None)::GetSize() == 1"), NoneField.GetSize(), uint64(1));
		TestEqual(TEXT("FCbField(None)::GetName().Len() == 0"), NoneField.GetName().Len(), 0);
		TestFalse(TEXT("!FCbField(None)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None)::HasValue()"), NoneField.HasValue());
		TestFalse(TEXT("!FCbField(None)::HasError()"), NoneField.HasError());
		TestEqual(TEXT("FCbField(None)::GetError() == None"), NoneField.GetError(), ECbFieldError::None);
		TestEqual(TEXT("FCbField(None)::GetHash()"), NoneField.GetHash(), FCbField().GetHash());
		TestEqual(TEXT("FCbField(None)::GetView()"), NoneField.GetView(), FMemoryView());
		FMemoryView SerializedView;
		TestFalse(TEXT("FCbField(None)::TryGetSerializedView()"), NoneField.TryGetSerializedView(SerializedView));
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
		TestEqual(TEXT("FCbField(None|Type|Name)::GetHash()"), NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Type|Name)::GetView()"), NoneField.GetView(), MakeMemoryView(NoneBytes));
		FMemoryView SerializedView;
		TestTrue(TEXT("FCbField(None|Type|Name)::TryGetSerializedView()"), NoneField.TryGetSerializedView(SerializedView) && SerializedView == MakeMemoryView(NoneBytes));

		uint8 CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbField(None|Type|Name)::CopyTo()"), MakeMemoryView(NoneBytes).EqualBytes(MakeMemoryView(CopyBytes)));
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
		TestEqual(TEXT("FCbField(None|Type)::GetHash()"), NoneField.GetHash(), FCbField().GetHash());
		TestEqual(TEXT("FCbField(None|Type)::GetView()"), NoneField.GetView(), MakeMemoryView(NoneBytes));
		FMemoryView SerializedView;
		TestTrue(TEXT("FCbField(None|Type)::TryGetSerializedView()"), NoneField.TryGetSerializedView(SerializedView) && SerializedView == MakeMemoryView(NoneBytes));
	}

	// Test FCbField(None|Name)
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType), 4, 'N', 'a', 'm', 'e' };
		FCbField NoneField(NoneBytes + 1, FieldType);
		TestEqual(TEXT("FCbField(None|Name)::GetSize()"), NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Name)::GetName()"), NoneField.GetName(), "Name"_ASV);
		TestTrue(TEXT("FCbField(None|Name)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None|Name)::HasValue()"), NoneField.HasValue());
		TestEqual(TEXT("FCbField(None|Name)::GetHash()"), NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		TestEqual(TEXT("FCbField(None|Name)::GetView()"), NoneField.GetView(), MakeMemoryView(NoneBytes) + 1);
		FMemoryView SerializedView;
		TestFalse(TEXT("FCbField(None|Name)::TryGetSerializedView()"), NoneField.TryGetSerializedView(SerializedView));

		uint8 CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbField(None|Name)::CopyTo()"), MakeMemoryView(NoneBytes).EqualBytes(MakeMemoryView(CopyBytes)));
	}

	// Test FCbField(None|EmptyName)
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const uint8 NoneBytes[] = { uint8(FieldType), 0 };
		FCbField NoneField(NoneBytes + 1, FieldType);
		TestEqual(TEXT("FCbField(None|EmptyName)::GetSize()"), NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		TestEqual(TEXT("FCbField(None|EmptyName)::GetName()"), NoneField.GetName(), ""_ASV);
		TestTrue(TEXT("FCbField(None|EmptyName)::HasName()"), NoneField.HasName());
		TestFalse(TEXT("!FCbField(None|EmptyName)::HasValue()"), NoneField.HasValue());
		TestEqual(TEXT("FCbField(None|EmptyName)::GetHash()"), NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		TestEqual(TEXT("FCbField(None|EmptyName)::GetView()"), NoneField.GetView(), MakeMemoryView(NoneBytes) + 1);
		FMemoryView SerializedView;
		TestFalse(TEXT("FCbField(None|EmptyName)::TryGetSerializedView()"), NoneField.TryGetSerializedView(SerializedView));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldNullTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Null", CompactBinaryTestFlags)
bool FCbFieldNullTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Null)
	{
		FCbField NullField(nullptr, ECbFieldType::Null);
		TestEqual(TEXT("FCbField(Null)::GetSize() == 1"), NullField.GetSize(), uint64(1));
		TestTrue(TEXT("FCbField(Null)::IsNull()"), NullField.IsNull());
		TestTrue(TEXT("FCbField(Null)::HasValue()"), NullField.HasValue());
		TestFalse(TEXT("!FCbField(Null)::HasError()"), NullField.HasError());
		TestEqual(TEXT("FCbField(Null)::GetError() == None"), NullField.GetError(), ECbFieldError::None);
		TestEqual(TEXT("FCbField(Null)::GetHash()"), NullField.GetHash(), FIoHash::HashBuffer(MakeMemoryView<uint8>({uint8(ECbFieldType::Null)})));
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
	static_assert(!std::is_constructible<FCbField, const FCbObject&>::value, "Invalid constructor for FCbField");
	static_assert(!std::is_assignable<FCbField, const FCbObject&>::value, "Invalid assignment for FCbField");
	static_assert(!std::is_convertible<FCbField, FCbObject>::value, "Invalid conversion to FCbObject");
	static_assert(!std::is_assignable<FCbObject, const FCbField&>::value, "Invalid assignment for FCbObject");

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
	TestField<ECbFieldType::Object>(TEXT("Object, Empty"), {0});

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
		TestField<ECbFieldType::Object>(TEXT("Object, NotEmpty"), Field, FCbObject(Payload, ECbFieldType::Object));
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
		TestEqual(TEXT("FCbObject::GetView()"), Field.AsObject().GetView(), Field.GetView());
	}

	// Test FCbField(UniformObject, NotEmpty)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbField Field(Payload, ECbFieldType::UniformObject);
		TestField<ECbFieldType::UniformObject>(TEXT("UniformObject, NotEmpty"), Field, FCbObject(Payload, ECbFieldType::UniformObject));
		FCbObjectRef Object = FCbObjectRef::Clone(Field.AsObject());
		TestIntObject(Object, 3, sizeof(Payload));
		TestIntObject(Field.AsObject(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbObject(Uniform)::Equals()"), Object.Equals(Field.AsObject()));
		TestEqual(TEXT("FCbObject(Uniform)::Find()"), Object.Find("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::Find()"), Object.FindRef("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::Find()"), Object.Find("b"_ASV).AsInt32(4), 4);
		TestEqual(TEXT("FCbObject(Uniform)::Find()"), Object.FindRef("b"_ASV).AsInt32(4), 4);
		TestEqual(TEXT("FCbObject(Uniform)::FindIgnoreCase()"), Object.FindIgnoreCase("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::FindIgnoreCase()"), Object.FindRefIgnoreCase("B"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::FindIgnoreCase()"), Object.FindIgnoreCase("b"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::FindIgnoreCase()"), Object.FindRefIgnoreCase("b"_ASV).AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::operator[]"), Object["B"_ASV].AsInt32(), 2);
		TestEqual(TEXT("FCbObject(Uniform)::operator[]"), Object["b"_ASV].AsInt32(4), 4);
		TestEqual(TEXT("FCbObject(Uniform)::GetView()"), Field.AsObject().GetView(), Field.GetView());

		TestTrue(TEXT("FCbObjectRef::AsFieldRef()"), Object.GetBuffer() == Object.AsFieldRef().AsObjectRef().GetBuffer());

		// Equals
		const uint8 NamedPayload[] = { 1, 'O', 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbField NamedField(NamedPayload, ECbFieldType::UniformObject | ECbFieldType::HasFieldName);
		TestTrue(TEXT("FCbObject::Equals()"), Field.AsObject().Equals(NamedField.AsObject()));
		TestTrue(TEXT("FCbObject::AsField().Equals()"), Field.Equals(Field.AsObject().AsField()));
		TestTrue(TEXT("FCbObject::AsField().Equals()"), NamedField.Equals(NamedField.AsObject().AsField()));
		TestEqual(TEXT("FCbObject(Name)::GetView()"), NamedField.AsObject().GetView(), NamedField.GetView());

		// CopyTo
		uint8 CopyBytes[sizeof(Payload) + 1];
		Field.AsObject().CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbObject(NoType)::CopyTo()"), MakeMemoryView(Payload).EqualBytes(MakeMemoryView(CopyBytes) + 1));
		NamedField.AsObject().CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbObject(NoType, Name)::CopyTo()"), MakeMemoryView(Payload).EqualBytes(MakeMemoryView(CopyBytes) + 1));

		// GetSerializedView
		FMemoryView SerializedView;
		TestTrue(TEXT("FCbObject(Clone)::TryGetSerializedView()"), Object.TryGetSerializedView(SerializedView) && SerializedView == Object.AsField().GetView());
		TestFalse(TEXT("FCbObject(NoType)::TryGetSerializedView()"), Field.AsObject().TryGetSerializedView(SerializedView));
		TestFalse(TEXT("FCbObject(Name)::TryGetSerializedView()"), NamedField.AsObject().TryGetSerializedView(SerializedView));
	}

	// Test FCbField(None) as Object
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Object>(TEXT("Object, None"), Field, ECbFieldError::TypeError);
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
		TestEqual(TEXT("FCbObject::GetHash()"), ObjectClone.GetHash(), Object.GetHash());
		for (FCbFieldRefIterator It = ObjectClone.CreateRefIterator(); It; ++It)
		{
			FCbFieldRef Field = *It;
			TestEqual(TEXT("FCbObjectRef::CreateRefIterator().GetName()"), Field.GetName(), "F"_ASV);
			TestEqual(TEXT("FCbObjectRef::CreateRefIterator().AsInt32()"), Field.AsInt32(), 8);
			TestTrue(TEXT("FCbObjectRef::CreateRefIterator().IsOwned()"), Field.IsOwned());
			TestEqual(TEXT("FCbObjectRef::CreateRefIterator().GetBuffer()"), Field.GetBuffer().GetView(), Field.GetView());
		}
		for (FCbFieldRefIterator It = ObjectClone.CreateRefIterator(), End; It != End; ++It)
		{
		}

		// CopyTo
		uint8 CopyBytes[6];
		Object.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbObject(Name)::CopyTo()"), ObjectClone.GetView().EqualBytes(MakeMemoryView(CopyBytes)));
		ObjectClone.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbObject()::CopyTo()"), ObjectClone.GetView().EqualBytes(MakeMemoryView(CopyBytes)));
	}

	// Test FCbObject as FCbFieldIterator
	{
		uint32 Count = 0;
		FCbObject Object;
		for (FCbField Field : FCbFieldIterator::MakeSingle(Object.AsField()))
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
		for (FCbFieldRef Field : FCbFieldRefIterator::MakeSingle(Object.AsFieldRef()))
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
	static_assert(!std::is_constructible<FCbField, const FCbArray&>::value, "Invalid constructor for FCbField");
	static_assert(!std::is_assignable<FCbField, const FCbArray&>::value, "Invalid assignment for FCbField");
	static_assert(!std::is_convertible<FCbField, FCbArray>::value, "Invalid conversion to FCbArray");
	static_assert(!std::is_assignable<FCbArray, const FCbField&>::value, "Invalid assignment for FCbArray");

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
	TestField<ECbFieldType::Array>(TEXT("Array, Empty"), {1, 0});

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
		TestField<ECbFieldType::Array>(TEXT("Array, NotEmpty"), Field, FCbArray(Payload, ECbFieldType::Array));
		FCbArrayRef Array = FCbArrayRef::Clone(Field.AsArray());
		TestIntArray(Array, 3, sizeof(Payload));
		TestIntArray(Field.AsArray(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(Field.AsArray()));
		TestEqual(TEXT("FCbArray::GetView()"), Field.AsArray().GetView(), Field.GetView());
	}

	// Test FCbField(UniformArray)
	{
		constexpr uint8 IntType = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { 5, 3, IntType, 1, 2, 3 };
		FCbField Field(Payload, ECbFieldType::UniformArray);
		TestField<ECbFieldType::UniformArray>(TEXT("UniformArray"), Field, FCbArray(Payload, ECbFieldType::UniformArray));
		FCbArrayRef Array = FCbArrayRef::Clone(Field.AsArray());
		TestIntArray(Array, 3, sizeof(Payload));
		TestIntArray(Field.AsArray(), 3, sizeof(Payload));
		TestTrue(TEXT("FCbArray(Uniform)::Equals()"), Array.Equals(Field.AsArray()));
		TestEqual(TEXT("FCbArray(Uniform)::GetView()"), Field.AsArray().GetView(), Field.GetView());

		TestTrue(TEXT("FCbArrayRef::AsFieldRef()"), Array.GetBuffer() == Array.AsFieldRef().AsArrayRef().GetBuffer());

		// Equals
		const uint8 NamedPayload[] = { 1, 'A', 5, 3, IntType, 1, 2, 3 };
		FCbField NamedField(NamedPayload, ECbFieldType::UniformArray | ECbFieldType::HasFieldName);
		TestTrue(TEXT("FCbArray::Equals()"), Field.AsArray().Equals(NamedField.AsArray()));
		TestTrue(TEXT("FCbArray::AsField().Equals()"), Field.Equals(Field.AsArray().AsField()));
		TestTrue(TEXT("FCbArray::AsField().Equals()"), NamedField.Equals(NamedField.AsArray().AsField()));
		TestEqual(TEXT("FCbArray(Name)::GetView()"), NamedField.AsArray().GetView(), NamedField.GetView());

		// CopyTo
		uint8 CopyBytes[sizeof(Payload) + 1];
		Field.AsArray().CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbArray(NoType)::CopyTo()"), MakeMemoryView(Payload).EqualBytes(MakeMemoryView(CopyBytes) + 1));
		NamedField.AsArray().CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbArray(NoType, Name)::CopyTo()"), MakeMemoryView(Payload).EqualBytes(MakeMemoryView(CopyBytes) + 1));

		// GetSerializedView
		FMemoryView SerializedView;
		TestTrue(TEXT("FCbArray(Clone)::TryGetSerializedView()"), Array.TryGetSerializedView(SerializedView) && SerializedView == Array.AsField().GetView());
		TestFalse(TEXT("FCbArray(NoType)::TryGetSerializedView()"), Field.AsArray().TryGetSerializedView(SerializedView));
		TestFalse(TEXT("FCbArray(Name)::TryGetSerializedView()"), NamedField.AsArray().TryGetSerializedView(SerializedView));
	}

	// Test FCbField(None) as Array
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Array>(TEXT("Array, None"), Field, ECbFieldError::TypeError);
		FCbFieldRef::MakeView(Field).AsArrayRef();
	}

	// Test FCbArray(ArrayWithName) and CreateRefIterator
	{
		const uint8 ArrayType = uint8(ECbFieldType::Array | ECbFieldType::HasFieldName);
		const uint8 Buffer[] = { ArrayType, 3, 'K', 'e', 'y', 3, 1, uint8(ECbFieldType::IntegerPositive), 8 };
		const FCbArray Array(Buffer);
		TestEqual(TEXT("FCbArray(ArrayWithName)::GetSize()"), Array.GetSize(), uint64(5));
		const FCbArrayRef ArrayClone = FCbArrayRef::Clone(Array);
		TestEqual(TEXT("FCbArrayRef(ArrayWithName)::GetSize()"), ArrayClone.GetSize(), uint64(5));
		TestTrue(TEXT("FCbArray::Equals()"), Array.Equals(ArrayClone));
		TestEqual(TEXT("FCbArray::GetHash()"), ArrayClone.GetHash(), Array.GetHash());
		for (FCbFieldRefIterator It = ArrayClone.CreateRefIterator(); It; ++It)
		{
			FCbFieldRef Field = *It;
			TestEqual(TEXT("FCbArrayRef::CreateRefIterator().AsInt32()"), Field.AsInt32(), 8);
			TestTrue(TEXT("FCbArrayRef::CreateRefIterator().IsOwned()"), Field.IsOwned());
			TestEqual(TEXT("FCbArrayRef::CreateRefIterator().GetBuffer()"), Field.GetBuffer().GetView(), Field.GetView());
		}
		for (FCbFieldRefIterator It = ArrayClone.CreateRefIterator(), End; It != End; ++It)
		{
		}

		// CopyTo
		uint8 CopyBytes[5];
		Array.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbArray(Name)::CopyTo()"), ArrayClone.GetView().EqualBytes(MakeMemoryView(CopyBytes)));
		ArrayClone.CopyTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbArray()::CopyTo()"), ArrayClone.GetView().EqualBytes(MakeMemoryView(CopyBytes)));
	}

	// Test FCbArray as FCbFieldIterator
	{
		uint32 Count = 0;
		FCbArray Array;
		for (FCbField Field : FCbFieldIterator::MakeSingle(Array.AsField()))
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
		for (FCbFieldRef Field : FCbFieldRefIterator::MakeSingle(Array.AsFieldRef()))
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
	TestField<ECbFieldType::Binary>(TEXT("Binary, Empty"), {0});

	// Test FCbField(Binary, Value)
	{
		const uint8 Payload[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FCbField Field(Payload, ECbFieldType::Binary);
		TestFieldNoClone<ECbFieldType::Binary>(TEXT("Binary, Value"), Field, MakeMemoryView(Payload + 1, 3));
	}

	// Test FCbField(None) as Binary
	{
		FCbField Field;
		const uint8 Default[] = { 1, 2, 3 };
		TestFieldError<ECbFieldType::Binary>(TEXT("Binary, None"), Field, ECbFieldError::TypeError, MakeMemoryView(Default));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldStringTest, FCbFieldTestBase, "System.Core.Serialization.CbField.String", CompactBinaryTestFlags)
bool FCbFieldStringTest::RunTest(const FString& Parameters)
{
	// Test FCbField(String, Empty)
	TestField<ECbFieldType::String>(TEXT("String, Empty"), {0});

	// Test FCbField(String, Value)
	{
		const uint8 Payload[] = { 3, 'A', 'B', 'C' }; // Size: 3, Data: ABC
		TestField<ECbFieldType::String>(TEXT("String, Value"), Payload, FAnsiStringView(reinterpret_cast<const ANSICHAR*>(Payload) + 1, 3));
	}

	// Test FCbField(String, OutOfRangeSize)
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 31, Payload);
		TestFieldError<ECbFieldType::String>(TEXT("String, OutOfRangeSize"), Payload, ECbFieldError::RangeError, "ABC"_ASV);
	}

	// Test FCbField(None) as String
	{
		FCbField Field;
		TestFieldError<ECbFieldType::String>(TEXT("String, None"), Field, ECbFieldError::TypeError, "ABC"_ASV);
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

	template <typename T, T (FCbField::*InAsTypeFn)(T)>
	struct TCbIntegerAccessors
	{
		static constexpr bool (FCbField::*IsType)() const = &FCbField::IsInteger;
		static constexpr T (FCbField::*AsType)(T) = InAsTypeFn;
	};

	void TestIntegerField(ECbFieldType FieldType, EIntType ExpectedMask, uint64 Magnitude)
	{
		uint8 Payload[9];
		const bool Negative = bool(uint8(FieldType) & 1);
		WriteVarUInt(Magnitude - Negative, Payload);
		constexpr uint64 DefaultValue = 8;
		const uint64 ExpectedValue = Negative ? uint64(-int64(Magnitude)) : Magnitude;
		FCbField Field(Payload, FieldType);
		TestField<ECbFieldType::IntegerNegative>(TEXT("Int8"), Field, int8(EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ExpectedValue : DefaultValue),
			int8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int8, &FCbField::AsInt8>());
		TestField<ECbFieldType::IntegerNegative>(TEXT("Int16"), Field, int16(EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ExpectedValue : DefaultValue),
			int16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int16, &FCbField::AsInt16>());
		TestField<ECbFieldType::IntegerNegative>(TEXT("Int32"), Field, int32(EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ExpectedValue : DefaultValue),
			int32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int32, &FCbField::AsInt32>());
		TestField<ECbFieldType::IntegerNegative>(TEXT("Int64"), Field, int64(EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ExpectedValue : DefaultValue),
			int64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int64, &FCbField::AsInt64>());
		TestField<ECbFieldType::IntegerPositive>(TEXT("UInt8"), Field, uint8(EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ExpectedValue : DefaultValue),
			uint8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint8, &FCbField::AsUInt8>());
		TestField<ECbFieldType::IntegerPositive>(TEXT("UInt16"), Field, uint16(EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ExpectedValue : DefaultValue),
			uint16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint16, &FCbField::AsUInt16>());
		TestField<ECbFieldType::IntegerPositive>(TEXT("UInt32"), Field, uint32(EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ExpectedValue : DefaultValue),
			uint32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint32, &FCbField::AsUInt32>());
		TestField<ECbFieldType::IntegerPositive>(TEXT("UInt64"), Field, uint64(EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ExpectedValue : DefaultValue),
			uint64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint64, &FCbField::AsUInt64>());
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
		TestFieldError<ECbFieldType::IntegerPositive>(TEXT("Integer+, None"), Field, ECbFieldError::TypeError, uint64(8));
		TestFieldError<ECbFieldType::IntegerNegative>(TEXT("Integer-, None"), Field, ECbFieldError::TypeError, int64(8));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldFloatTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Float", CompactBinaryTestFlags)
bool FCbFieldFloatTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Float, 32-bit)
	{
		const uint8 Payload[] = { 0xc0, 0x12, 0x34, 0x56 }; // -2.28444433f
		TestField<ECbFieldType::Float32>(TEXT("Float32"), Payload, -2.28444433f);

		FCbField Field(Payload, ECbFieldType::Float32);
		TestField<ECbFieldType::Float64>(TEXT("Float32, AsDouble"), Field, -2.28444433);
	}

	// Test FCbField(Float, 64-bit)
	{
		const uint8 Payload[] = { 0xc1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }; // -631475.76888888876
		TestField<ECbFieldType::Float64>(TEXT("Float64"), Payload, -631475.76888888876);

		FCbField Field(Payload, ECbFieldType::Float64);
		TestFieldError<ECbFieldType::Float32>(TEXT("Float64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
	}

	// Test FCbField(Integer+, MaxBinary32) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 1, Payload); // 16,777,215
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestField<ECbFieldType::Float32>(TEXT("Integer+, MaxBinary32, AsFloat"), Field, 16'777'215.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer+, MaxBinary32, AsDouble"), Field, 16'777'215.0);
	}

	// Test FCbField(Integer+, MaxBinary32+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 24, Payload); // 16,777,216
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer+, MaxBinary32+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer+, MaxBinary32+1, AsDouble"), Field, 16'777'216.0);
	}

	// Test FCbField(Integer+, MaxBinary64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 1, Payload); // 9,007,199,254,740,991
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer+, MaxBinary64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer+, MaxBinary64, AsDouble"), Field, 9'007'199'254'740'991.0);
	}

	// Test FCbField(Integer+, MaxBinary64+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(1) << 53, Payload); // 9,007,199,254,740,992
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer+, MaxBinary64+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(TEXT("Integer+, MaxBinary64+1, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(Integer+, MaxUInt64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt(uint64(-1), Payload); // Max uint64
		FCbField Field(Payload, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer+, MaxUInt64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(TEXT("Integer+, MaxUInt64, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(Integer-, MaxBinary32) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 2, Payload); // -16,777,215
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestField<ECbFieldType::Float32>(TEXT("Integer-, MaxBinary32, AsFloat"), Field, -16'777'215.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer-, MaxBinary32, AsDouble"), Field, -16'777'215.0);
	}

	// Test FCbField(Integer-, MaxBinary32+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 24) - 1, Payload); // -16,777,216
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer-, MaxBinary32+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer-, MaxBinary32+1, AsDouble"), Field, -16'777'216.0);
	}

	// Test FCbField(Integer-, MaxBinary64) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 2, Payload); // -9,007,199,254,740,991
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer-, MaxBinary64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(TEXT("Integer-, MaxBinary64, AsDouble"), Field, -9'007'199'254'740'991.0);
	}

	// Test FCbField(Integer-, MaxBinary64+1) as Float
	{
		uint8 Payload[9];
		WriteVarUInt((uint64(1) << 53) - 1, Payload); // -9,007,199,254,740,992
		FCbField Field(Payload, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(TEXT("Integer-, MaxBinary64+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(TEXT("Integer-, MaxBinary64+1, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	// Test FCbField(None) as Float
	{
		FCbField Field;
		TestFieldError<ECbFieldType::Float32>(TEXT("None, AsFloat"), Field, ECbFieldError::TypeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(TEXT("None, AsDouble"), Field, ECbFieldError::TypeError, 8.0);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldBoolTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Bool", CompactBinaryTestFlags)
bool FCbFieldBoolTest::RunTest(const FString& Parameters)
{
	// Test FCbField(Bool, False)
	TestField<ECbFieldType::BoolFalse>(TEXT("Bool, False"), {}, false, true);

	// Test FCbField(Bool, True)
	TestField<ECbFieldType::BoolTrue>(TEXT("Bool, True"), {}, true, false);

	// Test FCbField(None) as Bool
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::BoolFalse>(TEXT("Bool, False, None"), DefaultField, ECbFieldError::TypeError, false);
		TestFieldError<ECbFieldType::BoolTrue>(TEXT("Bool, True, None"), DefaultField, ECbFieldError::TypeError, true);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldCompactBinaryAttachmentTest, FCbFieldTestBase, "System.Core.Serialization.CbField.CompactBinaryAttachment", CompactBinaryTestFlags)
bool FCbFieldCompactBinaryAttachmentTest::RunTest(const FString& Parameters)
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	// Test FCbField(CompactBinaryAttachment, Zero)
	TestField<ECbFieldType::CompactBinaryAttachment>(TEXT("CompactBinaryAttachment, Zero"), ZeroBytes);

	// Test FCbField(CompactBinaryAttachment, NonZero)
	TestField<ECbFieldType::CompactBinaryAttachment>(TEXT("CompactBinaryAttachment, NonZero"), SequentialBytes, FIoHash(SequentialBytes));

	// Test FCbField(CompactBinaryAttachment, NonZero) AsAttachment
	{
		FCbField Field(SequentialBytes, ECbFieldType::CompactBinaryAttachment);
		TestField<ECbFieldType::CompactBinaryAttachment>(TEXT("CompactBinaryAttachment, NonZero, AsAttachment"), Field, FIoHash(SequentialBytes), FIoHash(), ECbFieldError::None, FCbAttachmentAccessors());
	}

	// Test FCbField(None) as CompactBinaryAttachment
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::CompactBinaryAttachment>(TEXT("CompactBinaryAttachment, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldBinaryAttachmentTest, FCbFieldTestBase, "System.Core.Serialization.CbField.BinaryAttachment", CompactBinaryTestFlags)
bool FCbFieldBinaryAttachmentTest::RunTest(const FString& Parameters)
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	// Test FCbField(BinaryAttachment, Zero)
	TestField<ECbFieldType::BinaryAttachment>(TEXT("BinaryAttachment, Zero"), ZeroBytes);

	// Test FCbField(BinaryAttachment, NonZero)
	TestField<ECbFieldType::BinaryAttachment>(TEXT("BinaryAttachment, NonZero"), SequentialBytes, FIoHash(SequentialBytes));

	// Test FCbField(BinaryAttachment, NonZero) AsAttachment
	{
		FCbField Field(SequentialBytes, ECbFieldType::BinaryAttachment);
		TestField<ECbFieldType::BinaryAttachment>(TEXT("BinaryAttachment, NonZero, AsAttachment"), Field, FIoHash(SequentialBytes), FIoHash(), ECbFieldError::None, FCbAttachmentAccessors());
	}

	// Test FCbField(None) as BinaryAttachment
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::BinaryAttachment>(TEXT("BinaryAttachment, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldHashTest, FCbFieldTestBase, "System.Core.Serialization.CbField.Hash", CompactBinaryTestFlags)
bool FCbFieldHashTest::RunTest(const FString& Parameters)
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	// Test FCbField(Hash, Zero)
	TestField<ECbFieldType::Hash>(TEXT("Hash, Zero"), ZeroBytes);

	// Test FCbField(Hash, NonZero)
	TestField<ECbFieldType::Hash>(TEXT("Hash, NonZero"), SequentialBytes, FIoHash(SequentialBytes));

	// Test FCbField(None) as Hash
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::Hash>(TEXT("Hash, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
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
	TestField<ECbFieldType::Uuid>(TEXT("Uuid, Zero"), ZeroBytes, FGuid(), SequentialGuid);

	// Test FCbField(Uuid, NonZero)
	TestField<ECbFieldType::Uuid>(TEXT("Uuid, NonZero"), SequentialBytes, SequentialGuid, FGuid());

	// Test FCbField(None) as Uuid
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::Uuid>(TEXT("Uuid, None"), DefaultField, ECbFieldError::TypeError, FGuid::NewGuid());
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldDateTimeTest, FCbFieldTestBase, "System.Core.Serialization.CbField.DateTime", CompactBinaryTestFlags)
bool FCbFieldDateTimeTest::RunTest(const FString& Parameters)
{
	// Test FCbField(DateTime, Zero)
	TestField<ECbFieldType::DateTime>(TEXT("DateTime, Zero"), {0, 0, 0, 0, 0, 0, 0, 0});

	// Test FCbField(DateTime, 0x1020'3040'5060'7080)
	TestField<ECbFieldType::DateTime>(TEXT("DateTime, NonZero"), {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, int64(0x1020'3040'5060'7080));

	// Test FCbField(DateTime, Zero) as FDateTime
	{
		const uint8 Payload[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbField Field(Payload, ECbFieldType::DateTime);
		TestEqual(TEXT("FCbField()::AsDateTime()"), Field.AsDateTime(), FDateTime(0));
	}

	// Test FCbField(None) as DateTime
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::DateTime>(TEXT("DateTime, None"), DefaultField, ECbFieldError::TypeError);
		const FDateTime DefaultValue(0x1020'3040'5060'7080);
		TestEqual(TEXT("FCbField()::AsDateTime()"), DefaultField.AsDateTime(DefaultValue), DefaultValue);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldTimeSpanTest, FCbFieldTestBase, "System.Core.Serialization.CbField.TimeSpan", CompactBinaryTestFlags)
bool FCbFieldTimeSpanTest::RunTest(const FString& Parameters)
{
	// Test FCbField(TimeSpan, Zero)
	TestField<ECbFieldType::TimeSpan>(TEXT("TimeSpan, Zero"), {0, 0, 0, 0, 0, 0, 0, 0});

	// Test FCbField(TimeSpan, 0x1020'3040'5060'7080)
	TestField<ECbFieldType::TimeSpan>(TEXT("TimeSpan, NonZero"), {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, int64(0x1020'3040'5060'7080));

	// Test FCbField(TimeSpan, Zero) as FTimeSpan
	{
		const uint8 Payload[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbField Field(Payload, ECbFieldType::TimeSpan);
		TestEqual(TEXT("FCbField()::AsTimeSpan()"), Field.AsTimeSpan(), FTimespan(0));
	}

	// Test FCbField(None) as TimeSpan
	{
		FCbField DefaultField;
		TestFieldError<ECbFieldType::TimeSpan>(TEXT("TimeSpan, None"), DefaultField, ECbFieldError::TypeError);
		const FTimespan DefaultValue(0x1020'3040'5060'7080);
		TestEqual(TEXT("FCbField()::AsTimeSpan()"), DefaultField.AsTimeSpan(DefaultValue), DefaultValue);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldIterateAttachmentsTest, FCbFieldTestBase, "System.Core.Serialization.CbField.IterateAttachments", CompactBinaryTestFlags)
bool FCbFieldIterateAttachmentsTest::RunTest(const FString& Parameters)
{
	const auto MakeTestHash = [](uint32 Index) { return FIoHash::HashBuffer(&Index, sizeof(Index)); };

	FCbFieldRefIterator Fields;
	{
		FCbWriter Writer;

		Writer.SetName("IgnoredTypeInRoot").AddHash(MakeTestHash(100));
		Writer.AddCompactBinaryAttachment(MakeTestHash(0));
		Writer.AddBinaryAttachment(MakeTestHash(1));
		Writer.SetName("CbAttachmentInRoot").AddCompactBinaryAttachment(MakeTestHash(2));
		Writer.SetName("BinAttachmentInRoot").AddBinaryAttachment(MakeTestHash(3));

		// Uniform array of type to ignore.
		Writer.BeginArray();
		{
			Writer << 1;
			Writer << 2;
		}
		Writer.EndArray();
		// Uniform array of binary attachments.
		Writer.BeginArray();
		{
			Writer.AddBinaryAttachment(MakeTestHash(4));
			Writer.AddBinaryAttachment(MakeTestHash(5));
		}
		Writer.EndArray();
		// Uniform array of uniform arrays.
		Writer.BeginArray();
		{
			Writer.BeginArray();
			Writer.AddBinaryAttachment(MakeTestHash(6));
			Writer.AddBinaryAttachment(MakeTestHash(7));
			Writer.EndArray();
			Writer.BeginArray();
			Writer.AddBinaryAttachment(MakeTestHash(8));
			Writer.AddBinaryAttachment(MakeTestHash(9));
			Writer.EndArray();
		}
		Writer.EndArray();
		// Uniform array of non-uniform arrays.
		Writer.BeginArray();
		{
			Writer.BeginArray();
			Writer << 0;
			Writer << false;
			Writer.EndArray();
			Writer.BeginArray();
			Writer.AddCompactBinaryAttachment(MakeTestHash(10));
			Writer << false;
			Writer.EndArray();
		}
		Writer.EndArray();
		// Uniform array of uniform objects.
		Writer.BeginArray();
		{
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInUniObjInUniObj1").AddCompactBinaryAttachment(MakeTestHash(11));
			Writer.SetName("CbAttachmentInUniObjInUniObj2").AddCompactBinaryAttachment(MakeTestHash(12));
			Writer.EndObject();
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInUniObjInUniObj3").AddCompactBinaryAttachment(MakeTestHash(13));
			Writer.SetName("CbAttachmentInUniObjInUniObj4").AddCompactBinaryAttachment(MakeTestHash(14));
			Writer.EndObject();
		}
		Writer.EndArray();
		// Uniform array of non-uniform objects.
		Writer.BeginArray();
		{
			Writer.BeginObject();
			Writer << "Int" << 0;
			Writer << "Bool" << false;
			Writer.EndObject();
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInNonUniObjInUniObj").AddCompactBinaryAttachment(MakeTestHash(15));
			Writer << "Bool" << false;
			Writer.EndObject();
		}
		Writer.EndArray();

		// Uniform object of type to ignore.
		Writer.BeginObject();
		{
			Writer << "Int1" << 1;
			Writer << "Int2" << 2;
		}
		Writer.EndObject();
		// Uniform object of binary attachments.
		Writer.BeginObject();
		{
			Writer.SetName("BinAttachmentInUniObj1").AddBinaryAttachment(MakeTestHash(16));
			Writer.SetName("BinAttachmentInUniObj2").AddBinaryAttachment(MakeTestHash(17));
		}
		Writer.EndObject();
		// Uniform object of uniform arrays.
		Writer.BeginObject();
		{
			Writer.SetName("Array1");
			Writer.BeginArray();
			Writer.AddBinaryAttachment(MakeTestHash(18));
			Writer.AddBinaryAttachment(MakeTestHash(19));
			Writer.EndArray();
			Writer.SetName("Array2");
			Writer.BeginArray();
			Writer.AddBinaryAttachment(MakeTestHash(20));
			Writer.AddBinaryAttachment(MakeTestHash(21));
			Writer.EndArray();
		}
		Writer.EndObject();
		// Uniform object of non-uniform arrays.
		Writer.BeginObject();
		{
			Writer.SetName("Array1");
			Writer.BeginArray();
			Writer << 0;
			Writer << false;
			Writer.EndArray();
			Writer.SetName("Array2");
			Writer.BeginArray();
			Writer.AddCompactBinaryAttachment(MakeTestHash(22));
			Writer << false;
			Writer.EndArray();
		}
		Writer.EndObject();
		// Uniform object of uniform objects.
		Writer.BeginObject();
		{
			Writer.SetName("Object1");
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInUniObjInUniObj1").AddCompactBinaryAttachment(MakeTestHash(23));
			Writer.SetName("CbAttachmentInUniObjInUniObj2").AddCompactBinaryAttachment(MakeTestHash(24));
			Writer.EndObject();
			Writer.SetName("Object2");
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInUniObjInUniObj3").AddCompactBinaryAttachment(MakeTestHash(25));
			Writer.SetName("CbAttachmentInUniObjInUniObj4").AddCompactBinaryAttachment(MakeTestHash(26));
			Writer.EndObject();
		}
		Writer.EndObject();
		// Uniform object of non-uniform objects.
		Writer.BeginObject();
		{
			Writer.SetName("Object1");
			Writer.BeginObject();
			Writer << "Int" << 0;
			Writer << "Bool" << false;
			Writer.EndObject();
			Writer.SetName("Object2");
			Writer.BeginObject();
			Writer.SetName("CbAttachmentInNonUniObjInUniObj").AddCompactBinaryAttachment(MakeTestHash(27));
			Writer << "Bool" << false;
			Writer.EndObject();
		}
		Writer.EndObject();

		Fields = Writer.Save();
	}

	TestEqual(TEXT("FCbField::IterateAttachments Validate"), ValidateCompactBinaryRange(Fields.GetBuffer(), ECbValidateMode::All), ECbValidateError::None);

	uint32 AttachmentIndex = 0;
	Fields.IterateRangeAttachments([this, &AttachmentIndex, &MakeTestHash](FCbField Field)
		{
			TestTrue(FString::Printf(TEXT("FCbField::IterateAttachments(%u)::IsAttachment"), AttachmentIndex), Field.IsAttachment());
			TestEqual(FString::Printf(TEXT("FCbField::IterateAttachments(%u)"), AttachmentIndex), Field.AsAttachment(), MakeTestHash(AttachmentIndex));
			++AttachmentIndex;
		});
	TestEqual(TEXT("FCbField::IterateAttachments"), AttachmentIndex, 28);

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCbFieldRefTest, FCbFieldTestBase, "System.Core.Serialization.CbFieldRef", CompactBinaryTestFlags)
bool FCbFieldRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbFieldRef, const FSharedBuffer&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, FSharedBuffer&&>::value, "Missing constructor for FCbFieldRef");

	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FSharedBuffer&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbFieldRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbArrayRef&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, const FCbObjectRef&>::value, "Missing constructor for FCbFieldRef");

	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FSharedBuffer&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbFieldRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbArrayRef&&>::value, "Missing constructor for FCbFieldRef");
	static_assert(std::is_constructible<FCbFieldRef, const FCbField&, FCbObjectRef&&>::value, "Missing constructor for FCbFieldRef");

	// Test FCbFieldRef()
	{
		FCbFieldRef DefaultField;
		TestFalse(TEXT("FCbFieldRef().HasValue()"), DefaultField.HasValue());
		TestFalse(TEXT("FCbFieldRef().IsOwned()"), DefaultField.IsOwned());
		DefaultField.MakeOwned();
		TestTrue(TEXT("FCbFieldRef().MakeOwned().IsOwned()"), DefaultField.IsOwned());
	}

	// Test Field w/ Type from Shared Buffer
	{
		const uint8 Payload[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FSharedBuffer ViewBuffer = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		FSharedBuffer OwnedBuffer = FSharedBuffer::Clone(ViewBuffer);

		FCbFieldRef View(ViewBuffer);
		FCbFieldRef ViewMove{FSharedBuffer(ViewBuffer)};
		FCbFieldRef ViewOuterField(ImplicitConv<FCbField>(View), ViewBuffer);
		FCbFieldRef ViewOuterBuffer(ImplicitConv<FCbField>(View), View);
		FCbFieldRef Owned(OwnedBuffer);
		FCbFieldRef OwnedMove{FSharedBuffer(OwnedBuffer)};
		FCbFieldRef OwnedOuterField(ImplicitConv<FCbField>(Owned), OwnedBuffer);
		FCbFieldRef OwnedOuterBuffer(ImplicitConv<FCbField>(Owned), Owned);

		// These lines are expected to assert when uncommented.
		//FCbFieldRef InvalidOuterBuffer(ImplicitConv<FCbField>(Owned), ViewBuffer);
		//FCbFieldRef InvalidOuterBufferMove(ImplicitConv<FCbField>(Owned), FSharedBuffer(ViewBuffer));

		TestEqual(TEXT("FCbFieldRef(ViewBuffer)"), View.AsBinary(), ViewBuffer.GetView().Right(3));
		TestEqual(TEXT("FCbFieldRef(ViewBuffer&&)"), ViewMove.AsBinary(), View.AsBinary());
		TestEqual(TEXT("FCbFieldRef(ViewOuterField)"), ViewOuterField.AsBinary(), View.AsBinary());
		TestEqual(TEXT("FCbFieldRef(ViewOuterBuffer)"), ViewOuterBuffer.AsBinary(), View.AsBinary());
		TestEqual(TEXT("FCbFieldRef(OwnedBuffer)"), Owned.AsBinary(), OwnedBuffer.GetView().Right(3));
		TestEqual(TEXT("FCbFieldRef(OwnedBuffer&&)"), OwnedMove.AsBinary(), Owned.AsBinary());
		TestEqual(TEXT("FCbFieldRef(OwnedOuterField)"), OwnedOuterField.AsBinary(), Owned.AsBinary());
		TestEqual(TEXT("FCbFieldRef(OwnedOuterBuffer)"), OwnedOuterBuffer.AsBinary(), Owned.AsBinary());

		TestFalse(TEXT("FCbFieldRef(ViewBuffer).IsOwned()"), View.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewBuffer&&).IsOwned()"), ViewMove.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewOuterField).IsOwned()"), ViewOuterField.IsOwned());
		TestFalse(TEXT("FCbFieldRef(ViewOuterBuffer).IsOwned()"), ViewOuterBuffer.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedBuffer).IsOwned()"), Owned.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedBuffer&&).IsOwned()"), OwnedMove.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedOuterField).IsOwned()"), OwnedOuterField.IsOwned());
		TestTrue(TEXT("FCbFieldRef(OwnedOuterBuffer).IsOwned()"), OwnedOuterBuffer.IsOwned());

		View.MakeOwned();
		Owned.MakeOwned();
		TestNotEqual(TEXT("FCbFieldRef(View).MakeOwned()"), View.AsBinary(), ViewBuffer.GetView().Right(3));
		TestTrue(TEXT("FCbFieldRef(View).MakeOwned().IsOwned()"), View.IsOwned());
		TestEqual(TEXT("FCbFieldRef(Owned).MakeOwned()"), Owned.AsBinary(), OwnedBuffer.GetView().Right(3));
		TestTrue(TEXT("FCbFieldRef(Owned).MakeOwned().IsOwned()"), Owned.IsOwned());
	}

	// Test Field w/ Type
	{
		const uint8 Payload[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		const FCbField Field(Payload);

		FCbFieldRef VoidView = FCbFieldRef::MakeView(Payload);
		FCbFieldRef VoidClone = FCbFieldRef::Clone(Payload);
		FCbFieldRef FieldView = FCbFieldRef::MakeView(Field);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		FCbFieldRef FieldRefClone = FCbFieldRef::Clone(FieldView);

		TestEqual(TEXT("FCbFieldRef::MakeView(Void)"), VoidView.AsBinary(), MakeMemoryView(Payload).Right(3));
		TestNotEqual(TEXT("FCbFieldRef::Clone(Void)"), VoidClone.AsBinary(), MakeMemoryView(Payload).Right(3));
		TestTrue(TEXT("FCbFieldRef::Clone(Void)->EqualBytes"), VoidClone.AsBinary().EqualBytes(VoidView.AsBinary()));
		TestEqual(TEXT("FCbFieldRef::MakeView(Field)"), FieldView.AsBinary(), MakeMemoryView(Payload).Right(3));
		TestNotEqual(TEXT("FCbFieldRef::Clone(Field)"), FieldClone.AsBinary(), MakeMemoryView(Payload).Right(3));
		TestTrue(TEXT("FCbFieldRef::Clone(Field)->EqualBytes"), FieldClone.AsBinary().EqualBytes(VoidView.AsBinary()));
		TestNotEqual(TEXT("FCbFieldRef::Clone(FieldRef)"), FieldRefClone.AsBinary(), FieldView.AsBinary());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef)->EqualBytes"), FieldRefClone.AsBinary().EqualBytes(VoidView.AsBinary()));

		TestFalse(TEXT("FCbFieldRef::MakeView(Void).IsOwned()"), VoidView.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Void).IsOwned()"), VoidClone.IsOwned());
		TestFalse(TEXT("FCbFieldRef::MakeView(Field).IsOwned()"), FieldView.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Field).IsOwned()"), FieldClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef).IsOwned()"), FieldRefClone.IsOwned());
	}

	// Test Field w/o Type
	{
		const uint8 Payload[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		const FCbField Field(Payload, ECbFieldType::Binary);

		FCbFieldRef FieldView = FCbFieldRef::MakeView(Field);
		FCbFieldRef FieldClone = FCbFieldRef::Clone(Field);
		FCbFieldRef FieldRefClone = FCbFieldRef::Clone(FieldView);

		TestEqual(TEXT("FCbFieldRef::MakeView(Field, NoType)"), FieldView.AsBinary(), MakeMemoryView(Payload).Right(3));
		TestTrue(TEXT("FCbFieldRef::Clone(Field, NoType)"), FieldClone.AsBinary().EqualBytes(FieldView.AsBinary()));
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef, NoType)"), FieldRefClone.AsBinary().EqualBytes(FieldView.AsBinary()));

		TestFalse(TEXT("FCbFieldRef::MakeView(Field, NoType).IsOwned()"), FieldView.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(Field, NoType).IsOwned()"), FieldClone.IsOwned());
		TestTrue(TEXT("FCbFieldRef::Clone(FieldRef, NoType).IsOwned()"), FieldRefClone.IsOwned());

		FieldView.MakeOwned();
		TestTrue(TEXT("FCbFieldRef::MakeView(NoType).MakeOwned()"), FieldView.AsBinary().EqualBytes(MakeMemoryView(Payload).Right(3)));
		TestTrue(TEXT("FCbFieldRef::MakeView(NoType).MakeOwned().IsOwned()"), FieldView.IsOwned());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbArrayRefTest, "System.Core.Serialization.CbArrayRef", CompactBinaryTestFlags)
bool FCbArrayRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FSharedBuffer&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbFieldRef&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbArrayRef&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, const FCbObjectRef&>::value, "Missing constructor for FCbArrayRef");

	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FSharedBuffer&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbFieldRef&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbArrayRef&&>::value, "Missing constructor for FCbArrayRef");
	static_assert(std::is_constructible<FCbArrayRef, const FCbArray&, FCbObjectRef&&>::value, "Missing constructor for FCbArrayRef");

	// Test FCbArrayRef()
	{
		FCbArrayRef DefaultArray;
		TestFalse(TEXT("FCbArrayRef().IsOwned()"), DefaultArray.IsOwned());
		DefaultArray.MakeOwned();
		TestTrue(TEXT("FCbArrayRef().MakeOwned().IsOwned()"), DefaultArray.IsOwned());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbObjectRefTest, "System.Core.Serialization.CbObjectRef", CompactBinaryTestFlags)
bool FCbObjectRefTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FSharedBuffer&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbFieldRefIterator&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbFieldRef&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbArrayRef&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, const FCbObjectRef&>::value, "Missing constructor for FCbObjectRef");

	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FSharedBuffer&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbFieldRefIterator&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbFieldRef&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbArrayRef&&>::value, "Missing constructor for FCbObjectRef");
	static_assert(std::is_constructible<FCbObjectRef, const FCbObject&, FCbObjectRef&&>::value, "Missing constructor for FCbObjectRef");

	// Test FCbObjectRef()
	{
		FCbObjectRef DefaultObject;
		TestFalse(TEXT("FCbObjectRef().IsOwned()"), DefaultObject.IsOwned());
		DefaultObject.MakeOwned();
		TestTrue(TEXT("FCbObjectRef().MakeOwned().IsOwned()"), DefaultObject.IsOwned());
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCbFieldRefIteratorTest, "System.Core.Serialization.CbFieldRefIterator", CompactBinaryTestFlags)
bool FCbFieldRefIteratorTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_constructible<FCbFieldIterator, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldIterator");

	static_assert(std::is_constructible<FCbFieldRefIterator, const FCbFieldRefIterator&>::value, "Missing constructor for FCbFieldRefIterator");
	static_assert(std::is_constructible<FCbFieldRefIterator, FCbFieldRefIterator&&>::value, "Missing constructor for FCbFieldRefIterator");

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

		const FSharedBuffer View = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		const FSharedBuffer Clone = FSharedBuffer::Clone(View);

		const FMemoryView EmptyView;
		const FSharedBuffer NullBuffer;

		const FCbFieldIterator FieldIt = FCbFieldIterator::MakeRange(View);
		const FCbFieldRefIterator FieldRefIt = FCbFieldRefIterator::MakeRange(View);

		TestEqual(TEXT("FCbFieldIterator::GetRangeHash()"), FieldIt.GetRangeHash(), FIoHash::HashBuffer(View));
		TestEqual(TEXT("FCbFieldRefIterator::GetRangeHash()"), FieldRefIt.GetRangeHash(), FIoHash::HashBuffer(View));

		TestEqual(TEXT("FCbFieldIterator::GetRangeView()"), FieldIt.GetRangeView(), MakeMemoryView(Payload));
		TestEqual(TEXT("FCbFieldRefIterator::GetRangeView()"), FieldRefIt.GetRangeView(), MakeMemoryView(Payload));
		FMemoryView SerializedView;
		TestTrue(TEXT("FCbFieldIterator::TryGetSerializedRangeView()"), FieldIt.TryGetSerializedRangeView(SerializedView) && SerializedView == MakeMemoryView(Payload));
		TestTrue(TEXT("FCbFieldRefIterator::TryGetSerializedRangeView()"), FieldRefIt.TryGetSerializedRangeView(SerializedView) && SerializedView == MakeMemoryView(Payload));

		TestEqual(TEXT("FCbFieldRefIterator::CloneRange(EmptyIt)"), GetCount(FCbFieldRefIterator::CloneRange(FCbFieldIterator())), 0);
		TestEqual(TEXT("FCbFieldRefIterator::CloneRange(EmptyRefIt)"), GetCount(FCbFieldRefIterator::CloneRange(FCbFieldRefIterator())), 0);
		const FCbFieldRefIterator FieldItClone = FCbFieldRefIterator::CloneRange(FieldIt);
		const FCbFieldRefIterator FieldRefItClone = FCbFieldRefIterator::CloneRange(FieldRefIt);
		TestEqual(TEXT("FCbFieldRefIterator::CloneRange(FieldIt)"), GetCount(FieldItClone), 4);
		TestEqual(TEXT("FCbFieldRefIterator::CloneRange(FieldRefIt)"), GetCount(FieldRefItClone), 4);
		TestNotEqual(TEXT("FCbFieldRefIterator::CloneRange(FieldIt).Equals()"), FieldItClone, FieldRefIt);
		TestNotEqual(TEXT("FCbFieldRefIterator::CloneRange(FieldRefIt).Equals()"), FieldRefItClone, FieldRefIt);

		TestEqual(TEXT("FCbFieldIterator::MakeRange(EmptyView)"), GetCount(FCbFieldIterator::MakeRange(EmptyView)), 0);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(BufferNullL)"), GetCount(FCbFieldRefIterator::MakeRange(NullBuffer)), 0);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(BufferNullR)"), GetCount(FCbFieldRefIterator::MakeRange(FSharedBuffer(NullBuffer))), 0);

		TestEqual(TEXT("FCbFieldIterator::MakeRange(BufferView)"), GetCount(FCbFieldIterator::MakeRange(MakeMemoryView(Payload))), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(BufferCloneL)"), GetCount(FCbFieldRefIterator::MakeRange(Clone)), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(BufferCloneR)"), GetCount(FCbFieldRefIterator::MakeRange(FSharedBuffer(Clone))), 4);

		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferNullL)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(View), NullBuffer)), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferNullR)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(View), FSharedBuffer(NullBuffer))), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferViewL)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(View), View)), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferViewR)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(View), FSharedBuffer(View))), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferCloneL)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(Clone), Clone)), 4);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, BufferCloneR)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(Clone), FSharedBuffer(Clone))), 4);

		TestEqual(TEXT("FCbFieldIterator(FieldRefItL)"), GetCount(FCbFieldIterator(FieldRefIt)), 4);
		TestEqual(TEXT("FCbFieldIterator(FieldRefItR)"), GetCount(FCbFieldIterator(FCbFieldRefIterator(FieldRefIt))), 4);

		// Uniform
		const uint8 UniformPayload[] = { 0, 1, 2, 3 };
		const FCbFieldIterator UniformFieldIt = FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload), ECbFieldType::IntegerPositive);
		TestEqual(TEXT("FCbFieldIterator::MakeRange(Uniform).GetRangeHash()"), UniformFieldIt.GetRangeHash(), FieldIt.GetRangeHash());
		TestEqual(TEXT("FCbFieldIterator::MakeRange(Uniform).GetRangeView()"), UniformFieldIt.GetRangeView(), MakeMemoryView(UniformPayload));
		TestFalse(TEXT("FCbFieldIterator::MakeRange(Uniform).TryGetSerializedRangeView()"), UniformFieldIt.TryGetSerializedRangeView(SerializedView));
		const FSharedBuffer UniformView = FSharedBuffer::MakeView(MakeMemoryView(UniformPayload));
		const FCbFieldRefIterator UniformFieldRefIt = FCbFieldRefIterator::MakeRange(UniformView, ECbFieldType::IntegerPositive);
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(Uniform).GetRangeHash()"), UniformFieldRefIt.GetRangeHash(), FieldIt.GetRangeHash());
		TestEqual(TEXT("FCbFieldRefIterator::MakeRange(Uniform).GetRangeView()"), UniformFieldRefIt.GetRangeView(), MakeMemoryView(UniformPayload));
		TestFalse(TEXT("FCbFieldRefIterator::MakeRange(Uniform).TryGetSerializedRangeView()"), UniformFieldRefIt.TryGetSerializedRangeView(SerializedView));

		// Equals
		TestTrue(TEXT("FCbFieldIterator::Equals(Self)"), FieldIt.Equals(FieldIt));
		TestTrue(TEXT("FCbFieldIterator::Equals(OtherType)"), FieldIt.Equals(FieldRefIt));
		TestTrue(TEXT("FCbFieldRefIterator::Equals(Self)"), FieldRefIt.Equals(FieldRefIt));
		TestTrue(TEXT("FCbFieldRefIterator::Equals(OtherType)"), FieldRefIt.Equals(FieldIt));
		TestFalse(TEXT("FCbFieldIterator::Equals(OtherRange)"), FieldIt.Equals(FieldItClone));
		TestFalse(TEXT("FCbFieldRefIterator::Equals(OtherRange)"), FieldRefIt.Equals(FieldRefItClone));
		TestTrue(TEXT("FCbFieldIterator::Equals(Uniform, Self)"), UniformFieldIt.Equals(UniformFieldIt));
		TestTrue(TEXT("FCbFieldIterator::Equals(Uniform, OtherType)"), UniformFieldIt.Equals(UniformFieldRefIt));
		TestTrue(TEXT("FCbFieldRefIterator::Equals(Uniform, Self)"), UniformFieldRefIt.Equals(UniformFieldRefIt));
		TestTrue(TEXT("FCbFieldRefIterator::Equals(Uniform, OtherType)"), UniformFieldRefIt.Equals(UniformFieldIt));
		TestFalse(TEXT("FCbFieldIterator::Equals(SamePayload, DifferentEnd)"),
			FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload), ECbFieldType::IntegerPositive)
				.Equals(FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload).LeftChop(1), ECbFieldType::IntegerPositive)));
		TestFalse(TEXT("FCbFieldIterator::Equals(DifferentPayload, SameEnd)"),
			FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload), ECbFieldType::IntegerPositive)
				.Equals(FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload).RightChop(1), ECbFieldType::IntegerPositive)));

		// CopyRangeTo
		uint8 CopyBytes[sizeof(Payload)];
		FieldIt.CopyRangeTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbFieldIterator::MakeRange().CopyRangeTo()"), MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Payload)));
		FieldRefIt.CopyRangeTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbFieldRefIterator::MakeRange().CopyRangeTo()"), MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Payload)));
		UniformFieldIt.CopyRangeTo(MakeMemoryView(CopyBytes));
		TestTrue(TEXT("FCbFieldIterator::MakeRange(Uniform).CopyRangeTo()"), MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Payload)));

		// MakeRangeOwned
		FCbFieldRefIterator OwnedFromView = UniformFieldRefIt;
		OwnedFromView.MakeRangeOwned();
		TestTrue(TEXT("FCbFieldRefIterator::MakeRangeOwned(View)"), OwnedFromView.GetRangeView().EqualBytes(MakeMemoryView(Payload)));
		FCbFieldRefIterator OwnedFromOwned = OwnedFromView;
		OwnedFromOwned.MakeRangeOwned();
		TestEqual(TEXT("FCbFieldRefIterator::MakeRangeOwned(Owned)"), OwnedFromOwned, OwnedFromView);

		// GetRangeBuffer
		TestEqual(TEXT("FCbFieldRefIterator::GetRangeBuffer()"), UniformFieldRefIt.GetRangeBuffer(), UniformView);
		const FCbFieldRefIterator UniformShortFieldRefIt = FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(MakeMemoryView(UniformPayload).LeftChop(1), ECbFieldType::IntegerPositive), UniformView);
		TestNotEqual(TEXT("FCbFieldRefIterator::GetRangeBuffer(Short)"), UniformShortFieldRefIt.GetRangeBuffer(), UniformView);

		// These lines are expected to assert when uncommented.
		//const FSharedBuffer ShortView = FSharedBuffer::MakeView(MakeMemoryView(Payload).LeftChop(2));
		//TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, InvalidBufferL)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(*View), ShortView)), 4);
		//TestEqual(TEXT("FCbFieldRefIterator::MakeRangeView(FieldIt, InvalidBufferR)"), GetCount(FCbFieldRefIterator::MakeRangeView(FCbFieldIterator::MakeRange(*View), FSharedBuffer(ShortView))), 4);
	}

	// Test FCbField[Ref]Iterator(Scalar)
	{
		constexpr uint8 T = uint8(ECbFieldType::IntegerPositive);
		const uint8 Payload[] = { T, 0 };

		const FSharedBuffer View = FSharedBuffer::MakeView(MakeMemoryView(Payload));
		const FSharedBuffer Clone = FSharedBuffer::Clone(View);

		const FCbField Field(Payload);
		const FCbFieldRef FieldRef(View);

		TestEqual(TEXT("FCbFieldIterator::MakeSingle(FieldL)"), GetCount(FCbFieldIterator::MakeSingle(Field)), 1);
		TestEqual(TEXT("FCbFieldIterator::MakeSingle(FieldR)"), GetCount(FCbFieldIterator::MakeSingle(FCbField(Field))), 1);
		TestEqual(TEXT("FCbFieldRefIterator::MakeSingle(FieldRefL)"), GetCount(FCbFieldRefIterator::MakeSingle(FieldRef)), 1);
		TestEqual(TEXT("FCbFieldRefIterator::MakeSingle(FieldRefR)"), GetCount(FCbFieldRefIterator::MakeSingle(FCbFieldRef(FieldRef))), 1);
	}

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
