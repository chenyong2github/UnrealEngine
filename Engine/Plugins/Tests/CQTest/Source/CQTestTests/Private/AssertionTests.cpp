// Copyright Epic Games, Inc. All Rights Reserved.

#include "CQTest.h"
#include "Assert/NoDiscardAsserter.h"

DECLARE_LOG_CATEGORY_CLASS(AssertionTests, Log, All);

namespace CQTests
{
	TEST_CLASS(NoDiscardAssert, "TestFramework.CQTest")
	{
		const char* ExpectedError = "Expected";

		TEST_METHOD(AssertFail_WithMessage_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			Assert.Fail(TEXT(""));
		}

		TEST_METHOD(Assertions_Accept_RawStrings)
		{
			Assert.ExpectError("Hello World");
			Assert.Fail("Hello World");
		}

		TEST_METHOD(Assertions_Accept_TCharArrays)
		{
			Assert.ExpectError(TEXT("Hello World"));
			Assert.Fail(TEXT("Hello World"));
		}

		TEST_METHOD(Assertions_Accept_FStrings)
		{
			FString message = TEXT("Hello World");
			Assert.ExpectError(message);
			Assert.Fail(TEXT("Hello WorldA"));
		}

		TEST_METHOD(AssertExpectError_WithMultipleErrors_Succeeds)
		{
			Assert.ExpectError("", 3);
			Assert.Fail("One");
			Assert.Fail("Two");
			Assert.Fail("Three");
		}

		TEST_METHOD(AsserterExpectError_WithZeroExpected_AcceptsAnyNumber)
		{
			Assert.ExpectError("", 0);
			Assert.Fail("One");
			Assert.Fail("Two");
			Assert.Fail("Three");
		}

		TEST_METHOD(AssertExpectError_WithMatchingError_Succeeds)
		{
			Assert.ExpectError("Hello World");
			Assert.Fail("Hello World");
		}

		TEST_METHOD(AssertExpectError_WithRegexSymbols_EscapesRegex)
		{
			Assert.ExpectError("[^abc]");
			Assert.Fail("[^abc]");
		}

		TEST_METHOD(AssertExpectErrorRegex_WithRegex_Succeeds)
		{
			Assert.ExpectErrorRegex("\\w+");
			Assert.Fail("abc");
		}

		TEST_METHOD(AssertTrue_WithTrue_Succeeds)
		{
			ASSERT_THAT(IsTrue(true));
		}
		TEST_METHOD(AssertTrue_WithTrueAndErrorMessage_DoesNotAddErrorMessage)
		{
			ASSERT_THAT(IsTrue(true, "Unexpected"));
		}
		TEST_METHOD(AssertTrue_WithFalse_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsTrue(false));
		}
		TEST_METHOD(AssertTrue_WithFalseAndError_AddSpecificError)
		{
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(IsTrue(false, ExpectedError));
		}

		TEST_METHOD(AssertEqual_WithSameInts_Succeeds)
		{
			ASSERT_THAT(AreEqual(42, 42));
		}
		TEST_METHOD(AssertEqual_WithSameIntsAndErrorMessage_DoesNotAddErrorMessage)
		{
			ASSERT_THAT(AreEqual(42, 42, "Unexpected"));
		}
		TEST_METHOD(AssertEqual_WithDifferentInts_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(AreEqual(42, 0));
		}
		TEST_METHOD(AssertEqual_WithDifferentIntsAndErrorMessage_AddsSpecificError)
		{
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(AreEqual(42, 0, ExpectedError));
		}

