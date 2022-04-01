// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "IO/IoHash.h"
#include "Memory/CompositeBuffer.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/VarInt.h"
#include "TestHarness.h"

template <ECbFieldType FieldType>
struct TCbFieldTypeAccessors;

template <ECbFieldType FieldType>
using TCbFieldValueType = typename TCbFieldTypeAccessors<FCbFieldType::GetType(FieldType)>::ValueType;

#define UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(FieldType, InIsTypeFn, InAsTypeFn, InValueType)                           \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbFieldView::*IsType)() const = &FCbFieldView::InIsTypeFn;                                    \
		static auto AsType(FCbFieldView& Field, ValueType) { return Field.InAsTypeFn(); }                                 \
	};

#define UE_CBFIELD_TYPE_ACCESSOR_EX(FieldType, InIsTypeFn, InAsTypeFn, InValueType, InDefaultType)                    \
	template <>                                                                                                       \
	struct TCbFieldTypeAccessors<ECbFieldType::FieldType>                                                             \
	{                                                                                                                 \
		using ValueType = InValueType;                                                                                \
		static constexpr bool (FCbFieldView::*IsType)() const = &FCbFieldView::InIsTypeFn;                                    \
		static constexpr InValueType (FCbFieldView::*AsType)(InDefaultType) = &FCbFieldView::InAsTypeFn;                      \
	};

#define UE_CBFIELD_TYPE_ACCESSOR(FieldType, InIsTypeFn, InAsTypeFn, InValueType)                                      \
	UE_CBFIELD_TYPE_ACCESSOR_EX(FieldType, InIsTypeFn, InAsTypeFn, InValueType, InValueType)

UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(Object, IsObject, AsObjectView, FCbObjectView);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(UniformObject, IsObject, AsObjectView, FCbObjectView);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(Array, IsArray, AsArrayView, FCbArrayView);
UE_CBFIELD_TYPE_ACCESSOR_NO_DEFAULT(UniformArray, IsArray, AsArrayView, FCbArrayView);
UE_CBFIELD_TYPE_ACCESSOR(Binary, IsBinary, AsBinaryView, FMemoryView);
UE_CBFIELD_TYPE_ACCESSOR(String, IsString, AsString, FUtf8StringView);
UE_CBFIELD_TYPE_ACCESSOR(IntegerPositive, IsInteger, AsUInt64, uint64);
UE_CBFIELD_TYPE_ACCESSOR(IntegerNegative, IsInteger, AsInt64, int64);
UE_CBFIELD_TYPE_ACCESSOR(Float32, IsFloat, AsFloat, float);
UE_CBFIELD_TYPE_ACCESSOR(Float64, IsFloat, AsDouble, double);
UE_CBFIELD_TYPE_ACCESSOR(BoolFalse, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR(BoolTrue, IsBool, AsBool, bool);
UE_CBFIELD_TYPE_ACCESSOR_EX(ObjectAttachment, IsObjectAttachment, AsObjectAttachment, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(BinaryAttachment, IsBinaryAttachment, AsBinaryAttachment, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(Hash, IsHash, AsHash, FIoHash, const FIoHash&);
UE_CBFIELD_TYPE_ACCESSOR_EX(Uuid, IsUuid, AsUuid, FGuid, const FGuid&);
UE_CBFIELD_TYPE_ACCESSOR(DateTime, IsDateTime, AsDateTimeTicks, int64);
UE_CBFIELD_TYPE_ACCESSOR(TimeSpan, IsTimeSpan, AsTimeSpanTicks, int64);
UE_CBFIELD_TYPE_ACCESSOR_EX(ObjectId, IsObjectId, AsObjectId, FCbObjectId, const FCbObjectId&);
UE_CBFIELD_TYPE_ACCESSOR(CustomById, IsCustomById, AsCustomById, FCbCustomById);
UE_CBFIELD_TYPE_ACCESSOR(CustomByName, IsCustomByName, AsCustomByName, FCbCustomByName);

struct FCbAttachmentAccessors
{
	static constexpr bool (FCbFieldView::*IsType)() const = &FCbFieldView::IsAttachment;
	static constexpr FIoHash (FCbFieldView::*AsType)(const FIoHash&) = &FCbFieldView::AsAttachment;
};

void TestEqualCb(FAutomationTestFixture& Test, const FString& What, const FCbArrayView& Actual, const FCbArrayView& Expected)
{
	INFO(What);
	CHECK(Actual.Equals(Expected));
}

void TestEqualCb(FAutomationTestFixture& Test, const FString& What, const FCbObjectView& Actual, const FCbObjectView& Expected)
{
	INFO(What);
	CHECK(Actual.Equals(Expected));
}

void TestEqualCb(FAutomationTestFixture& Test, const FString& What, const FCbCustomById& Actual, const FCbCustomById& Expected)
{
	INFO(What);
	CHECK(Actual.Id == Expected.Id);
	CHECK(Actual.Data == Expected.Data);
}

void TestEqualCb(FAutomationTestFixture& Test, const FString& What, const FCbCustomByName& Actual, const FCbCustomByName& Expected)
{
	INFO(What);
	CHECK(Actual.Name.Equals(Expected.Name, ESearchCase::CaseSensitive));
	CHECK(Actual.Data == Expected.Data);
}

void TestEqualBytes(FAutomationTestFixture& Test, const TCHAR* What, FMemoryView Actual, TArrayView<const uint8> Expected)
{
	INFO(What);
	CHECK(Actual.EqualBytes(MakeMemoryView(Expected)));
}

template <typename A, typename B>
void TestEqualCb(FAutomationTestFixture& Test, const FString& What, const A& Actual, const B& Expected)
{
	INFO(What);
	CHECK(Actual==Expected);
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestFieldNoClone(FAutomationTestFixture& Test, const TCHAR* What, FCbFieldView& Field, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
{
	INFO(What);
	CHECK_EQUAL(Invoke(Accessors.IsType, Field), (ExpectedError != ECbFieldError::TypeError));
	if (ExpectedError == ECbFieldError::None && !Field.IsBool())
	{
		CHECK_FALSE(Field.AsBool());
		CHECK(Field.HasError());
		CHECK_EQUAL(Field.GetError(), ECbFieldError::TypeError);
	}
	TestEqualCb(Test, FString::Printf(TEXT("FCbFieldView::As[Type](%s) -> Equal"), What), Invoke(Accessors.AsType, Field, DefaultValue), ExpectedValue);
	TestEqualCb(Test, FString::Printf(TEXT("FCbFieldView::As[Type](%s) -> HasError()"), What), Field.HasError(), ExpectedError != ECbFieldError::None);
	TestEqualCb(Test, FString::Printf(TEXT("FCbFieldView::As[Type](%s) -> GetError()"), What), Field.GetError(), ExpectedError);
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestFieldNoClone(FAutomationTestFixture& Test, const TCHAR* What, TArrayView<const uint8> Value, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
{
	FCbFieldView Field(Value.GetData(), FieldType);
	TestFieldNoClone<FieldType>(Test, What, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestField(FAutomationTestFixture& Test, const TCHAR* What, FCbFieldView& Field, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
{
	TestFieldNoClone<FieldType>(Test, What, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
	FCbField FieldClone = FCbField::Clone(Field);
	TestFieldNoClone<FieldType>(Test, *FString::Printf(TEXT("%s, Clone"), What), FieldClone, ExpectedValue, DefaultValue, ExpectedError, Accessors);
	CHECK(Field.Equals(FieldClone));
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestField(FAutomationTestFixture& Test, const TCHAR* What, TArrayView<const uint8> Value, T ExpectedValue = T(), T DefaultValue = T(), ECbFieldError ExpectedError = ECbFieldError::None, const AccessorsType& Accessors = AccessorsType())
{
	FCbFieldView Field(Value.GetData(), FieldType);
	CHECK_EQUAL(Field.GetSize(), uint64(Value.Num() + !FCbFieldType::HasFieldType(FieldType)));
	CHECK(Field.HasValue());
	CHECK_FALSE(Field.HasError());
	CHECK_EQUAL(Field.GetError(), ECbFieldError::None);
	TestField<FieldType>(Test, What, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestFieldError(FAutomationTestFixture& Test, const TCHAR* What, FCbFieldView& Field, ECbFieldError ExpectedError, T ExpectedValue = T(), const AccessorsType& Accessors = AccessorsType())
{
	TestFieldNoClone<FieldType>(Test, What, Field, ExpectedValue, ExpectedValue, ExpectedError, Accessors);
}

template <ECbFieldType FieldType, typename T = TCbFieldValueType<FieldType>, typename AccessorsType = TCbFieldTypeAccessors<FieldType>>
void TestFieldError(FAutomationTestFixture& Test, const TCHAR* What, TArrayView<const uint8> Value, ECbFieldError ExpectedError, T ExpectedValue = T(), const AccessorsType& Accessors = AccessorsType())
{
	FCbFieldView Field(Value.GetData(), FieldType);
	TestFieldError<FieldType>(Test, What, Field, ExpectedError, ExpectedValue, Accessors);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Ctor Test", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView")
	{
		constexpr FCbFieldView DefaultField;
		static_assert(!DefaultField.HasName(), "Error in HasName()");
		static_assert(!DefaultField.HasValue(), "Error in HasValue()");
		static_assert(!DefaultField.HasError(), "Error in HasError()");
		static_assert(DefaultField.GetError() == ECbFieldError::None, "Error in GetError()");
		CHECK_EQUAL(DefaultField.GetSize(), uint64(1));
		CHECK_EQUAL(DefaultField.GetName().Len(), 0);
		CHECK_FALSE(DefaultField.HasName());
		CHECK_FALSE(DefaultField.HasValue());
		CHECK_FALSE(DefaultField.HasError());
		CHECK_EQUAL(DefaultField.GetError(), ECbFieldError::None);
		//CHECK(FIoHash::HashBuffer(MakeMemoryView<uint8>({uint8(ECbFieldType::None)})));
		FMemoryView View;
		CHECK_FALSE(DefaultField.TryGetView(View));
	}

	SECTION("Test FCbFieldView(None)")
	{
		FCbFieldView NoneField(nullptr, ECbFieldType::None);
		CHECK_EQUAL(NoneField.GetSize(), uint64(1));
		CHECK_EQUAL(NoneField.GetName().Len(), 0);
		CHECK_FALSE(NoneField.HasName());
		CHECK_FALSE(NoneField.HasValue());
		CHECK_FALSE(NoneField.HasError());
		CHECK_EQUAL(NoneField.GetError(), ECbFieldError::None);
		CHECK_EQUAL(NoneField.GetHash(), FCbFieldView().GetHash());
		FMemoryView View;
		CHECK_FALSE(NoneField.TryGetView(View));
	}

	SECTION("Test FCbFieldView(None|Type|Name)")
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType), 4, 'N', 'a', 'm', 'e' };
		FCbFieldView NoneField(NoneBytes);
		CHECK_EQUAL(NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		CHECK_EQUAL(NoneField.GetName(), "Name"_U8SV);
		CHECK(NoneField.HasName());
		CHECK_FALSE(NoneField.HasValue());
		CHECK_EQUAL(NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		FMemoryView View;
		CHECK(NoneField.TryGetView(View));
		CHECK(View == MakeMemoryView(NoneBytes));

		uint8 CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MakeMemoryView(CopyBytes));
		CHECK(MakeMemoryView(NoneBytes).EqualBytes(MakeMemoryView(CopyBytes)));
	}

	SECTION("Test FCbFieldView(None|Type)")
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType) };
		FCbFieldView NoneField(NoneBytes);
		CHECK_EQUAL(NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		CHECK_EQUAL(NoneField.GetName().Len(), 0);
		CHECK_FALSE( NoneField.HasName());
		CHECK_FALSE(NoneField.HasValue());
		CHECK_EQUAL(NoneField.GetHash(), FCbFieldView().GetHash());
		FMemoryView View;
		CHECK(NoneField.TryGetView(View));
		CHECK(View == MakeMemoryView(NoneBytes));
	}

	SECTION("Test FCbFieldView(None|Name)")
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const ANSICHAR NoneBytes[] = { ANSICHAR(FieldType), 4, 'N', 'a', 'm', 'e' };
		FCbFieldView NoneField(NoneBytes + 1, FieldType);
		CHECK_EQUAL(NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		CHECK_EQUAL(NoneField.GetName(), "Name"_U8SV);
		CHECK(NoneField.HasName());
		CHECK_FALSE( NoneField.HasValue());
		CHECK_EQUAL(NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		FMemoryView View;
		CHECK_FALSE(NoneField.TryGetView(View));

		uint8 CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MakeMemoryView(CopyBytes));
		CHECK(MakeMemoryView(NoneBytes).EqualBytes(MakeMemoryView(CopyBytes)));
	}

	SECTION("Test FCbFieldView(None|EmptyName)")
	{
		constexpr ECbFieldType FieldType = ECbFieldType::None | ECbFieldType::HasFieldName;
		constexpr const uint8 NoneBytes[] = { uint8(FieldType), 0 };
		FCbFieldView NoneField(NoneBytes + 1, FieldType);
		CHECK_EQUAL(NoneField.GetSize(), uint64(sizeof(NoneBytes)));
		CHECK_EQUAL(NoneField.GetName(), ""_U8SV);
		CHECK(NoneField.HasName());
		CHECK_FALSE(NoneField.HasValue());
		CHECK_EQUAL(NoneField.GetHash(), FIoHash::HashBuffer(MakeMemoryView(NoneBytes)));
		FMemoryView View;
		CHECK_FALSE(NoneField.TryGetView(View));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Null Test", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(Null)")
	{
		FCbFieldView NullField(nullptr, ECbFieldType::Null);
		CHECK_EQUAL(NullField.GetSize(), uint64(1));
		CHECK(NullField.IsNull());
		CHECK(NullField.HasValue());
		CHECK_FALSE(NullField.HasError());
		CHECK_EQUAL(NullField.GetError(), ECbFieldError::None);
		CHECK_EQUAL(NullField.GetHash(), FIoHash::HashBuffer(MakeMemoryView<uint8>({uint8(ECbFieldType::Null)})));
	}

	SECTION("Test FCbFieldView(None) as Null")
	{
		FCbFieldView Field;
		CHECK_FALSE(Field.IsNull());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Object Test", "[Core][Serialization][Smoke]")
{
	static_assert(!std::is_constructible<FCbFieldView, const FCbObjectView&>::value, "Invalid constructor for FCbFieldView");
	static_assert(!std::is_assignable<FCbFieldView, const FCbObjectView&>::value, "Invalid assignment for FCbFieldView");
	static_assert(!std::is_convertible<FCbFieldView, FCbObjectView>::value, "Invalid conversion to FCbObjectView");
	static_assert(!std::is_assignable<FCbObjectView, const FCbFieldView&>::value, "Invalid assignment for FCbObjectView");

	auto TestIntObject = [this](const FCbObjectView& Object, int32 ExpectedNum, uint64 ExpectedValueSize)
	{
		CHECK_EQUAL(Object.GetSize(), ExpectedValueSize + sizeof(ECbFieldType));

		int32 ActualNum = 0;
		for (FCbFieldViewIterator It = Object.CreateViewIterator(); It; ++It)
		{
			++ActualNum;
			TestNotEqual(TEXT("FCbFieldView(Object) Iterator Name"), It->GetName().Len(), 0);
			CHECK_EQUAL(It->AsInt32(), ActualNum);
		}
		CHECK_EQUAL( ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbFieldView Field : Object)
		{
			++ActualNum;
			TestNotEqual(TEXT("FCbFieldView(Object) Iterator Name"), Field.GetName().Len(), 0);
			CHECK_EQUAL(Field.AsInt32(), ActualNum);
		}
		CHECK_EQUAL(ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbFieldView Field : Object.AsFieldView())
		{
			++ActualNum;
			TestNotEqual(TEXT("FCbFieldView(ObjectField) Iterator Name"), Field.GetName().Len(), 0);
			CHECK_EQUAL(Field.AsInt32(), ActualNum);
		}
		CHECK_EQUAL( ActualNum, ExpectedNum);
	};

	SECTION("Test FCbFieldView(Object, Empty)")
	{
		TestField<ECbFieldType::Object>(*this, TEXT("Object, Empty"), { 0 });
	}
	
	SECTION("Test FCbFieldView(Object, Empty)")
	{
		FCbObjectView Object;
		TestIntObject(Object, 0, 1);

		// Find fields that do not exist.
		CHECK_FALSE(Object.FindView("Field"_U8SV).HasValue());
		CHECK_FALSE(Object.FindViewIgnoreCase("Field"_U8SV).HasValue());
		CHECK_FALSE(Object["Field"_U8SV].HasValue());

		// Advance an iterator past the last field.
		FCbFieldViewIterator It = Object.CreateViewIterator();
		CHECK_FALSE(bool(It));
		CHECK(!It);
		for (int Count = 16; Count > 0; --Count)
		{
			++It;
			It->AsInt32();
		}
		CHECK_FALSE(bool(It));
		CHECK(!It);
	}

	SECTION("Test FCbFieldView(Object, NotEmpty)")
	{
		constexpr uint8 IntType = uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive);
		const uint8 Value[] = { 12, IntType, 1, 'A', 1, IntType, 1, 'B', 2, IntType, 1, 'C', 3 };
		FCbFieldView Field(Value, ECbFieldType::Object);
		TestField<ECbFieldType::Object>(*this, TEXT("Object, NotEmpty"), Field, FCbObjectView(Value, ECbFieldType::Object));
		FCbObject Object = FCbObject::Clone(Field.AsObjectView());
		TestIntObject(Object, 3, sizeof(Value));
		TestIntObject(Field.AsObjectView(), 3, sizeof(Value));
		CHECK(Object.Equals(Field.AsObjectView()));
		CHECK_EQUAL(Object.FindView("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindView("b"_ASV).AsInt32(4), 4);
		CHECK_EQUAL(Object.FindViewIgnoreCase("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindViewIgnoreCase("b"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object["B"_ASV].AsInt32(), 2);
		CHECK_EQUAL(Object["b"_ASV].AsInt32(4), 4);
	}

	SECTION("Test FCbFieldView(UniformObject, NotEmpty)")
	{
		constexpr uint8 IntType = uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive);
		const uint8 Value[] = { 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbFieldView Field(Value, ECbFieldType::UniformObject);
		TestField<ECbFieldType::UniformObject>(*this, TEXT("UniformObject, NotEmpty"), Field, FCbObjectView(Value, ECbFieldType::UniformObject));
		FCbObject Object = FCbObject::Clone(Field.AsObjectView());
		TestIntObject(Object, 3, sizeof(Value));
		TestIntObject(Field.AsObjectView(), 3, sizeof(Value));
		CHECK(Object.Equals(Field.AsObjectView()));
		CHECK_EQUAL(Object.FindView("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.Find("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindView("b"_ASV).AsInt32(4), 4);
		CHECK_EQUAL(Object.Find("b"_ASV).AsInt32(4), 4);
		CHECK_EQUAL(Object.FindViewIgnoreCase("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindIgnoreCase("B"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindViewIgnoreCase("b"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object.FindIgnoreCase("b"_ASV).AsInt32(), 2);
		CHECK_EQUAL(Object["B"_ASV].AsInt32(), 2);
		CHECK_EQUAL(Object["b"_ASV].AsInt32(4), 4);

		CHECK(Object.GetOuterBuffer() == Object.AsField().AsObject().GetOuterBuffer());

		const uint8 NamedValue[] = { 1, 'O', 10, IntType, 1, 'A', 1, 1, 'B', 2, 1, 'C', 3 };
		FCbFieldView NamedField(NamedValue, ECbFieldType::UniformObject | ECbFieldType::HasFieldName);

		{
			INFO("Equals");
			CHECK(Field.AsObjectView().Equals(NamedField.AsObjectView()));
			CHECK(Field.Equals(Field.AsObjectView().AsFieldView()));
			CHECK(NamedField.Equals(NamedField.AsObjectView().AsFieldView()));
		}
		
		{
			INFO("CopyTo");
			uint8 CopyBytes[sizeof(Value) + 1];
			Field.AsObjectView().CopyTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(Value).EqualBytes(MakeMemoryView(CopyBytes) + 1));
			NamedField.AsObjectView().CopyTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(Value).EqualBytes(MakeMemoryView(CopyBytes) + 1));
		}
	
		{
			INFO("TryGetView")
			FMemoryView View;
			CHECK(Object.TryGetView(View));
			CHECK(View == Object.GetOuterBuffer().GetView());
			CHECK_FALSE(Field.AsObjectView().TryGetView(View));
			CHECK_FALSE(NamedField.AsObjectView().TryGetView(View));
		}

		{
			INFO("GetBuffer")
			CHECK(Object.GetBuffer().ToShared().GetView() == Object.GetOuterBuffer().GetView());
			CHECK(FCbField::MakeView(Field).AsObject().GetBuffer().ToShared().GetView().EqualBytes(Object.GetOuterBuffer().GetView()));
			CHECK(FCbField::MakeView(NamedField).AsObject().GetBuffer().ToShared().GetView().EqualBytes(Object.GetOuterBuffer().GetView()));
		}
	}

	SECTION("Test FCbFieldView(None) as Object")
	{
		FCbFieldView Field;
		TestFieldError<ECbFieldType::Object>(*this, TEXT("Object, None"), Field, ECbFieldError::TypeError);
		FCbField::MakeView(Field).AsObject();
	}

	SECTION("Test FCbObjectView(ObjectWithName) and CreateIterator")
	{
		const uint8 ObjectType = uint8(ECbFieldType::Object | ECbFieldType::HasFieldName);
		const uint8 Buffer[] = { ObjectType, 3, 'K', 'e', 'y', 4, uint8(ECbFieldType::HasFieldName | ECbFieldType::IntegerPositive), 1, 'F', 8 };
		const FCbObjectView Object(Buffer);
		CHECK_EQUAL(Object.GetSize(), uint64(6));
		const FCbObject ObjectClone = FCbObject::Clone(Object);
		CHECK_EQUAL(ObjectClone.GetSize(), uint64(6));
		CHECK(Object.Equals(ObjectClone));
		CHECK_EQUAL(ObjectClone.GetHash(), Object.GetHash());
		for (FCbFieldIterator It = ObjectClone.CreateIterator(); It; ++It)
		{
			FCbField Field = *It;
			CHECK_EQUAL(Field.GetName(), "F"_U8SV);
			CHECK_EQUAL(Field.AsInt32(), 8);
			CHECK(Field.IsOwned());
		}
		for (FCbFieldIterator It = ObjectClone.CreateIterator(), End; It != End; ++It)
		{
		}
		for (FCbField Field : ObjectClone)
		{
		}

		SECTION("CopyTo")
		{
			uint8 CopyBytes[6];
			Object.CopyTo(MakeMemoryView(CopyBytes));
			CHECK(ObjectClone.GetOuterBuffer().GetView().EqualBytes(MakeMemoryView(CopyBytes)));
			ObjectClone.CopyTo(MakeMemoryView(CopyBytes));
			CHECK(ObjectClone.GetOuterBuffer().GetView().EqualBytes(MakeMemoryView(CopyBytes)));
		}
		
		SECTION("GetBuffer")
		{
			CHECK(FCbField(FSharedBuffer::MakeView(MakeMemoryView(Buffer))).GetBuffer().ToShared().GetView().EqualBytes(MakeMemoryView(Buffer)));
			CHECK(FCbField(FCbFieldView(Buffer + 1, ECbFieldType(ObjectType)), FSharedBuffer::MakeView(MakeMemoryView(Buffer))).GetBuffer().ToShared().GetView().EqualBytes(MakeMemoryView(Buffer)));
		}
		
		SECTION("Access Missing Field")
		{
			CHECK_FALSE(ObjectClone["M"_ASV].HasValue());
			CHECK_FALSE(ObjectClone.AsField()["M"_ASV].HasValue());
		}
		
	}

	SECTION("Test FCbObjectView as FCbFieldViewIterator")
	{
		uint32 Count = 0;
		FCbObjectView Object;
		for (FCbFieldView Field : FCbFieldViewIterator::MakeSingle(Object.AsFieldView()))
		{
			CHECK(Field.IsObject());
			++Count;
		}
		CHECK_EQUAL(Count, 1u);
	}

	SECTION("Test FCbObject as FCbFieldIterator")
	{
		uint32 Count = 0;
		FCbObject Object;
		Object.MakeOwned();
		for (FCbField Field : FCbFieldIterator::MakeSingle(Object.AsField()))
		{
			CHECK(Field.IsObject());
			++Count;
		}
		CHECK_EQUAL(Count, 1u);
	}

	SECTION("Test FCbObject(Empty) as FCbFieldIterator")
	{
		const uint8 Buffer[] = { uint8(ECbFieldType::Object), 0 };
		const FCbObject Object = FCbObject::Clone(Buffer);
		for (FCbField& Field : Object)
		{
		}
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Array Test", "[Core][Serialization][Smoke]")
{
	static_assert(!std::is_constructible<FCbFieldView, const FCbArrayView&>::value, "Invalid constructor for FCbFieldView");
	static_assert(!std::is_assignable<FCbFieldView, const FCbArrayView&>::value, "Invalid assignment for FCbFieldView");
	static_assert(!std::is_convertible<FCbFieldView, FCbArrayView>::value, "Invalid conversion to FCbArrayView");
	static_assert(!std::is_assignable<FCbArrayView, const FCbFieldView&>::value, "Invalid assignment for FCbArrayView");

	auto TestIntArray = [this](FCbArrayView Array, int32 ExpectedNum, uint64 ExpectedValueSize)
	{
		CHECK_EQUAL(Array.GetSize(), ExpectedValueSize + sizeof(ECbFieldType));
		CHECK_EQUAL(Array.Num(), uint64(ExpectedNum));

		int32 ActualNum = 0;
		for (FCbFieldViewIterator It = Array.CreateViewIterator(); It; ++It)
		{
			++ActualNum;
			CHECK_EQUAL(It->AsInt32(), ActualNum);
		}
		CHECK_EQUAL(ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbFieldView Field : Array)
		{
			++ActualNum;
			CHECK_EQUAL(Field.AsInt32(), ActualNum);
		}
		CHECK_EQUAL(ActualNum, ExpectedNum);

		ActualNum = 0;
		for (FCbFieldView Field : Array.AsFieldView())
		{
			++ActualNum;
			CHECK_EQUAL(Field.AsInt32(), ActualNum);
		}
		CHECK_EQUAL(ActualNum, ExpectedNum);
	};

	SECTION("Test FCbFieldView(Array, Empty)")
	{
		TestField<ECbFieldType::Array>(*this, TEXT("Array, Empty"), { 1, 0 });
	}
	
	SECTION("Test FCbFieldView(Array, Empty)")
	{
		FCbArrayView Array;
		TestIntArray(Array, 0, 2);

		// Advance an iterator past the last field.
		FCbFieldViewIterator It = Array.CreateViewIterator();
		CHECK_FALSE(bool(It));
		CHECK(!It);
		for (int Count = 16; Count > 0; --Count)
		{
			++It;
			It->AsInt32();
		}
		CHECK_FALSE(bool(It));
		CHECK(!It);
	}

	SECTION("Test FCbFieldView(Array, NotEmpty)")
	{
		constexpr uint8 IntType = uint8(ECbFieldType::IntegerPositive);
		const uint8 Value[] = { 7, 3, IntType, 1, IntType, 2, IntType, 3 };
		FCbFieldView Field(Value, ECbFieldType::Array);
		TestField<ECbFieldType::Array>(*this, TEXT("Array, NotEmpty"), Field, FCbArrayView(Value, ECbFieldType::Array));
		FCbArray Array = FCbArray::Clone(Field.AsArrayView());
		TestIntArray(Array, 3, sizeof(Value));
		TestIntArray(Field.AsArrayView(), 3, sizeof(Value));
		CHECK(Array.Equals(Field.AsArrayView()));
	}

	SECTION("Test FCbFieldView(UniformArray)")
	{
		constexpr uint8 IntType = uint8(ECbFieldType::IntegerPositive);
		const uint8 Value[] = { 5, 3, IntType, 1, 2, 3 };
		FCbFieldView Field(Value, ECbFieldType::UniformArray);
		TestField<ECbFieldType::UniformArray>(*this, TEXT("UniformArray"), Field, FCbArrayView(Value, ECbFieldType::UniformArray));
		FCbArray Array = FCbArray::Clone(Field.AsArrayView());
		TestIntArray(Array, 3, sizeof(Value));
		TestIntArray(Field.AsArrayView(), 3, sizeof(Value));
		CHECK(Array.Equals(Field.AsArrayView()));

		CHECK(Array.GetOuterBuffer() == Array.AsField().AsArray().GetOuterBuffer());

		const uint8 NamedValue[] = { 1, 'A', 5, 3, IntType, 1, 2, 3 };
		FCbFieldView NamedField(NamedValue, ECbFieldType::UniformArray | ECbFieldType::HasFieldName);

		{
			INFO("Equals");
			CHECK(Field.AsArrayView().Equals(NamedField.AsArrayView()));
			CHECK(Field.Equals(Field.AsArrayView().AsFieldView()));
			CHECK(NamedField.Equals(NamedField.AsArrayView().AsFieldView()));
		}
		
		{
			INFO("CopyTo");
			uint8 CopyBytes[sizeof(Value) + 1];
			Field.AsArrayView().CopyTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(Value).EqualBytes(MakeMemoryView(CopyBytes) + 1));
			NamedField.AsArrayView().CopyTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(Value).EqualBytes(MakeMemoryView(CopyBytes) + 1));
		}
		
		{
			INFO("TryGetView");
			FMemoryView View;
			CHECK(Array.TryGetView(View));
			CHECK(View == Array.GetOuterBuffer().GetView());
			CHECK_FALSE(Field.AsArrayView().TryGetView(View));
			CHECK_FALSE(NamedField.AsArrayView().TryGetView(View));
		}
		
		{
			INFO("GetBuffer");
			CHECK(Array.GetBuffer().ToShared().GetView() == Array.GetOuterBuffer().GetView());
			CHECK(FCbField::MakeView(Field).AsArray().GetBuffer().ToShared().GetView().EqualBytes(Array.GetOuterBuffer().GetView()));
			CHECK(FCbField::MakeView(NamedField).AsArray().GetBuffer().ToShared().GetView().EqualBytes(Array.GetOuterBuffer().GetView()));
		}
	}

	SECTION("Test FCbFieldView(None) as Array")
	{
		FCbFieldView Field;
		TestFieldError<ECbFieldType::Array>(*this, TEXT("Array, None"), Field, ECbFieldError::TypeError);
		FCbField::MakeView(Field).AsArray();
	}

	SECTION("Test FCbArrayView(ArrayWithName) and CreateIterator")
	{
		const uint8 ArrayType = uint8(ECbFieldType::Array | ECbFieldType::HasFieldName);
		const uint8 Buffer[] = { ArrayType, 3, 'K', 'e', 'y', 3, 1, uint8(ECbFieldType::IntegerPositive), 8 };
		const FCbArrayView Array(Buffer);
		CHECK_EQUAL(Array.GetSize(), uint64(5));
		const FCbArray ArrayClone = FCbArray::Clone(Array);
		CHECK_EQUAL(ArrayClone.GetSize(), uint64(5));
		CHECK(Array.Equals(ArrayClone));
		CHECK_EQUAL(ArrayClone.GetHash(), Array.GetHash());
		for (FCbFieldIterator It = ArrayClone.CreateIterator(); It; ++It)
		{
			FCbField Field = *It;
			CHECK_EQUAL(Field.AsInt32(), 8);
			CHECK(Field.IsOwned());
		}
		for (FCbFieldIterator It = ArrayClone.CreateIterator(), End; It != End; ++It)
		{
		}
		for (FCbField Field : ArrayClone)
		{
		}

		{
			INFO("CopyTo");
			uint8 CopyBytes[5];
			Array.CopyTo(MakeMemoryView(CopyBytes));
			CHECK(ArrayClone.GetOuterBuffer().GetView().EqualBytes(MakeMemoryView(CopyBytes)));
			ArrayClone.CopyTo(MakeMemoryView(CopyBytes));
			CHECK(ArrayClone.GetOuterBuffer().GetView().EqualBytes(MakeMemoryView(CopyBytes)));
		}
		
		{
			INFO("GetBuffer")
			CHECK(FCbField(FSharedBuffer::MakeView(MakeMemoryView(Buffer))).GetBuffer().ToShared().GetView().EqualBytes(MakeMemoryView(Buffer)));
			CHECK(FCbField(FCbFieldView(Buffer + 1, ECbFieldType(ArrayType)), FSharedBuffer::MakeView(MakeMemoryView(Buffer))).GetBuffer().ToShared().GetView().EqualBytes(MakeMemoryView(Buffer)));
		}
	}

	SECTION("Test FCbArrayView as FCbFieldViewIterator")
	{
		uint32 Count = 0;
		FCbArrayView Array;
		for (FCbFieldView Field : FCbFieldViewIterator::MakeSingle(Array.AsFieldView()))
		{
			CHECK(Field.IsArray());
			++Count;
		}
		CHECK_EQUAL(Count, 1u);
	}

	SECTION("Test FCbArray as FCbFieldIterator")
	{
		uint32 Count = 0;
		FCbArray Array;
		Array.MakeOwned();
		for (FCbField Field : FCbFieldIterator::MakeSingle(Array.AsField()))
		{
			CHECK(Field.IsArray());
			++Count;
		}
		CHECK_EQUAL(Count, 1u);
	}

	SECTION("Test FCbArray(Empty) as FCbFieldIterator")
	{
		const uint8 Buffer[] = { uint8(ECbFieldType::Array), 1, 0 };
		const FCbArray Array = FCbArray::Clone(Buffer);
		for (FCbField& Field : Array)
		{
		}
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Binary Test", "[Core][Serialization][Smoke]")
{
	struct FCbBinaryAccessors
	{
		TUniqueFunction<bool (const FCbFieldView&)> IsType = [](const FCbFieldView& Field) { return Field.IsInteger(); };
		TUniqueFunction<FSharedBuffer (FCbFieldView& Field, const FSharedBuffer& Default)> AsType =
			[](FCbFieldView& Field, const FSharedBuffer& Default) -> FSharedBuffer { return static_cast<FCbField&>(Field).AsBinary(Default); };
	};

	SECTION("Test FCbFieldView(Binary, Empty)")
	{
		TestField<ECbFieldType::Binary>(*this, TEXT("Binary, Empty"), { 0 });
	}

	SECTION("Test FCbFieldView(Binary, Value)")
	{
		const uint8 Value[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FCbFieldView FieldView(Value, ECbFieldType::Binary);
		TestFieldNoClone<ECbFieldType::Binary>(*this, TEXT("Binary, Value, View"), FieldView, MakeMemoryView(Value + 1, 3));

		FCbField Field = FCbField::Clone(FieldView);
		Field.AsBinary();
		CHECK_FALSE(Field.GetOuterBuffer().IsNull());
		MoveTemp(Field).AsBinary();
		CHECK(Field.GetOuterBuffer().IsNull());
	}

	SECTION("Test FCbFieldView(None) as Binary")
	{
		FCbFieldView FieldView;
		const uint8 Default[] = { 1, 2, 3 };
		TestFieldError<ECbFieldType::Binary>(*this, TEXT("Binary, None, View"), FieldView, ECbFieldError::TypeError, MakeMemoryView(Default));

		FCbField Field = FCbField::Clone(FieldView);
		TestFieldError<ECbFieldType::Binary, FSharedBuffer>(*this, TEXT("Binary, None"), Field, ECbFieldError::TypeError, FSharedBuffer::MakeView(MakeMemoryView(Default)), FCbBinaryAccessors());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::String Test", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(String, Empty)")
	{
		TestField<ECbFieldType::String>(*this, TEXT("String, Empty"), { 0 });
	}
	
	SECTION("Test FCbFieldView(String, Value)")
	{
		const uint8 Value[] = { 3, 'A', 'B', 'C' }; // Size: 3, Data: ABC
		TestField<ECbFieldType::String>(*this, TEXT("String, Value"), Value, FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Value) + 1, 3));
	}

	SECTION("Test FCbFieldView(String, OutOfRangeSize)")
	{
		uint8 Value[9];
		WriteVarUInt(uint64(1) << 31, Value);
		TestFieldError<ECbFieldType::String>(*this, TEXT("String, OutOfRangeSize"), Value, ECbFieldError::RangeError, "ABC"_U8SV);
	}

	SECTION("Test FCbFieldView(None) as String")
	{
		FCbFieldView Field;
		TestFieldError<ECbFieldType::String>(*this, TEXT("String, None"), Field, ECbFieldError::TypeError, "ABC"_U8SV);
	}
}


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

template <typename T, T (FCbFieldView::*InAsTypeFn)(T)>
struct TCbIntegerAccessors
{
	static constexpr bool (FCbFieldView::*IsType)() const = &FCbFieldView::IsInteger;
	static constexpr T (FCbFieldView::*AsType)(T) = InAsTypeFn;
};

void TestIntegerField(FAutomationTestFixture& Test, ECbFieldType FieldType, EIntType ExpectedMask, uint64 Magnitude)
{
	uint8 Value[9];
	const bool Negative = bool(uint8(FieldType) & 1);
	WriteVarUInt(Magnitude - Negative, Value);
	constexpr uint64 DefaultValue = 8;
	const uint64 ExpectedValue = Negative ? uint64(-int64(Magnitude)) : Magnitude;
	FCbFieldView Field(Value, FieldType);
	TestField<ECbFieldType::IntegerNegative>(Test, TEXT("Int8"), Field, int8(EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ExpectedValue : DefaultValue),
		int8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int8) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int8, &FCbFieldView::AsInt8>());
	TestField<ECbFieldType::IntegerNegative>(Test, TEXT("Int16"), Field, int16(EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ExpectedValue : DefaultValue),
		int16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int16) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int16, &FCbFieldView::AsInt16>());
	TestField<ECbFieldType::IntegerNegative>(Test, TEXT("Int32"), Field, int32(EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ExpectedValue : DefaultValue),
		int32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int32) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int32, &FCbFieldView::AsInt32>());
	TestField<ECbFieldType::IntegerNegative>(Test, TEXT("Int64"), Field, int64(EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ExpectedValue : DefaultValue),
		int64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::Int64) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<int64, &FCbFieldView::AsInt64>());
	TestField<ECbFieldType::IntegerPositive>(Test, TEXT("UInt8"), Field, uint8(EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ExpectedValue : DefaultValue),
		uint8(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt8) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint8, &FCbFieldView::AsUInt8>());
	TestField<ECbFieldType::IntegerPositive>(Test, TEXT("UInt16"), Field, uint16(EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ExpectedValue : DefaultValue),
		uint16(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt16) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint16, &FCbFieldView::AsUInt16>());
	TestField<ECbFieldType::IntegerPositive>(Test, TEXT("UInt32"), Field, uint32(EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ExpectedValue : DefaultValue),
		uint32(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt32) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint32, &FCbFieldView::AsUInt32>());
	TestField<ECbFieldType::IntegerPositive>(Test, TEXT("UInt64"), Field, uint64(EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ExpectedValue : DefaultValue),
		uint64(DefaultValue), EnumHasAnyFlags(ExpectedMask, EIntType::UInt64) ? ECbFieldError::None : ECbFieldError::RangeError, TCbIntegerAccessors<uint64, &FCbFieldView::AsUInt64>());
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Integer Test", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(IntegerPositive)")
	{
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos7, 0x00);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos7, 0x7f);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos8, 0x80);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos8, 0xff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos15, 0x0100);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos15, 0x7fff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos16, 0x8000);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos16, 0xffff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos31, 0x0001'0000);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos31, 0x7fff'ffff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos32, 0x8000'0000);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos32, 0xffff'ffff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos63, 0x0000'0001'0000'0000);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos63, 0x7fff'ffff'ffff'ffff);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos64, 0x8000'0000'0000'0000);
		TestIntegerField(*this, ECbFieldType::IntegerPositive, EIntType::Pos64, 0xffff'ffff'ffff'ffff);
	}

	SECTION("Test FCbFieldView(IntegerNegative)")
	{
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg7, 0x01);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg7, 0x80);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg15, 0x81);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg15, 0x8000);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg31, 0x8001);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg31, 0x8000'0000);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg63, 0x8000'0001);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::Neg63, 0x8000'0000'0000'0000);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::None, 0x8000'0000'0000'0001);
		TestIntegerField(*this, ECbFieldType::IntegerNegative, EIntType::None, 0xffff'ffff'ffff'ffff);
	}
	
	SECTION("Test FCbFieldView(None) as Integer")
	{
		FCbFieldView Field;
		TestFieldError<ECbFieldType::IntegerPositive>(*this, TEXT("Integer+, None"), Field, ECbFieldError::TypeError, uint64(8));
		TestFieldError<ECbFieldType::IntegerNegative>(*this, TEXT("Integer-, None"), Field, ECbFieldError::TypeError, int64(8));
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Float Test", "[Core][Serialization][Smoke]")
{
	//SECTION("Test FCbFieldView(Float, 32-bit)")
	//{
	//	const uint8 Value[] = { 0xc0, 0x12, 0x34, 0x56 }; // -2.28444433f
	//	TestField<ECbFieldType::Float32>(TEXT("Float32"), Value, -2.2844443321f);

	//	FCbFieldView Field(Value, ECbFieldType::Float32);
	//	TestField<ECbFieldType::Float64>(TEXT("Float32, AsDouble"), Field, -2.2844443321);
	//}

	SECTION("Test FCbFieldView(Float, 64-bit)")
	{
		const uint8 Value[] = { 0xc1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }; // -631475.76888888876
		TestField<ECbFieldType::Float64>(*this, TEXT("Float64"), Value, -631475.76888888876);

		FCbFieldView Field(Value, ECbFieldType::Float64);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Float64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
	}

	SECTION("Test FCbFieldView(Integer+, MaxBinary32) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 24) - 1, Value); // 16,777,215
		FCbFieldView Field(Value, ECbFieldType::IntegerPositive);
		TestField<ECbFieldType::Float32>(*this, TEXT("Integer+, MaxBinary32, AsFloat"), Field, 16'777'215.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer+, MaxBinary32, AsDouble"), Field, 16'777'215.0);
	}

	SECTION("Test FCbFieldView(Integer+, MaxBinary32+1) as Float")
	{
		uint8 Value[9];
		WriteVarUInt(uint64(1) << 24, Value); // 16,777,216
		FCbFieldView Field(Value, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer+, MaxBinary32+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer+, MaxBinary32+1, AsDouble"), Field, 16'777'216.0);
	}

	SECTION("Test FCbFieldView(Integer+, MaxBinary64) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 53) - 1, Value); // 9,007,199,254,740,991
		FCbFieldView Field(Value, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer+, MaxBinary64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer+, MaxBinary64, AsDouble"), Field, 9'007'199'254'740'991.0);
	}

	SECTION("Test FCbFieldView(Integer+, MaxBinary64+1) as Float")
	{
		uint8 Value[9];
		WriteVarUInt(uint64(1) << 53, Value); // 9,007,199,254,740,992
		FCbFieldView Field(Value, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer+, MaxBinary64+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(*this, TEXT("Integer+, MaxBinary64+1, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	SECTION("Test FCbFieldView(Integer+, MaxUInt64) as Float")
	{
		uint8 Value[9];
		WriteVarUInt(uint64(-1), Value); // Max uint64
		FCbFieldView Field(Value, ECbFieldType::IntegerPositive);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer+, MaxUInt64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(*this, TEXT("Integer+, MaxUInt64, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	SECTION("Test FCbFieldView(Integer-, MaxBinary32) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 24) - 2, Value); // -16,777,215
		FCbFieldView Field(Value, ECbFieldType::IntegerNegative);
		TestField<ECbFieldType::Float32>(*this, TEXT("Integer-, MaxBinary32, AsFloat"), Field, -16'777'215.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer-, MaxBinary32, AsDouble"), Field, -16'777'215.0);
	}

	SECTION("Test FCbFieldView(Integer-, MaxBinary32+1) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 24) - 1, Value); // -16,777,216
		FCbFieldView Field(Value, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer-, MaxBinary32+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer-, MaxBinary32+1, AsDouble"), Field, -16'777'216.0);
	}

	SECTION("Test FCbFieldView(Integer-, MaxBinary64) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 53) - 2, Value); // -9,007,199,254,740,991
		FCbFieldView Field(Value, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer-, MaxBinary64, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestField<ECbFieldType::Float64>(*this, TEXT("Integer-, MaxBinary64, AsDouble"), Field, -9'007'199'254'740'991.0);
	}

	SECTION("Test FCbFieldView(Integer-, MaxBinary64+1) as Float")
	{
		uint8 Value[9];
		WriteVarUInt((uint64(1) << 53) - 1, Value); // -9,007,199,254,740,992
		FCbFieldView Field(Value, ECbFieldType::IntegerNegative);
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("Integer-, MaxBinary64+1, AsFloat"), Field, ECbFieldError::RangeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(*this, TEXT("Integer-, MaxBinary64+1, AsDouble"), Field, ECbFieldError::RangeError, 8.0);
	}

	SECTION("Test FCbFieldView(None) as Float")
	{
		FCbFieldView Field;
		TestFieldError<ECbFieldType::Float32>(*this, TEXT("None, AsFloat"), Field, ECbFieldError::TypeError, 8.0f);
		TestFieldError<ECbFieldType::Float64>(*this, TEXT("None, AsDouble"), Field, ECbFieldError::TypeError, 8.0);
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Bool Test", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(Bool, False)")
	{
		TestField<ECbFieldType::BoolFalse>(*this, TEXT("Bool, False"), {}, false, true);
	}
	
	SECTION("Test FCbFieldView(Bool, True)")
	{
		TestField<ECbFieldType::BoolTrue>(*this, TEXT("Bool, True"), {}, true, false);
	}
	
	SECTION("Test FCbFieldView(None) as Bool")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::BoolFalse>(*this, TEXT("Bool, False, None"), DefaultField, ECbFieldError::TypeError, false);
		TestFieldError<ECbFieldType::BoolTrue>(*this, TEXT("Bool, True, None"), DefaultField, ECbFieldError::TypeError, true);
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Object Attachment Test", "[Core][Serialization][Smoke]")
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	SECTION("Test FCbFieldView(ObjectAttachment, Zero)")
	{
		TestField<ECbFieldType::ObjectAttachment>(*this, TEXT("ObjectAttachment, Zero"), ZeroBytes);
	}
	
	SECTION("Test FCbFieldView(ObjectAttachment, NonZero)")
	{
		TestField<ECbFieldType::ObjectAttachment>(*this, TEXT("ObjectAttachment, NonZero"), SequentialBytes, FIoHash(SequentialBytes));
	}
	
	SECTION("Test FCbFieldView(ObjectAttachment, NonZero) AsAttachment")
	{
		FCbFieldView Field(SequentialBytes, ECbFieldType::ObjectAttachment);
		TestField<ECbFieldType::ObjectAttachment>(*this, TEXT("ObjectAttachment, NonZero, AsAttachment"), Field, FIoHash(SequentialBytes), FIoHash(), ECbFieldError::None, FCbAttachmentAccessors());
	}

	SECTION("Test FCbFieldView(None) as ObjectAttachment")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::ObjectAttachment>(*this, TEXT("ObjectAttachment, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Binary  Attachment Test", "[Core][Serialization][Smoke]")
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	SECTION("Test FCbFieldView(BinaryAttachment, Zero)")
	{
		TestField<ECbFieldType::BinaryAttachment>(*this, TEXT("BinaryAttachment, Zero"), ZeroBytes);
	}
	
	SECTION("Test FCbFieldView(BinaryAttachment, NonZero)")
	{
		TestField<ECbFieldType::BinaryAttachment>(*this, TEXT("BinaryAttachment, NonZero"), SequentialBytes, FIoHash(SequentialBytes));
	}
	
	SECTION("Test FCbFieldView(BinaryAttachment, NonZero) AsAttachment")
	{
		FCbFieldView Field(SequentialBytes, ECbFieldType::BinaryAttachment);
		TestField<ECbFieldType::BinaryAttachment>(*this, TEXT("BinaryAttachment, NonZero, AsAttachment"), Field, FIoHash(SequentialBytes), FIoHash(), ECbFieldError::None, FCbAttachmentAccessors());
	}

	SECTION("Test FCbFieldView(None) as BinaryAttachment")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::BinaryAttachment>(*this, TEXT("BinaryAttachment, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Hash Test", "[Core][Serialization][Smoke]")
{
	const FIoHash::ByteArray ZeroBytes{};
	const FIoHash::ByteArray SequentialBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

	SECTION("Test FCbFieldView(Hash, Zero)")
	{
		TestField<ECbFieldType::Hash>(*this, TEXT("Hash, Zero"), ZeroBytes);
	}
	
	SECTION("Test FCbFieldView(Hash, NonZero)")
	{
		TestField<ECbFieldType::Hash>(*this, TEXT("Hash, NonZero"), SequentialBytes, FIoHash(SequentialBytes));
	}
	
	SECTION("Test FCbFieldView(None) as Hash")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::Hash>(*this, TEXT("Hash, None"), DefaultField, ECbFieldError::TypeError, FIoHash(SequentialBytes));
	}

	SECTION("Test FCbFieldView(ObjectAttachment) as Hash")
	{
		FCbFieldView Field(SequentialBytes, ECbFieldType::ObjectAttachment);
		TestField<ECbFieldType::Hash>(*this, TEXT("ObjectAttachment, NonZero, AsHash"), Field, FIoHash(SequentialBytes));
	}

	SECTION("Test FCbFieldView(BinaryAttachment) as Hash")
	{
		FCbFieldView Field(SequentialBytes, ECbFieldType::BinaryAttachment);
		TestField<ECbFieldType::Hash>(*this, TEXT("BinaryAttachment, NonZero, AsHash"), Field, FIoHash(SequentialBytes));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Uuid Test", "[Core][Serialization][Smoke]")
{
	const uint8 ZeroBytes[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const uint8 SequentialBytes[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	const FGuid SequentialGuid(TEXT("00010203-0405-0607-0809-0a0b0c0d0e0f"));

	SECTION("Test FCbFieldView(Uuid, Zero)")
	{
		TestField<ECbFieldType::Uuid>(*this, TEXT("Uuid, Zero"), ZeroBytes, FGuid(), SequentialGuid);
	}
	
	SECTION("Test FCbFieldView(Uuid, NonZero)")
	{
		TestField<ECbFieldType::Uuid>(*this, TEXT("Uuid, NonZero"), SequentialBytes, SequentialGuid, FGuid());
	}
	
	SECTION("Test FCbFieldView(None) as Uuid")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::Uuid>(*this, TEXT("Uuid, None"), DefaultField, ECbFieldError::TypeError, FGuid::NewGuid());
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Date Time", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(DateTime, Zero)")
	{
		TestField<ECbFieldType::DateTime>(*this, TEXT("DateTime, Zero"), { 0, 0, 0, 0, 0, 0, 0, 0 });
	}
	
	SECTION("Test FCbFieldView(DateTime, 0x1020'3040'5060'7080)")
	{
		TestField<ECbFieldType::DateTime>(*this, TEXT("DateTime, NonZero"), { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 }, int64(0x1020'3040'5060'7080));
	}
	
	SECTION("Test FCbFieldView(DateTime, Zero) as FDateTime")
	{
		const uint8 Value[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbFieldView Field(Value, ECbFieldType::DateTime);
		CHECK_EQUAL(Field.AsDateTime(), FDateTime(0));
	}

	SECTION("Test FCbFieldView(None) as DateTime")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::DateTime>(*this, TEXT("DateTime, None"), DefaultField, ECbFieldError::TypeError);
		const FDateTime DefaultValue(0x1020'3040'5060'7080);
		CHECK_EQUAL(DefaultField.AsDateTime(DefaultValue), DefaultValue);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Time Span", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(TimeSpan, Zero)")
	{
		TestField<ECbFieldType::TimeSpan>(*this, TEXT("TimeSpan, Zero"), { 0, 0, 0, 0, 0, 0, 0, 0 });
	}
	
	SECTION("Test FCbFieldView(TimeSpan, 0x1020'3040'5060'7080)")
	{
		TestField<ECbFieldType::TimeSpan>(*this, TEXT("TimeSpan, NonZero"), { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 }, int64(0x1020'3040'5060'7080));
	}
	
	SECTION("Test FCbFieldView(TimeSpan, Zero) as FTimeSpan")
	{
		const uint8 Value[] = {0, 0, 0, 0, 0, 0, 0, 0};
		FCbFieldView Field(Value, ECbFieldType::TimeSpan);
		CHECK_EQUAL(Field.AsTimeSpan(), FTimespan(0));
	}

	SECTION("Test FCbFieldView(None) as TimeSpan")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::TimeSpan>(*this, TEXT("TimeSpan, None"), DefaultField, ECbFieldError::TypeError);
		const FTimespan DefaultValue(0x1020'3040'5060'7080);
		CHECK_EQUAL( DefaultField.AsTimeSpan(DefaultValue), DefaultValue);
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::ObjectId Span", "[Core][Serialization][Smoke]")
{
	SECTION("Test FCbFieldView(ObjectId, Zero)")
	{
		TestField<ECbFieldType::ObjectId>(*this, TEXT("ObjectId, Zero"), { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
	}
	
	SECTION("Test FCbFieldView(ObjectId, 0x102030405060708090A0B0C0)")
	{
		TestField<ECbFieldType::ObjectId>(*this, TEXT("ObjectId, NonZero"), { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0 },
			FCbObjectId(MakeMemoryView<uint8>({ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0 })));
	}
	
	SECTION("Test FCbFieldView(ObjectId, Zero) as FCbObjectId")
	{
		const uint8 Value[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		FCbFieldView Field(Value, ECbFieldType::ObjectId);
		CHECK_EQUAL(Field.AsObjectId(), FCbObjectId());
	}

	SECTION("Test FCbFieldView(None) as ObjectId")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::ObjectId>(*this, TEXT("ObjectId, None"), DefaultField, ECbFieldError::TypeError);
		const FCbObjectId DefaultValue(MakeMemoryView<uint8>({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0}));
		CHECK_EQUAL(DefaultField.AsObjectId(DefaultValue), DefaultValue);
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Custom By Id", "[Core][Serialization][Smoke]")
{
	struct FCustomByIdAccessor
	{
		explicit FCustomByIdAccessor(uint64 Id)
			: AsType([Id](FCbFieldView& Field, FMemoryView Default) { return Field.AsCustom(Id, Default); })
		{
		}

		bool (FCbFieldView::*IsType)() const = &FCbFieldView::IsCustomById;
		TUniqueFunction<FMemoryView (FCbFieldView& Field, FMemoryView Default)> AsType;
	};

	SECTION("Test FCbFieldView(CustomById, MinId, Empty)")
	{
		const uint8 Value[] = {1, 0};
		TestField<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Empty"), Value, FCbCustomById{0});
		TestField<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Empty, View"), Value, FMemoryView(), MakeMemoryView<uint8>({1, 2, 3}), ECbFieldError::None, FCustomByIdAccessor(0));
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Empty, InvalidId"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByIdAccessor(MAX_uint64));
	}

	SECTION("Test FCbFieldView(CustomById, MinId, Value)")
	{
		const uint8 Value[] = {5, 0, 1, 2, 3, 4};
		TestFieldNoClone<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Value"), Value, FCbCustomById{0, MakeMemoryView(Value).Right(4)});
		TestFieldNoClone<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Value, View"), Value, MakeMemoryView(Value).Right(4), FMemoryView(), ECbFieldError::None, FCustomByIdAccessor(0));
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, MinId, Value, InvalidId"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByIdAccessor(MAX_uint64));
	}

	SECTION("Test FCbFieldView(CustomById, MaxId, Empty)")
	{
		const uint8 Value[] = {9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		TestField<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Empty"), Value, FCbCustomById{MAX_uint64});
		TestField<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Empty, View"), Value, FMemoryView(), MakeMemoryView<uint8>({1, 2, 3}), ECbFieldError::None, FCustomByIdAccessor(MAX_uint64));
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Empty, InvalidId"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByIdAccessor(0));
	}

	SECTION(" Test FCbFieldView(CustomById, MaxId, Value)")
	{
		const uint8 Value[] = {13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 2, 3, 4};
		TestFieldNoClone<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Value"), Value, FCbCustomById{MAX_uint64, MakeMemoryView(Value).Right(4)});
		TestFieldNoClone<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Value, View"), Value, MakeMemoryView(Value).Right(4), FMemoryView(), ECbFieldError::None, FCustomByIdAccessor(MAX_uint64));
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, MaxId, Value, InvalidId"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByIdAccessor(0));
	}

	SECTION("Test FCbFieldView(None) as CustomById")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, None"), DefaultField, ECbFieldError::TypeError, FCbCustomById{4, MakeMemoryView<uint8>({1, 2, 3})});
		TestFieldError<ECbFieldType::CustomById>(*this, TEXT("CustomById, None, View"), DefaultField, ECbFieldError::TypeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByIdAccessor(0));
		const uint8 DefaultValue[] = {1, 2, 3};
		CHECK_EQUAL(DefaultField.AsCustom(0, MakeMemoryView(DefaultValue)), MakeMemoryView(DefaultValue));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Custom By Name", "[Core][Serialization][Smoke]")
{
	struct FCustomByNameAccessor
	{
		explicit FCustomByNameAccessor(FUtf8StringView Name)
			: AsType([Name = FString(Name)](FCbFieldView& Field, FMemoryView Default) { return Field.AsCustom(FTCHARToUTF8(Name), Default); })
		{
		}

		bool (FCbFieldView::*IsType)() const = &FCbFieldView::IsCustomByName;
		TUniqueFunction<FMemoryView (FCbFieldView& Field, FMemoryView Default)> AsType;
	};

	SECTION("Test FCbFieldView(CustomByName, ABC, Empty)")
	{
		const uint8 Value[] = {4, 3, 'A', 'B', 'C'};
		TestField<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Empty"), Value, FCbCustomByName{"ABC"_U8SV});
		TestField<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Empty, View"), Value, FMemoryView(), MakeMemoryView<uint8>({1, 2, 3}), ECbFieldError::None, FCustomByNameAccessor("ABC"_U8SV));
		TestFieldError<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Empty, InvalidCase"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByNameAccessor("abc"_U8SV));
	}

	SECTION("Test FCbFieldView(CustomByName, ABC, Value)")
	{
		const uint8 Value[] = {8, 3, 'A', 'B', 'C', 1, 2, 3, 4};
		TestFieldNoClone<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Value"), Value, FCbCustomByName{"ABC"_U8SV, MakeMemoryView(Value).Right(4)});
		TestFieldNoClone<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Value, View"), Value, MakeMemoryView(Value).Right(4), FMemoryView(), ECbFieldError::None, FCustomByNameAccessor("ABC"_U8SV));
		TestFieldError<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, MinId, Value, InvalidCase"), Value, ECbFieldError::RangeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByNameAccessor("abc"_U8SV));
	}

	SECTION("Test FCbFieldView(None) as CustomByName")
	{
		FCbFieldView DefaultField;
		TestFieldError<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, None"), DefaultField, ECbFieldError::TypeError, FCbCustomByName{"ABC"_U8SV, MakeMemoryView<uint8>({1, 2, 3})});
		TestFieldError<ECbFieldType::CustomByName>(*this, TEXT("CustomByName, None, View"), DefaultField, ECbFieldError::TypeError, MakeMemoryView<uint8>({1, 2, 3}), FCustomByNameAccessor("ABC"_U8SV));
		const uint8 DefaultValue[] = {1, 2, 3};
		CHECK_EQUAL(DefaultField.AsCustom("ABC"_U8SV, MakeMemoryView(DefaultValue)), MakeMemoryView(DefaultValue));
	}
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Iterate Attachments", "[Core][Serialization][Smoke]")
{
	const auto MakeTestHash = [](uint32 Index) { return FIoHash::HashBuffer(&Index, sizeof(Index)); };

	FCbFieldIterator Fields;
	{
		FCbWriter Writer;

		Writer.SetName("IgnoredTypeInRoot").AddHash(MakeTestHash(100));
		Writer.AddObjectAttachment(MakeTestHash(0));
		Writer.AddBinaryAttachment(MakeTestHash(1));
		Writer.SetName("ObjAttachmentInRoot").AddObjectAttachment(MakeTestHash(2));
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
			Writer.AddObjectAttachment(MakeTestHash(10));
			Writer << false;
			Writer.EndArray();
		}
		Writer.EndArray();
		// Uniform array of uniform objects.
		Writer.BeginArray();
		{
			Writer.BeginObject();
			Writer.SetName("ObjAttachmentInUniObjInUniObj1").AddObjectAttachment(MakeTestHash(11));
			Writer.SetName("ObjAttachmentInUniObjInUniObj2").AddObjectAttachment(MakeTestHash(12));
			Writer.EndObject();
			Writer.BeginObject();
			Writer.SetName("ObjAttachmentInUniObjInUniObj3").AddObjectAttachment(MakeTestHash(13));
			Writer.SetName("ObjAttachmentInUniObjInUniObj4").AddObjectAttachment(MakeTestHash(14));
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
			Writer.SetName("ObjAttachmentInNonUniObjInUniObj").AddObjectAttachment(MakeTestHash(15));
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
			Writer.AddObjectAttachment(MakeTestHash(22));
			Writer << false;
			Writer.EndArray();
		}
		Writer.EndObject();
		// Uniform object of uniform objects.
		Writer.BeginObject();
		{
			Writer.SetName("Object1");
			Writer.BeginObject();
			Writer.SetName("ObjAttachmentInUniObjInUniObj1").AddObjectAttachment(MakeTestHash(23));
			Writer.SetName("ObjAttachmentInUniObjInUniObj2").AddObjectAttachment(MakeTestHash(24));
			Writer.EndObject();
			Writer.SetName("Object2");
			Writer.BeginObject();
			Writer.SetName("ObjAttachmentInUniObjInUniObj3").AddObjectAttachment(MakeTestHash(25));
			Writer.SetName("ObjAttachmentInUniObjInUniObj4").AddObjectAttachment(MakeTestHash(26));
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
			Writer.SetName("ObjAttachmentInNonUniObjInUniObj").AddObjectAttachment(MakeTestHash(27));
			Writer << "Bool" << false;
			Writer.EndObject();
		}
		Writer.EndObject();

		Fields = Writer.Save();
	}

	CHECK_EQUAL(ValidateCompactBinaryRange(Fields.GetOuterBuffer(), ECbValidateMode::All), ECbValidateError::None);

	uint32 AttachmentIndex = 0;

	Fields.IterateRangeAttachments([this, &AttachmentIndex, &MakeTestHash](FCbFieldView Field)
		{
			CHECK(Field.IsAttachment());
			CHECK_EQUAL(Field.AsAttachment(), MakeTestHash(AttachmentIndex));
			++AttachmentIndex;
		});
	CHECK_EQUAL(AttachmentIndex, 28);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Field Buffer", "[Core][Serialization][Smoke]")
{
	static_assert(std::is_constructible<FCbField>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbField&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, FCbField&&>::value, "Missing constructor for FCbField");

	static_assert(std::is_constructible<FCbField, const FSharedBuffer&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, FSharedBuffer&&>::value, "Missing constructor for FCbField");

	static_assert(std::is_constructible<FCbField, const FCbFieldView&, const FSharedBuffer&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, const FCbFieldIterator&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, const FCbField&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, const FCbArray&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, const FCbObject&>::value, "Missing constructor for FCbField");

	static_assert(std::is_constructible<FCbField, const FCbFieldView&, FSharedBuffer&&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, FCbFieldIterator&&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, FCbField&&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, FCbArray&&>::value, "Missing constructor for FCbField");
	static_assert(std::is_constructible<FCbField, const FCbFieldView&, FCbObject&&>::value, "Missing constructor for FCbField");

	SECTION("Test FCbField()")
	{
		FCbField DefaultField;
		CHECK_FALSE( DefaultField.HasValue());
		CHECK_FALSE( DefaultField.IsOwned());
		DefaultField.MakeOwned();
		CHECK(DefaultField.IsOwned());
	}

	SECTION("Test Field w/ Type from Shared Buffer")
	{
		const uint8 Value[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		FSharedBuffer ViewBuffer = FSharedBuffer::MakeView(MakeMemoryView(Value));
		FSharedBuffer OwnedBuffer = FSharedBuffer::Clone(ViewBuffer);

		FCbField View(ViewBuffer);
		FCbField ViewMove{FSharedBuffer(ViewBuffer)};
		FCbField ViewOuterField(ImplicitConv<FCbFieldView>(View), ViewBuffer);
		FCbField ViewOuterBuffer(ImplicitConv<FCbFieldView>(View), View);
		FCbField Owned(OwnedBuffer);
		FCbField OwnedMove{FSharedBuffer(OwnedBuffer)};
		FCbField OwnedOuterField(ImplicitConv<FCbFieldView>(Owned), OwnedBuffer);
		FCbField OwnedOuterBuffer(ImplicitConv<FCbFieldView>(Owned), Owned);

		// These lines are expected to assert when uncommented.
		//FCbField InvalidOuterBuffer(ImplicitConv<FCbFieldView>(Owned), ViewBuffer);
		//FCbField InvalidOuterBufferMove(ImplicitConv<FCbFieldView>(Owned), FSharedBuffer(ViewBuffer));

		CHECK_EQUAL(View.AsBinaryView(), ViewBuffer.GetView().Right(3));
		CHECK_EQUAL(ViewMove.AsBinaryView(), View.AsBinaryView());
		CHECK_EQUAL(ViewOuterField.AsBinaryView(), View.AsBinaryView());
		CHECK_EQUAL(ViewOuterBuffer.AsBinaryView(), View.AsBinaryView());
		CHECK_EQUAL(Owned.AsBinaryView(), OwnedBuffer.GetView().Right(3));
		CHECK_EQUAL(OwnedMove.AsBinaryView(), Owned.AsBinaryView());
		CHECK_EQUAL(OwnedOuterField.AsBinaryView(), Owned.AsBinaryView());
		CHECK_EQUAL(OwnedOuterBuffer.AsBinaryView(), Owned.AsBinaryView());

		CHECK_FALSE(View.IsOwned());
		CHECK_FALSE(ViewMove.IsOwned());
		CHECK_FALSE(ViewOuterField.IsOwned());
		CHECK_FALSE(ViewOuterBuffer.IsOwned());
		CHECK(Owned.IsOwned());
		CHECK(OwnedMove.IsOwned());
		CHECK(OwnedOuterField.IsOwned());
		CHECK(OwnedOuterBuffer.IsOwned());

		View.MakeOwned();
		Owned.MakeOwned();
		TestNotEqual(TEXT("FCbField(View).MakeOwned()"), View.AsBinaryView(), ViewBuffer.GetView().Right(3));
		CHECK(View.IsOwned());
		CHECK_EQUAL(Owned.AsBinaryView(), OwnedBuffer.GetView().Right(3));
		CHECK(Owned.IsOwned());
	}

	SECTION("Test Field w/ Type")
	{
		const uint8 Value[] = { uint8(ECbFieldType::Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		const FCbFieldView Field(Value);

		FCbField VoidView = FCbField::MakeView(Value);
		FCbField VoidClone = FCbField::Clone(Value);
		FCbField FieldView = FCbField::MakeView(Field);
		FCbField FieldClone = FCbField::Clone(Field);
		FCbField FieldViewClone = FCbField::Clone(FieldView);

		CHECK_EQUAL(VoidView.AsBinaryView(), MakeMemoryView(Value).Right(3));
		TestNotEqual(TEXT("FCbField::Clone(Void)"), VoidClone.AsBinaryView(), MakeMemoryView(Value).Right(3));
		CHECK(VoidClone.AsBinaryView().EqualBytes(VoidView.AsBinaryView()));
		CHECK_EQUAL(FieldView.AsBinaryView(), MakeMemoryView(Value).Right(3));
		TestNotEqual(TEXT("FCbField::Clone(Field)"), FieldClone.AsBinaryView(), MakeMemoryView(Value).Right(3));
		CHECK(FieldClone.AsBinaryView().EqualBytes(VoidView.AsBinaryView()));
		TestNotEqual(TEXT("FCbField::Clone(FieldView)"), FieldViewClone.AsBinaryView(), FieldView.AsBinaryView());
		CHECK(FieldViewClone.AsBinaryView().EqualBytes(VoidView.AsBinaryView()));

		CHECK_FALSE(VoidView.IsOwned());
		CHECK(VoidClone.IsOwned());
		CHECK_FALSE(FieldView.IsOwned());
		CHECK(FieldClone.IsOwned());
		CHECK(FieldViewClone.IsOwned());
	}

	SECTION("Test Field w/o Type")
	{
		const uint8 Value[] = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
		const FCbFieldView Field(Value, ECbFieldType::Binary);

		FCbField FieldView = FCbField::MakeView(Field);
		FCbField FieldClone = FCbField::Clone(Field);
		FCbField FieldViewClone = FCbField::Clone(FieldView);

		CHECK_EQUAL(FieldView.AsBinaryView(), MakeMemoryView(Value).Right(3));
		CHECK(FieldClone.AsBinaryView().EqualBytes(FieldView.AsBinaryView()));
		CHECK(FieldViewClone.AsBinaryView().EqualBytes(FieldView.AsBinaryView()));

		CHECK_FALSE(FieldView.IsOwned());
		CHECK(FieldClone.IsOwned());
		CHECK(FieldViewClone.IsOwned());

		FieldView.MakeOwned();
		CHECK(FieldView.AsBinaryView().EqualBytes(MakeMemoryView(Value).Right(3)));
		CHECK(FieldView.IsOwned());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Array Buffer", "[Core][Serialization][Smoke]")
{
	static_assert(std::is_constructible<FCbArray>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArray&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, FCbArray&&>::value, "Missing constructor for FCbArray");

	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, const FSharedBuffer&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, const FCbFieldIterator&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, const FCbField&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, const FCbArray&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, const FCbObject&>::value, "Missing constructor for FCbArray");

	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, FSharedBuffer&&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, FCbFieldIterator&&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, FCbField&&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, FCbArray&&>::value, "Missing constructor for FCbArray");
	static_assert(std::is_constructible<FCbArray, const FCbArrayView&, FCbObject&&>::value, "Missing constructor for FCbArray");

	SECTION("Test FCbArray()")
	{
		FCbArray DefaultArray;
		CHECK_FALSE(DefaultArray.IsOwned());
		DefaultArray.MakeOwned();
		CHECK(DefaultArray.IsOwned());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Object Buffer", "[Core][Serialization][Smoke]")
{
	static_assert(std::is_constructible<FCbObject>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObject&&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, FCbObject&&>::value, "Missing constructor for FCbObject");

	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, const FSharedBuffer&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, const FCbFieldIterator&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, const FCbField&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, const FCbArray&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, const FCbObject&>::value, "Missing constructor for FCbObject");

	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, FSharedBuffer&&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, FCbFieldIterator&&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, FCbField&&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, FCbArray&&>::value, "Missing constructor for FCbObject");
	static_assert(std::is_constructible<FCbObject, const FCbObjectView&, FCbObject&&>::value, "Missing constructor for FCbObject");

	SECTION("Test FCbObject()")
	{
		FCbObject DefaultObject;
		CHECK_FALSE(DefaultObject.IsOwned());
		DefaultObject.MakeOwned();
		CHECK(DefaultObject.IsOwned());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Buffer Iterator", "[Core][Serialization][Smoke]")
{
	static_assert(std::is_constructible<FCbFieldViewIterator, const FCbFieldIterator&>::value, "Missing constructor for FCbFieldViewIterator");
	static_assert(std::is_constructible<FCbFieldViewIterator, FCbFieldIterator&&>::value, "Missing constructor for FCbFieldViewIterator");

	static_assert(std::is_constructible<FCbFieldIterator, const FCbFieldIterator&>::value, "Missing constructor for FCbFieldIterator");
	static_assert(std::is_constructible<FCbFieldIterator, FCbFieldIterator&&>::value, "Missing constructor for FCbFieldIterator");

	const auto GetCount = [](auto It) -> uint32
	{
		uint32 Count = 0;
		for (; It; ++It)
		{
			++Count;
		}
		return Count;
	};

	SECTION("Test FCbField[View]Iterator()")
	{
		CHECK_EQUAL(GetCount(FCbFieldViewIterator()), 0);
		CHECK_EQUAL(GetCount(FCbFieldIterator()), 0);
	}

	SECTION("Test FCbField[View]Iterator(Range)")
	{
		constexpr uint8 T = uint8(ECbFieldType::IntegerPositive);
		const uint8 Value[] = { T, 0, T, 1, T, 2, T, 3 };

		const FSharedBuffer View = FSharedBuffer::MakeView(MakeMemoryView(Value));
		const FSharedBuffer Clone = FSharedBuffer::Clone(View);

		const FMemoryView EmptyView;
		const FSharedBuffer NullBuffer;

		const FCbFieldViewIterator FieldViewIt = FCbFieldViewIterator::MakeRange(View);
		const FCbFieldIterator FieldIt = FCbFieldIterator::MakeRange(View);

		CHECK_EQUAL(FieldViewIt.GetRangeHash(), FIoHash::HashBuffer(View));
		CHECK_EQUAL(FieldIt.GetRangeHash(), FIoHash::HashBuffer(View));

		FMemoryView RangeView;
		CHECK(FieldViewIt.TryGetRangeView(RangeView));
		CHECK(RangeView == MakeMemoryView(Value));
		CHECK(FieldIt.TryGetRangeView(RangeView));
		CHECK(RangeView == MakeMemoryView(Value));

		CHECK_EQUAL(GetCount(FCbFieldIterator::CloneRange(FCbFieldViewIterator())), 0);
		CHECK_EQUAL(GetCount(FCbFieldIterator::CloneRange(FCbFieldIterator())), 0);
		const FCbFieldIterator FieldViewItClone = FCbFieldIterator::CloneRange(FieldViewIt);
		const FCbFieldIterator FieldItClone = FCbFieldIterator::CloneRange(FieldIt);
		CHECK_EQUAL(GetCount(FieldViewItClone), 4);
		CHECK_EQUAL(GetCount(FieldItClone), 4);
		TestNotEqual(TEXT("FCbFieldIterator::CloneRange(FieldViewIt).Equals()"), FieldViewItClone, FieldIt);
		TestNotEqual(TEXT("FCbFieldIterator::CloneRange(FieldIt).Equals()"), FieldItClone, FieldIt);

		CHECK_EQUAL(GetCount(FCbFieldViewIterator::MakeRange(EmptyView)), 0);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRange(NullBuffer)), 0);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRange(FSharedBuffer(NullBuffer))), 0);

		CHECK_EQUAL(GetCount(FCbFieldViewIterator::MakeRange(MakeMemoryView(Value))), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRange(Clone)), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRange(FSharedBuffer(Clone))), 4);

		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(View), NullBuffer)), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(View), FSharedBuffer(NullBuffer))), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(View), View)), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(View), FSharedBuffer(View))), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(Clone), Clone)), 4);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(Clone), FSharedBuffer(Clone))), 4);

		CHECK_EQUAL(GetCount(FCbFieldViewIterator(FieldIt)), 4);
		CHECK_EQUAL(GetCount(FCbFieldViewIterator(FCbFieldIterator(FieldIt))), 4);

		const uint8 UniformValue[] = { 0, 1, 2, 3 };
		const FCbFieldViewIterator UniformFieldViewIt = FCbFieldViewIterator::MakeRange(MakeMemoryView(UniformValue), ECbFieldType::IntegerPositive);
		const FSharedBuffer UniformView = FSharedBuffer::MakeView(MakeMemoryView(UniformValue));
		const FCbFieldIterator UniformFieldIt = FCbFieldIterator::MakeRange(UniformView, ECbFieldType::IntegerPositive);

		{
			INFO("Uniform");
			CHECK_EQUAL(UniformFieldViewIt.GetRangeHash(), FieldViewIt.GetRangeHash());
			CHECK_FALSE(UniformFieldViewIt.TryGetRangeView(RangeView));
			CHECK_EQUAL(UniformFieldIt.GetRangeHash(), FieldViewIt.GetRangeHash());
			CHECK_FALSE(UniformFieldIt.TryGetRangeView(RangeView));
		}
		
		{
			INFO("Equals");
			CHECK(FieldViewIt.Equals(AsConst(FieldViewIt)));
			CHECK(FieldViewIt.Equals(AsConst(FieldIt)));
			CHECK(FieldIt.Equals(AsConst(FieldIt)));
			CHECK(FieldIt.Equals(AsConst(FieldViewIt)));
			CHECK_FALSE(FieldViewIt.Equals(FieldViewItClone));
			CHECK_FALSE(FieldIt.Equals(FieldItClone));
			CHECK(UniformFieldViewIt.Equals(AsConst(UniformFieldViewIt)));
			CHECK(UniformFieldViewIt.Equals(UniformFieldIt));
			CHECK(UniformFieldIt.Equals(AsConst(UniformFieldIt)));
			CHECK(UniformFieldIt.Equals(UniformFieldViewIt));
			CHECK_FALSE(FCbFieldViewIterator::MakeRange(MakeMemoryView(UniformValue), ECbFieldType::IntegerPositive)
				.Equals(FCbFieldViewIterator::MakeRange(MakeMemoryView(UniformValue).LeftChop(1), ECbFieldType::IntegerPositive)));
			CHECK_FALSE(FCbFieldViewIterator::MakeRange(MakeMemoryView(UniformValue), ECbFieldType::IntegerPositive)
				.Equals(FCbFieldViewIterator::MakeRange(MakeMemoryView(UniformValue).RightChop(1), ECbFieldType::IntegerPositive)));
		}
		
		{
			INFO("CopyRangeTo");
			uint8 CopyBytes[sizeof(Value)];
			FieldViewIt.CopyRangeTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Value)));
			FieldIt.CopyRangeTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Value)));
			UniformFieldViewIt.CopyRangeTo(MakeMemoryView(CopyBytes));
			CHECK(MakeMemoryView(CopyBytes).EqualBytes(MakeMemoryView(Value)));
		}
		
		{
			INFO("MakeRangeOwned");
			FCbFieldIterator OwnedFromView = UniformFieldIt;
			OwnedFromView.MakeRangeOwned();
			CHECK(OwnedFromView.TryGetRangeView(RangeView));
			CHECK(RangeView.EqualBytes(MakeMemoryView(Value)));
			FCbFieldIterator OwnedFromOwned = OwnedFromView;
			OwnedFromOwned.MakeRangeOwned();
			CHECK_EQUAL(OwnedFromOwned, OwnedFromView);
		}
		

		// These lines are expected to assert when uncommented.
		//const FSharedBuffer ShortView = FSharedBuffer::MakeView(MakeMemoryView(Value).LeftChop(2));
		//CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(*View), ShortView)), 4);
		//CHECK_EQUAL(GetCount(FCbFieldIterator::MakeRangeView(FCbFieldViewIterator::MakeRange(*View), FSharedBuffer(ShortView))), 4);
	}

	SECTION("Test FCbField[View]Iterator(Scalar)")
	{
		constexpr uint8 T = uint8(ECbFieldType::IntegerPositive);
		const uint8 Value[] = { T, 0 };

		const FSharedBuffer View = FSharedBuffer::MakeView(MakeMemoryView(Value));
		const FSharedBuffer Clone = FSharedBuffer::Clone(View);

		const FCbFieldView FieldView(Value);
		const FCbField Field(View);

		CHECK_EQUAL(GetCount(FCbFieldViewIterator::MakeSingle(FieldView)), 1);
		CHECK_EQUAL(GetCount(FCbFieldViewIterator::MakeSingle(FCbFieldView(FieldView))), 1);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeSingle(Field)), 1);
		CHECK_EQUAL(GetCount(FCbFieldIterator::MakeSingle(FCbField(Field))), 1);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Serialization::FCbField::Parse Test", "[Core][Serialization][Smoke]")
{
	// Test the optimal object parsing loop because it is expected to be required for high performance.
	// Under ideal conditions, when the fields are in the expected order and there are no extra fields,
	// the loop will execute once and only one comparison will be performed for each field name. Either
	// way, each field will only be visited once even if the loop needs to execute several times.
	auto ParseObject = [](const FCbObjectView& Object, uint32& A, uint32& B, uint32& C, uint32& D)
	{
		for (FCbFieldViewIterator It = Object.CreateViewIterator(); It;)
		{
			const FCbFieldViewIterator Last = It;
			if (It.GetName().Equals("A"_U8SV))
			{
				A = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("B"_U8SV))
			{
				B = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("C"_U8SV))
			{
				C = It.AsUInt32();
				++It;
			}
			if (It.GetName().Equals("D"_U8SV))
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

	auto TestParseObject = [&ParseObject](std::initializer_list<uint8> Data, uint32 A, uint32 B, uint32 C, uint32 D) -> bool
	{
		uint32 ParsedA = 0, ParsedB = 0, ParsedC = 0, ParsedD = 0;
		ParseObject(FCbObjectView(GetData(Data), ECbFieldType::Object), ParsedA, ParsedB, ParsedC, ParsedD);
		return A == ParsedA && B == ParsedB && C == ParsedC && D == ParsedD;
	};

	constexpr uint8 T = uint8(ECbFieldType::IntegerPositive | ECbFieldType::HasFieldName);
	CHECK(TestParseObject({0}, 0, 0, 0, 0));
	CHECK(TestParseObject({16, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 1, 2, 3, 4));
	CHECK(TestParseObject({16, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4, T, 1, 'A', 1}, 1, 2, 3, 4));
	CHECK(TestParseObject({12, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 0, 2, 3, 4));
	CHECK(TestParseObject({8, T, 1, 'B', 2, T, 1, 'C', 3}, 0, 2, 3, 0));
	CHECK(TestParseObject({20, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4, T, 1, 'E', 5}, 1, 2, 3, 4));
	CHECK(TestParseObject({20, T, 1, 'E', 5, T, 1, 'A', 1, T, 1, 'B', 2, T, 1, 'C', 3, T, 1, 'D', 4}, 1, 2, 3, 4));
	CHECK(TestParseObject({16, T, 1, 'D', 4, T, 1, 'C', 3, T, 1, 'B', 2, T, 1, 'A', 1}, 1, 2, 3, 4));

}
