// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Internationalization/InternationalizationArchive.h"
#include "Internationalization/InternationalizationMetadata.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Internationalization::FInternationalizationArchive::Smoke Test", "[Core][Internationalization][Smoke]")
{
	// Key metadata
	TSharedPtr< FLocMetadataObject > KeyMetadataA = MakeShareable( new FLocMetadataObject );
	TSharedPtr< FLocMetadataObject > KeyMetadataB = MakeShareable( new FLocMetadataObject );

	// Source metadata
	TSharedPtr< FLocMetadataObject > SourceMetadataA = MakeShareable( new FLocMetadataObject );
	TSharedPtr< FLocMetadataObject > SourceMetadataB = MakeShareable( new FLocMetadataObject );


	// Setup KeyMetadataA
	KeyMetadataA->SetStringField( TEXT("Gender"			),	TEXT("Masculine")	);
	KeyMetadataA->SetStringField( TEXT("Plurality"		),	TEXT("Singular")	);
	KeyMetadataA->SetStringField( TEXT("TargetGender"	),	TEXT("Masculine")	);
	KeyMetadataA->SetStringField( TEXT("TargetPlurality"),	TEXT("Singular")	);

	// Setup KeyMetadataB
	KeyMetadataB->SetStringField( TEXT("Gender"			),	TEXT("Masculine")	);
	KeyMetadataB->SetStringField( TEXT("Plurality"		),	TEXT("Singular")	);
	KeyMetadataB->SetStringField( TEXT("TargetGender"	),	TEXT("Feminine")	);
	KeyMetadataB->SetStringField( TEXT("TargetPlurality"),	TEXT("Singular")	);

	// Setup source metadata
	SourceMetadataA->SetBoolField( TEXT("*IsMature"), false );
	SourceMetadataB->SetBoolField( TEXT("*IsMature"), true );

	// Setup source item
	FLocItem SourceA( TEXT("TextA") );
	SourceA.MetadataObj = MakeShareable( new FLocMetadataObject(*SourceMetadataA) );

	FLocItem Translation = SourceA;
	Translation.Text = TEXT("TranslatedTextA");

	FString TestNamespace = TEXT("TestNamespace");
	FString SourceAKey = TEXT("TextA");

	// Test entry add
	{
		bool TestOptionalTrue = true;
		bool TestOptionalFalse = false;

		// bIsOptional is not used as a key.  We ensure adding entries where the bIsOptional member is the only difference works as expected.
		FInternationalizationArchive TestArchive;
		TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, NULL, TestOptionalTrue );
		// Add duplicate entry that differs in bIsOptional flag.  This add should report success because we already have an entry with matching 
		//  namespace/source/keymetadata.  Differences in bIsOptional are not taken into consideration. 
		bool bResult = TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, NULL, TestOptionalFalse );
		TEST_TRUE( TEXT("AddEntry result = true"), bResult);

		// We should only have one entry in the archive
		int32 EntryCount = 0;
		for (auto Iter = TestArchive.GetEntriesBySourceTextIterator(); Iter; ++Iter, ++EntryCount) {};
		TEST_EQUAL( TEXT("EntryCount == 1"), EntryCount, 1 );

		// Make sure the original bIsOptional value is not overwritten by our second add.
		TSharedPtr< FArchiveEntry > FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, NULL );
		if( !FoundEntry.IsValid() )
		{
			TEST_TRUE(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid());
		}
		else
		{
			TEST_TRUE( TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->bIsOptional == TestOptionalTrue );
		}
	}

	// Test lookup an entry
	{
		{
			FInternationalizationArchive TestArchive;
			TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, KeyMetadataA, false );

			TSharedPtr< FArchiveEntry > FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, KeyMetadataA );
			if( !FoundEntry.IsValid() )
			{
				TEST_TRUE(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid());
			}
			else
			{
				TEST_TRUE( TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->Namespace == TestNamespace );
				TEST_TRUE( TEXT("FoundEntry->Source == Source"), FoundEntry->Source == SourceA );
				TEST_TRUE( TEXT("FoundEntry->Translation == Translation"), FoundEntry->Translation == Translation );
				if( FoundEntry->KeyMetadataObj == KeyMetadataA )
				{
					TEST_FALSE(TEXT("FArchiveEntry KeyMetadataObj is not a unique object."), FoundEntry->KeyMetadataObj == KeyMetadataA );
				}
				TEST_TRUE( TEXT("FoundEntry->KeyMetadataObj == KeyMetadataA"), *(FoundEntry->KeyMetadataObj) == *(KeyMetadataA) );
			}

			// Passing in a mismatched key metadata will fail to find the entry.  Any fallback logic is intended to happen at runtime
			FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, NULL );
			TEST_INVALID( TEXT("!FoundEntry.IsValid()"), FoundEntry);

			FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, MakeShareable( new FLocMetadataObject() ) );
			TEST_INVALID( TEXT("!FoundEntry.IsValid()"), FoundEntry);

			FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, KeyMetadataB );
			TEST_INVALID( TEXT("!FoundEntry.IsValid()"), FoundEntry);
		}

		// Ensure we can properly lookup entries with non-null but empty key metadata
		{
			FInternationalizationArchive TestArchive;
			TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, MakeShareable( new FLocMetadataObject() ), false );

			TSharedPtr< FArchiveEntry > FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, NULL );
			
			if (TestTrue(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid()))
			{
				TEST_TRUE( TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->Namespace == TestNamespace );
				TEST_TRUE( TEXT("FoundEntry->Source == Source"), FoundEntry->Source == SourceA );
			}
		}

		// Ensure we can properly lookup entries with null key metadata
		{
			FInternationalizationArchive TestArchive;
			TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, NULL, false );

			TSharedPtr< FArchiveEntry > FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, NULL );
			if(TestTrue(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid() ) )
			{
				TEST_TRUE( TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->Namespace == TestNamespace );
				TEST_TRUE( TEXT("FoundEntry->Source == Source"), FoundEntry->Source == SourceA );
			}

			// Test lookup with non-null but empty key metadata
			FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, MakeShareable( new FLocMetadataObject() ) );
			if (TestTrue(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid()))
			{
				TEST_TRUE(TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->Namespace == TestNamespace);
				TEST_TRUE( TEXT("FoundEntry->Source == Source"), FoundEntry->Source == SourceA );
			}
		}

		// Ensure lookup where source metadata has * prefixed entries work as expected.  Note: The source metadata object
		//   supports a * prefix on metadata names which modifies the way we perform metadata comparison(ignores entry type and value, only name is checked)
		{
			FLocItem SourceCompare( TEXT("TextA") );
			SourceCompare.MetadataObj = MakeShareable( new FLocMetadataObject() );
			SourceCompare.MetadataObj->SetStringField( TEXT("*IsMature"), TEXT("") );

			FInternationalizationArchive TestArchive;
			// Added entry with String *IsMature entry
			TestArchive.AddEntry( TestNamespace, SourceAKey, SourceCompare, Translation, KeyMetadataA, false );

			// Finding entry using a source that has Boolean *IsMature
			TSharedPtr< FArchiveEntry > FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, KeyMetadataA );
			if(TestTrue(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid() ) )
			{
				TEST_TRUE( TEXT("FoundEntry->Namespace == Namespace"), FoundEntry->Namespace == TestNamespace );
				TEST_TRUE( TEXT("FoundEntry->Source == Source"), FoundEntry->Source == SourceA );
			}

			// Attempting to add an entry that only differs by a * prefexed source metadata entry value or type will result in success since
			//  a 'matching' entry already exists in the archive.  We should, however, only have one entry in the archive
			bool bResult = TestArchive.AddEntry( TestNamespace, SourceAKey, SourceA, Translation, KeyMetadataA, false );

			TEST_TRUE( TEXT("AddEntry result = true"), bResult);

			// We should only have one entry in the archive
			int32 EntryCount = 0;
			for (auto Iter = TestArchive.GetEntriesBySourceTextIterator(); Iter; ++Iter, ++EntryCount) {};
			TEST_EQUAL( TEXT("EntryCount == 1"), EntryCount, 1 );

			// Check to see that the original type/value of the * prefixed entry did not get modified
			FoundEntry = TestArchive.FindEntryByKey( TestNamespace, SourceAKey, KeyMetadataA );

			if (TestTrue(TEXT("FArchiveEntry could not find entry using FindEntryByKey."), FoundEntry.IsValid()) )
			{
				if( FoundEntry->Source.MetadataObj->HasTypedField< ELocMetadataType::String >( TEXT("*IsMature") ) )
				{
					TEST_TRUE( TEXT("Metadata Type == String and Value == Empty string"), FoundEntry->Source.MetadataObj->GetStringField( TEXT("*IsMature") ) == TEXT("") );
				}
				else
				{
					TEST_TRUE(TEXT("FArchiveEntry * prefixed metadata entry on source object was modified unexpectedly."), false);
				}
			}
		}
	}
}
