// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/Regex.h"
#include "Assert/CQTestConvert.h"

void FNoDiscardAsserter::ExpectError(FString Error, int32 Count)
{
	static const FRegexPattern Pattern(TEXT("([\\[\\]{}()^$.?\\\\*+|])"));
	FRegexMatcher Matcher(Pattern, Error);
	FString SanitizedError(Error);
	int32 Offset = 0;

	while (Matcher.FindNext())
	{
		int32 Pos = Matcher.GetMatchBeginning() + Offset++;
		SanitizedError.InsertAt(Pos, TEXT("\\"));
	}

	TestRunner.AddExpectedError(SanitizedError, EAutomationExpectedErrorFlags::Contains, Count);
}

inline void FNoDiscardAsserter::ExpectErrorRegex(FString Error, int32 Count)
{
	TestRunner.AddExpectedError(Error, EAutomationExpectedErrorFlags::Contains, Count);
}

inline void FNoDiscardAsserter::Fail(FString Error)
{
	TestRunner.AddError(Error);
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsTrue(bool Condition)
{
	return IsTrue(Condition, "condition");
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsTrue(bool Condition, const char* FailureMessage)
{
	return IsTrue(Condition, FString(FailureMessage));
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsTrue(bool Condition, const TCHAR* FailureMessage)
{
	return IsTrue(Condition, FString(FailureMessage));
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsTrue(bool Condition, const FString& FailureMessage)
{
	return TestRunner.TestTrue(FailureMessage, Condition);
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsFalse(bool Condition)
{
	return IsFalse(Condition, "condition");
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsFalse(bool Condition, const char* FailureMessage)
{
	return IsFalse(Condition, FString(FailureMessage));
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsFalse(bool Condition, const TCHAR* FailureMessage)
{
	return IsFalse(Condition, FString(FailureMessage));
}

[[nodiscard]] inline bool FNoDiscardAsserter::IsFalse(bool Condition, const FString& FailureMessage)
{
	return TestRunner.TestFalse(FString(FailureMessage), Condition);
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNull(const T& Ptr)
{
	return IsNull(Ptr, "TestNull failed");
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNull(const T& Ptr, const char* FailureMessage)
{
	return IsNull(Ptr, FString(FailureMessage));
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNull(const T& Ptr, const TCHAR* FailureMessage)
{
	return IsNull(Ptr, FString(FailureMessage));
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNull(const T& Ptr, const FString& FailureMessage)
{
	if (Ptr != nullptr)
	{
		Fail(FailureMessage);
		return false;
	}
	return true;
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNotNull(const T& Ptr)
{
	return IsNotNull(Ptr, "TestNotNull Failed");
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNotNull(const T& Ptr, const char* FailureMessage)
{
	return IsNotNull(Ptr, FString(FailureMessage));
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNotNull(const T& Ptr, const TCHAR* FailureMessage)
{
	return IsNotNull(Ptr, FString(FailureMessage));
}

template <typename T>
[[nodiscard]] bool FNoDiscardAsserter::IsNotNull(const T& Ptr, const FString& FailureMessage)
{
	if (Ptr == nullptr)
	{
		Fail(FailureMessage);
		return false;
	}
	return true;
}


template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreEqual(const TExpected& Expected, const TActual& Actual)
{
	FString Message = FString::Printf(TEXT("Expected %s to equal %s"), *CQTestConvert::ToString(Expected), *CQTestConvert::ToString(Actual));
	return AreEqual(Expected, Actual, Message);
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreEqual(const TExpected& Expected, const TActual& Actual, const char* FailureMessage)
{
	return AreEqual(Expected, Actual, FString(FailureMessage));
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreEqual(const TExpected& Expected, const TActual& Actual, const TCHAR* FailureMessage)
{
	return AreEqual(Expected, Actual, FString(FailureMessage));
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreEqual(const TExpected& Expected, const TActual& Actual, const FString& FailureMessage)
{
	return TestRunner.TestEqual(FailureMessage, Actual, Expected); // FAutomationTestBase expects Actual then Expected
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreNotEqual(const TExpected& Expected, const TActual& Actual)
{
	FString Message = FString::Printf(TEXT("Expected %s to not equal %s"), *CQTestConvert::ToString(Expected), *CQTestConvert::ToString(Actual));
	return AreNotEqual(Expected, Actual, Message);
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreNotEqual(const TExpected& Expected, const TActual& Actual, const char* FailureMessage)
{
	return AreNotEqual(Expected, Actual, FString(FailureMessage));
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreNotEqual(const TExpected& Expected, const TActual& Actual, const TCHAR* FailureMessage)
{
	return AreNotEqual(Expected, Actual, FString(FailureMessage));
}

template <typename TExpected, typename TActual>
[[nodiscard]] bool FNoDiscardAsserter::AreNotEqual(const TExpected& Expected, const TActual& Actual, const FString& FailureMessage)
{
	return TestRunner.TestNotEqual(FailureMessage, Actual, Expected); // FAutomationTestBase expects Actual then Expected
}

template <typename TExpected, typename TActual, typename TEpsilon>
[[nodiscard]] bool FNoDiscardAsserter::IsNear(TExpected Expected, TActual Actual, TEpsilon Epsilon)
{
	FString Message = FString::Printf(TEXT("Expected %s to be near %s (within %s)"), *CQTestConvert::ToString(Expected), *CQTestConvert::ToString(Actual), *CQTestConvert::ToString(Epsilon));
	return IsNear(Expected, Actual, Epsilon, Message);
}

template <typename TExpected, typename TActual, typename TEpsilon>
[[nodiscard]] bool FNoDiscardAsserter::IsNear(TExpected Expected, TActual Actual, TEpsilon Epsilon, const char* FailureMessage)
{
	return IsNear(Expected, Actual, Epsilon, FString(FailureMessage));
}

template <typename TExpected, typename TActual, typename TEpsilon>
[[nodiscard]] inline bool FNoDiscardAsserter::IsNear(TExpected Expected, TActual Actual, TEpsilon Epsilon, const TCHAR* FailureMessage)
{
	return IsNear(Expected, Actual, Epsilon, FString(FailureMessage));
}

template <typename TExpected, typename TActual, typename TEpsilon>
[[nodiscard]] inline bool FNoDiscardAsserter::IsNear(TExpected Expected, TActual Actual, TEpsilon Epsilon, const FString& FailureMessage)
{
	return TestRunner.TestEqual(FailureMessage, Actual, Expected, Epsilon); // FAutomationTestBase expects Actual then Expected
}
