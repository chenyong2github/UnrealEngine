// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/InternationalizationMetadata.h"
#include "TestHarness.h"

BEGIN_FUNCTION_BUILD_OPTIMIZATION

TEST_CASE("Core::Internationalization::FLocMetadataValue::Metadata", "[Core][Internationalization][Smoke]")
{
	// BooleanValue metadata
	bool BoolFalse = false;
	bool BoolTrue = true;
	TSharedPtr<FLocMetadataValue> MetadataValueBoolFalse = MakeShareable( new FLocMetadataValueBoolean( BoolFalse ) );
	TSharedPtr<FLocMetadataValue> MetadataValueBoolTrue = MakeShareable( new FLocMetadataValueBoolean( BoolTrue ) );

	// StringValue metadata
	FString StringA = TEXT("A");
	FString StringB = TEXT("B");
	TSharedPtr<FLocMetadataValue> MetadataValueStringA = MakeShareable( new FLocMetadataValueString( StringA ) );
	TSharedPtr<FLocMetadataValue> MetadataValueStringB = MakeShareable( new FLocMetadataValueString( StringB ) );

	// ArrayValue metadata
	TArray< TSharedPtr< FLocMetadataValue > > ArrayA;
	TArray< TSharedPtr< FLocMetadataValue > > ArrayB;

	ArrayA.Add( MetadataValueBoolFalse );
	ArrayA.Add( MetadataValueStringA );

	ArrayB.Add( MetadataValueBoolTrue );
	ArrayB.Add( MetadataValueStringB );

	TSharedPtr<FLocMetadataValue> MetadataValueArrayA = MakeShareable( new FLocMetadataValueArray( ArrayA ) );
	TSharedPtr<FLocMetadataValue> MetadataValueArrayB = MakeShareable( new FLocMetadataValueArray( ArrayB ) );

	// Object metadata
	TSharedPtr< FLocMetadataObject > MetadataObjectA = MakeShareable( new FLocMetadataObject );
	TSharedPtr< FLocMetadataObject > MetadataObjectB = MakeShareable( new FLocMetadataObject );

	// Setup object A
	MetadataObjectA->SetField( TEXT("MetadataBoolFalse"), MetadataValueBoolFalse );
	MetadataObjectA->SetField( TEXT("MetadataStringA"), MetadataValueStringA );
	MetadataObjectA->SetField( TEXT("MetadataArrayA"), MetadataValueArrayA );
	MetadataObjectA->SetField( TEXT("*MetadataCompareModifier"), MetadataValueStringA);// Note: The * name prefix modifies the way entries in the object are compared.

	// Setup object B
	MetadataObjectB->SetField( TEXT("MetadataBoolFalse"), MetadataValueBoolTrue );
	MetadataObjectB->SetField( TEXT("MetadataStringB"), MetadataValueStringB );
	MetadataObjectB->SetField( TEXT("MetadataArrayB"), MetadataValueArrayB );
	MetadataObjectA->SetBoolField( TEXT("*MetadataCompareModifier"), true);// Note: Different type/value. The * name prefix modifies the way entries in the object are compared.

	// ObjectValue metadata
	TSharedPtr< FLocMetadataValue > MetadataValueObjectA = MakeShareable( new FLocMetadataValueObject( MetadataObjectA ) );
	TSharedPtr< FLocMetadataValue > MetadataValueObjectB = MakeShareable( new FLocMetadataValueObject( MetadataObjectB ) );

	SECTION("Testing the bool meta data value type")
	{
		CHECK_FALSE((*MetadataValueBoolFalse == *MetadataValueBoolTrue));
		CHECK((*MetadataValueBoolFalse < *MetadataValueBoolTrue));
		CHECK_FALSE((* MetadataValueBoolTrue < *MetadataValueBoolFalse));

		CHECK((* MetadataValueBoolFalse < *MetadataValueStringA));
		CHECK((*MetadataValueBoolTrue < *MetadataValueStringA));

		CHECK((*MetadataValueBoolFalse < *MetadataValueArrayA));
		CHECK((*MetadataValueBoolTrue < *MetadataValueArrayA));

		CHECK((*MetadataValueBoolFalse < *MetadataValueObjectA));
		CHECK((*MetadataValueBoolTrue < *MetadataValueObjectA));

		TSharedPtr<FLocMetadataValue> MetadataValueBoolFalseClone = MetadataValueBoolFalse->Clone();
		TSharedPtr<FLocMetadataValue> MetadataValueBoolTrueClone = MetadataValueBoolTrue->Clone();

		if( MetadataValueBoolFalse == MetadataValueBoolFalseClone )
		{
			FAIL_CHECK(TEXT("MetadataValueBool and its Clone are not unique objects."));
		}
		
		CHECK((*MetadataValueBoolFalseClone == *MetadataValueBoolFalse));
		CHECK_FALSE((*MetadataValueBoolFalseClone < *MetadataValueBoolFalse));

		CHECK((* MetadataValueBoolTrueClone == *MetadataValueBoolTrue));
		CHECK_FALSE((* MetadataValueBoolTrueClone < *MetadataValueBoolTrue));

		SECTION("Test the bool metadata when it is part of an object")
		{
			TSharedPtr< FLocMetadataObject > MetadataObjectFalse = MakeShareable(new FLocMetadataObject);
			MetadataObjectFalse->SetField(TEXT("MetadataValueBool"), MetadataValueBoolFalse);

			TSharedPtr< FLocMetadataObject > MetadataObjectTrue = MakeShareable(new FLocMetadataObject);
			MetadataObjectTrue->SetField(TEXT("MetadataValueBool"), MetadataValueBoolTrue);

			CHECK_FALSE(MetadataObjectFalse->GetBoolField(TEXT("MetadataValueBool")));
			CHECK(MetadataObjectTrue->GetBoolField(TEXT("MetadataValueBool")));

			CHECK_FALSE(*MetadataObjectFalse == *MetadataObjectTrue);
			CHECK(*MetadataObjectFalse < *MetadataObjectTrue);
		}
	}

	SECTION("Testing string metadata value type")
	{
		CHECK_FALSE((* MetadataValueStringA == *MetadataValueStringB));
		CHECK((*MetadataValueStringA < *MetadataValueStringB));
		CHECK_FALSE((* MetadataValueStringB < *MetadataValueStringA));

		CHECK((*MetadataValueStringA < *MetadataValueArrayA));

		CHECK((*MetadataValueStringA < *MetadataValueObjectA));


		TSharedPtr<FLocMetadataValue> MetadataValueStringAClone = MetadataValueStringA->Clone();

		if( MetadataValueStringA == MetadataValueStringAClone )
		{
			FAIL_CHECK("MetadataValueString and its Clone are not unique objects.");
		}

		CHECK((*MetadataValueStringAClone == *MetadataValueStringA));
		CHECK_FALSE((* MetadataValueStringAClone < *MetadataValueStringA));
		CHECK((* MetadataValueStringAClone < *MetadataValueStringB));

		SECTION("Test the string metadata when it is part of an object")
		{
			TSharedPtr< FLocMetadataObject > TestMetadataObjectA = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectA->SetField(TEXT("MetadataValueString"), MetadataValueStringA);

			TSharedPtr< FLocMetadataObject > TestMetadataObjectB = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectB->SetField(TEXT("MetadataValueString"), MetadataValueStringB);

			CHECK(TestMetadataObjectA->GetStringField(TEXT("MetadataValueString")) == StringA);

			CHECK_FALSE(*TestMetadataObjectA == *TestMetadataObjectB);
			CHECK(*TestMetadataObjectA < *TestMetadataObjectB);
		}
	}

	SECTION("Testing array metadata value type")
	{
		CHECK_FALSE((* MetadataValueArrayA == *MetadataValueArrayB));
		CHECK((*MetadataValueArrayA < *MetadataValueArrayB));
		CHECK_FALSE((* MetadataValueArrayB < *MetadataValueArrayA));

		CHECK((* MetadataValueArrayA < *MetadataValueObjectA));

		TSharedPtr<FLocMetadataValue> MetadataValueArrayAClone = MetadataValueArrayA->Clone();

		if( MetadataValueArrayA == MetadataValueArrayAClone )
		{
			FAIL_CHECK(TEXT("MetadataValueString and its Clone are not unique objects."));
		}

		CHECK((* MetadataValueArrayAClone == *MetadataValueArrayA));
		CHECK_FALSE((* MetadataValueArrayAClone < *MetadataValueArrayA));
		CHECK((* MetadataValueArrayAClone < *MetadataValueArrayB));

		SECTION("Test Less than and equality checks")
		{
			//  Metadata arrays are equivalent if they contain equivalent contents in any order.
			//  To calculate if a metadata array is less than another, we sort both arrays and check each entry index against its counterpart.
			//  If we encounter an entry that is less than another we stop looking.
			TArray< TSharedPtr< FLocMetadataValue > > ArrayC;
			ArrayC.Add(MetadataValueBoolFalse);
			ArrayC.Add(MetadataValueBoolFalse->Clone());
			TSharedPtr<FLocMetadataValue> MetadataValueArrayC = MakeShareable(new FLocMetadataValueArray(ArrayC));

			CHECK_FALSE((*MetadataValueArrayA == *MetadataValueArrayC));
			CHECK((* MetadataValueArrayC < *MetadataValueArrayA));
			CHECK((* MetadataValueArrayC < *MetadataValueArrayB));

			TArray< TSharedPtr< FLocMetadataValue > > ArrayD;
			ArrayD.Add(MetadataValueBoolFalse);
			ArrayD.Add(MetadataValueBoolFalse->Clone());
			ArrayD.Add(MetadataValueBoolFalse->Clone());
			TSharedPtr<FLocMetadataValue> MetadataValueArrayD = MakeShareable(new FLocMetadataValueArray(ArrayD));

			CHECK_FALSE((*MetadataValueArrayA == *MetadataValueArrayD));
			CHECK_FALSE((* MetadataValueArrayC == *MetadataValueArrayD));
			CHECK((*MetadataValueArrayC < *MetadataValueArrayD));
			CHECK((*MetadataValueArrayD < *MetadataValueArrayA));
		}

		SECTION("Test the array metadata when it is part of an object")
		{
			TSharedPtr< FLocMetadataObject > TestMetadataObjectA = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectA->SetField(TEXT("MetadataValueArray"), MetadataValueArrayA);

			TSharedPtr< FLocMetadataObject > TestMetadataObjectB = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectB->SetField(TEXT("MetadataValueArray"), MetadataValueArrayB);

			CHECK(TestMetadataObjectA->GetArrayField(TEXT("MetadataValueArray")) == ArrayA);

			CHECK_FALSE(*TestMetadataObjectA == *TestMetadataObjectB);
			CHECK(*TestMetadataObjectA < *TestMetadataObjectB);
		}
	}

	SECTION("Testing object metadata value type")
	{
		CHECK_FALSE((* MetadataValueObjectA == *MetadataValueObjectB));
		CHECK((* MetadataValueObjectA < *MetadataValueObjectB));
		CHECK_FALSE((* MetadataValueObjectB < *MetadataValueObjectA));

		TSharedPtr<FLocMetadataValue> MetadataValueObjectAClone = MetadataValueObjectA->Clone();

		CHECK_FALSE(MetadataValueObjectA == MetadataValueObjectAClone);
		
		CHECK((*MetadataValueObjectAClone == *MetadataValueObjectA));
		CHECK_FALSE((* MetadataValueObjectAClone < *MetadataValueObjectA));
		CHECK((* MetadataValueObjectAClone < *MetadataValueObjectB));


		SECTION("Test the object metadata when it is part of another object")
		{
			TSharedPtr< FLocMetadataObject > TestMetadataObjectA = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectA->SetField(TEXT("MetadataValueObject"), MetadataValueObjectA);

			TSharedPtr< FLocMetadataObject > TestMetadataObjectB = MakeShareable(new FLocMetadataObject);
			TestMetadataObjectB->SetField(TEXT("MetadataValueObject"), MetadataValueObjectB);

			CHECK(*TestMetadataObjectA->GetObjectField(TEXT("MetadataValueObject")) == *MetadataObjectA);

			CHECK_FALSE(*TestMetadataObjectA == *TestMetadataObjectB);
			CHECK(*TestMetadataObjectA < *TestMetadataObjectB);
		}
	}

	SECTION("Testing Loc Metadata Object")
	{
		CHECK_FALSE(*MetadataObjectA == *MetadataObjectB );
		CHECK(*MetadataObjectA < *MetadataObjectB );
		CHECK_FALSE(*MetadataObjectB < *MetadataObjectA);

		SECTION("Test copy ctor")
		{
			FLocMetadataObject MetadataObjectAClone = *MetadataObjectA;
			CHECK(MetadataObjectAClone == *MetadataObjectA );
		}
		
		SECTION("Test assignment")
		{
			FLocMetadataObject MetadataObjectAClone = *MetadataObjectB;
			// PVS-Studio complains about double initialization, but that's something we're testing
			// so we disable the warning
			MetadataObjectAClone = *MetadataObjectA; //-V519
			CHECK(MetadataObjectAClone == *MetadataObjectA );
			CHECK_FALSE(MetadataObjectAClone == *MetadataObjectB );
		}
		
		SECTION("Test comparison operator")
		{
			FLocMetadataObject MetadataObjectAClone = *MetadataObjectA;
			
			
			// Adding standard entry
			MetadataObjectAClone.SetStringField( TEXT("NewEntry"), TEXT("NewEntryValue") );
			CHECK_FALSE(MetadataObjectAClone == *MetadataObjectA );
			
			// Adding non-standard entry.  Note metadata with * prefix in the name will ignore value and type when performing comparisons
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NewEntry"), TEXT("*NewEntryValue") );
			CHECK_FALSE( MetadataObjectAClone == *MetadataObjectA );

			// Value mismatch on entry with * prefix with same type
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			FLocMetadataObject MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare2") );
			CHECK(MetadataObjectAClone == MetadataObjectAClone2 );

			// Value and type mismatch on entry with * prefix
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetBoolField( TEXT("*NoCompare"), true );
			CHECK(MetadataObjectAClone == MetadataObjectAClone2 );
		
			// Value mismatch on standard entry
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("DoCompare"), TEXT("DoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("DoCompare"), TEXT("DoCompare2") );
			CHECK_FALSE(MetadataObjectAClone == MetadataObjectAClone2 );

			// Value and type mismatch on standard entry
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("DoCompare"), TEXT("DoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetBoolField( TEXT("DoCompare"), true );
			CHECK_FALSE( MetadataObjectAClone == MetadataObjectAClone2 );
		}		
		
		SECTION("Test IsExactMatch function.  Note: Differs from the == operator which takes into account the COMPARISON_MODIFIER_PREFIX")
		{
			// This function behaves similar to the comparison operator except that it ensures all metadata entries match exactly.  In other
			//  words, it will also perform exact match checks on * prefixed metadata items.
			FLocMetadataObject MetadataObjectAClone = *MetadataObjectA;

			CHECK(MetadataObjectAClone.IsExactMatch( *MetadataObjectA) );

			// Adding standard entry
			MetadataObjectAClone.SetStringField( TEXT("NewEntry"), TEXT("NewEntryValue") );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( *MetadataObjectA) );

			// Adding non-standard entry.  Note metadata with * prefix in the name will ignore value and type when performing comparisons
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NewEntry"), TEXT("*NewEntryValue") );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( *MetadataObjectA)  );

			// Value mismatch on entry with * prefix with same type
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			FLocMetadataObject MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare2") );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( MetadataObjectAClone2) );

			// Value and type mismatch on entry with * prefix
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetBoolField( TEXT("*NoCompare"), true );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( MetadataObjectAClone2) );

			// Value mismatch on standard entry
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("DoCompare"), TEXT("DoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("DoCompare"), TEXT("DoCompare2") );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( MetadataObjectAClone2) );

			// Value and type mismatch on standard entry
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("DoCompare"), TEXT("DoCompare") );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetBoolField( TEXT("DoCompare"), true );
			CHECK_FALSE(MetadataObjectAClone.IsExactMatch( MetadataObjectAClone2) );
		}

		SECTION("Test less than operator")
		{
			
			// Adding standard entry that would appear before other entries
			FLocMetadataObject MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("ANewEntry"), TEXT("NewEntryValue") );
			CHECK(MetadataObjectAClone < *MetadataObjectA );
			
			// Adding standard entry that would appear after other entries
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("ZNewEntry"), TEXT("NewEntryValue") );
			CHECK(*MetadataObjectA < MetadataObjectAClone );

			// Adding non-standard entry that would appear before other entries.  Note metadata with * prefix has no special treatment in less than operator
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NewEntry"), TEXT("NewEntryValue") );
			CHECK(MetadataObjectAClone < *MetadataObjectA );

			// Value mismatch on entry with * prefix with same type. Note metadata with * prefix has no special treatment in less than operator
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			FLocMetadataObject MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare2") );
			CHECK(MetadataObjectAClone < MetadataObjectAClone2 );

			// Value and type mismatch on entry with * prefix.  Note metadata with * prefix has no special treatment in less than operator
			MetadataObjectAClone = *MetadataObjectA;
			MetadataObjectAClone.SetBoolField( TEXT("*NoCompare"), true );
			MetadataObjectAClone2 = *MetadataObjectA;
			MetadataObjectAClone2.SetStringField( TEXT("*NoCompare"), TEXT("NoCompare") );
			CHECK(MetadataObjectAClone < MetadataObjectAClone2 );
		}
	}
}

END_FUNCTION_BUILD_OPTIMIZATION