		TEST_METHOD(AssertEqual_WithSameFStrings_Succeeds)
		{
			ASSERT_THAT(AreEqual(TEXT("Hello"), TEXT("Hello")));
		}
		TEST_METHOD(AssertEqual_WithDifferentFStrings_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(AreEqual(TEXT("Hello"), TEXT("World")));
		}
		TEST_METHOD(AssertEqual_WithDifferentCases_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(AreEqual(TEXT("hello"), TEXT("HELLO")));
		}
		TEST_METHOD(AssertEqual_WithDifferentFStringsAndErrorMessage_AddsSpecificError)
		{
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(AreEqual(TEXT("Hello"), TEXT("World"), ExpectedError));
		}
		TEST_METHOD(AssertNear_WithSameNumbers_Succeeds)
		{
			ASSERT_THAT(IsNear(3.14, 3.14, 0.001));
		}
		TEST_METHOD(AssertNear_WithSameNumbersAndErrorMessage_DoesNotAddErrorMessage)
		{
			ASSERT_THAT(IsNear(3.14, 3.14, 0.001, "Unexpected"));
		}
		TEST_METHOD(AssertNear_WithSimilarNumbers_Succeeds)
		{
			ASSERT_THAT(IsNear(3.0, 3.1, 1.0));
		}
		TEST_METHOD(AssertNear_WithDifferentNumbers_AddsError)
		{
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNear(1.0, 2.0, 0.001));
		}
		TEST_METHOD(AssertNear_WithDifferentNumbersAndError_AddsSpecificError)
		{
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(IsNear(1.0, 2.0, 0.001, ExpectedError));
		}

		TEST_METHOD(AssertNull_WithNullPtr_Succeeds)
		{
			int* ptr = nullptr;
			ASSERT_THAT(IsNull(ptr));
		}
		TEST_METHOD(AssertNull_WithNullPtrAndErrorMessage_DoesNotAddError)
		{
			int* ptr = nullptr;
			ASSERT_THAT(IsNull(ptr, "Unexpected"));
		}
		TEST_METHOD(AssertNull_WithNonNull_AddsError)
		{
			int val = 42;
			int* ptr = &val;
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNull(ptr));
		}
		TEST_METHOD(AssertNull_WithNonNullAndErrorMessage_AddsSpecificErrorMessage)
		{
			int val = 42;
			int* ptr = &val;
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(IsNull(ptr, ExpectedError));
		}
		TEST_METHOD(AssertNull_WithInvalidSharedPtr_Succeeds)
		{
			auto ptr = TSharedPtr<int>{};
			ASSERT_THAT(IsNull(ptr));
		}
		TEST_METHOD(AssertNull_WithValidSharedPtr_AddsError)
		{
			TSharedPtr<int> ptr = MakeShared<int>(42);
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNull(ptr));
		}
		TEST_METHOD(AssertNull_WithInvalidUniquePtr_Succeeds)
		{
			TUniquePtr<int> ptr;
			ASSERT_THAT(IsNull(ptr));
		}
		TEST_METHOD(AssertNull_WithValidUniquePtr_AddsError)
		{
			TUniquePtr<int> ptr = MakeUnique<int>(42);
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNull(ptr));
		}

		TEST_METHOD(AssertNotNull_WithNullPtr_AddsError)
		{
			int* ptr = nullptr;
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNotNull(ptr));
		}
		TEST_METHOD(AssertNotNull_WithNullPtrAndError_AddsSpecificError)
		{
			int* ptr = nullptr;
			Assert.ExpectError(FString(ExpectedError));
			ASSERT_THAT(IsNotNull(ptr, ExpectedError));
		}
		TEST_METHOD(AssertNotNull_WithNonNull_Succeeds)
		{
			int val = 42;
			int* ptr = &val;
			ASSERT_THAT(IsNotNull(ptr));
		}
		TEST_METHOD(AssertNotNull_WithNonNullAndErrorMessage_DoesNotAddErrorMessage)
		{
			int val = 42;
			int* ptr = &val;
			ASSERT_THAT(IsNotNull(ptr, "Unexpected"));
		}
		TEST_METHOD(AssertNotNull_WithInvalidSharedPtr_AddsError)
		{
			auto ptr = TSharedPtr<int>{};
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNotNull(ptr));
		}
		TEST_METHOD(AssertNotNull_WithValidSharedPtr_Succeeds)
		{
			TSharedPtr<int> ptr = MakeShared<int>(42);
			ASSERT_THAT(IsNotNull(ptr));
		}
		TEST_METHOD(AssertNotNull_WithInvalidUniquePtr_AddsError)
		{
			TUniquePtr<int> ptr;
			Assert.ExpectError(TEXT(""));
			ASSERT_THAT(IsNotNull(ptr));
		}
		TEST_METHOD(AssertNotNull_WithValidUniquePtr_Succeeds)
		{
			TUniquePtr<int> ptr = MakeUnique<int>(42);
			ASSERT_THAT(IsNotNull(ptr));
		}
	};
	
} // namespace CQTests

struct FCustomType
{
	FCustomType(const FString& InName)
		: Name(InName)
	{
	}

	FString Name;

	bool operator==(const FCustomType& other) const
	{
		return Name == other.Name;
	}
	bool operator!=(const FCustomType& other) const
	{
		return !(*this == other);
	}
};

template <>
FString CQTestConvert::ToString(const FCustomType& obj)
{
	return obj.Name;
}

TEST_CLASS(AssertMessage, "TestFramework.CQTest")
{
	TEST_METHOD(AssertAreEqual_WithUnequalCustomTypes_PrintObjectNames)
	{
		FCustomType left = { "left" };
		FCustomType right = { "right" };

		Assert.ExpectError("Expected left to equal right");
		ASSERT_THAT(AreEqual(left, right));
	}

	TEST_METHOD(AssertAreNotEqual_WithEqualCustomTypes_PrintObjectNames)
	{
		FCustomType obj = { "Object" };

		Assert.ExpectError("Expected Object to not equal Object");
		ASSERT_THAT(AreNotEqual(obj, obj));
	}
};
