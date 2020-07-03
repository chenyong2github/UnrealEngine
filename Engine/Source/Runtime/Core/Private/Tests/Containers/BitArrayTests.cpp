// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/BitArray.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE
{
namespace BitArrayTest
{

	TBitArray<> ConstructBitArray(const char* Bits, int32 MaxNum = TNumericLimits<int32>::Max())
	{
		TBitArray<> Out;

		for ( ; MaxNum > 0 && *Bits != '\0'; ++Bits)
		{
			check(*Bits == ' ' || *Bits == '0' || *Bits == '1');

			// Skip spaces
			if (*Bits != ' ')
			{
				Out.Add(*Bits == '1');
				--MaxNum;
			}
		}
		return Out;
	}

	FString BitArrayToString(const TBitArray<>& BitArray)
	{
		FString Out;
		int32 Index = 0;
		for (TBitArray<>::FConstIterator It(BitArray); It; ++It)
		{
			if (Index != 0 && Index % 8 == 0)
			{
				Out.AppendChar(' ');
			}
			Out.AppendChar(It.GetValue() ? '1' : '0');
		}
		return Out;
	}

} // namespace BitArrayTest
} // namespace UE

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayInvariantsTest, "System.Core.Containers.BitArray.Invariants", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayInvariantsTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	// CheckInvariants will fail an assertion if invariants have been broken

	// TBitArray::TBitArray
	{
		TBitArray<> Empty;
		Empty.CheckInvariants();

		TBitArray<> Partial(true, 3);
		Partial.CheckInvariants();

		TBitArray<> Full(true, 32);
		Full.CheckInvariants();
	}
	// TBitArray::Add
	{
		// Num=3
		TBitArray<> Temp(true, 3);
		// Num=5
		Temp.Add(true, 2);
		Temp.CheckInvariants();
		// Num=8
		Temp.Add(true, 3);
		Temp.CheckInvariants();
		// Num=31
		Temp.Add(true, 23);
		Temp.CheckInvariants();
		// Num=32
		Temp.Add(true, 1);
		Temp.CheckInvariants();
		// Num=65
		Temp.Add(true, 33);
		Temp.CheckInvariants();
	}

	// TBitArray::RemoveAt
	{
		// Num=65
		TBitArray<> Temp(true, 65);
		// Num=64
		Temp.RemoveAt(64);
		Temp.CheckInvariants();
		// Num=32
		Temp.RemoveAt(31, 32);
		Temp.CheckInvariants();
		// Num=16
		Temp.RemoveAt(15, 16);
		Temp.CheckInvariants();
		// Num=0
		Temp.RemoveAt(0, 16);
		Temp.CheckInvariants();
	}

	// TBitArray::RemoveAtSwap
	{
		// Num=65
		TBitArray<> Temp(true, 65);
		// Num=64
		Temp.RemoveAtSwap(64);
		Temp.CheckInvariants();
		// Num=32
		Temp.RemoveAtSwap(31, 32);
		Temp.CheckInvariants();
		// Num=16
		Temp.RemoveAtSwap(15, 16);
		Temp.CheckInvariants();
		// Num=0
		Temp.RemoveAtSwap(0, 16);
		Temp.CheckInvariants();
	}

	// TBitArray::Init
	{
		TBitArray<> Temp(false, 16);
		Temp.Init(true, 5);
		Temp.CheckInvariants();

		Temp = TBitArray<>(true, 37);
		Temp.Init(true, 33);
		Temp.CheckInvariants();

		Temp = TBitArray<>(true, 37);
		Temp.Init(true, 32);
		Temp.CheckInvariants();
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayCountSetBitsTest, "System.Core.Containers.BitArray.CountSetBits", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayCountSetBitsTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	// Test unconstrained CountSetBits
	{
		struct FTest { const char* Bits; int32 Expected; };
		FTest Tests[] = {
			{ "0", 0 },
			{ "10010", 2 },
			{ "100001", 2 },
			{ "00000000", 0 },
			{ "10000000", 1 },
			{ "00000001", 1 },
			{ "00000000 1", 1 },
			{ "00000000 0", 0 },
			{ "10000001 1", 3 },
			{ "01011101 11101000 10000001 00101100", 14 },
		};

		for (const FTest& Test : Tests)
		{
			const TBitArray<> Array = ConstructBitArray(Test.Bits);

			const int32 SetBits = Array.CountSetBits();
			if (SetBits != Test.Expected)
			{
				AddError(*FString::Printf(TEXT("CountSetBits: Unexpected number of set bits for array %s. Expected: %i, Actual: %i"), *BitArrayToString(Array), Test.Expected, SetBits));
			}
		}
	}

	// Test constrained CountSetBits
	{
		struct FTest { const char* Bits; int32 StartIndex; int32 EndIndex; int32 Expected; };
		FTest Tests[] = {
			{ "0", 0, 1, 0 },
			{ "0", 0, 1, 0 },
			{ "10000000", 1, 8, 0 },
			{ "00000001", 1, 8, 1 },
			{ "00000000 1", 8, 9, 1 },
			{ "01011101 11101000 10000001 00101100", 24, 32, 3 },
			{ "01011101 11101000 10000001 00101100", 8, 24, 6 },
			{ "01011101 11101000 10000001 00101100", 12, 18, 2 },
			{ "01011101 11101000 10000001 00101100", 4, 30, 12 },
		};

		for (const FTest& Test : Tests)
		{
			const TBitArray<> Array = ConstructBitArray(Test.Bits);

			const int32 SetBits = Array.CountSetBits(Test.StartIndex, Test.EndIndex);
			if (SetBits != Test.Expected)
			{
				AddError(*FString::Printf(TEXT("CountSetBits: Unexpected number of set bits for array %s between index %d and %d. Expected: %i, Actual: %i"), *BitArrayToString(Array), Test.StartIndex, Test.EndIndex, Test.Expected, SetBits));
			}
		}
	}

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayBitwiseNOTTest, "System.Core.Containers.BitArray.BitwiseNOT", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayBitwiseNOTTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	// Test bitwise NOT (~)
	struct FTest { const char* Input; const char* Expected; };
	FTest Tests[] = {
		{ "0",
		  "1" },
		{ "10010",
		  "01101" },
		{ "100001",
		  "011110" },
		{ "00000000",
		  "11111111" },
		{ "10000000",
		  "01111111" },
		{ "00000001",
		  "11111110" },
		{ "00000000 1",
		  "11111111 0" },
		{ "00000000 0",
		  "11111111 1" },
		{ "10000001 1",
		  "01111110 0" },
		{ "01011101 11101000 10000001 001011",
		  "10100010 00010111 01111110 110100" },
	};

	for (const FTest& Test : Tests)
	{
		const TBitArray<> Input    = ConstructBitArray(Test.Input);
		const TBitArray<> Expected = ConstructBitArray(Test.Expected);

		TBitArray<> Result   = Input;
		Result.BitwiseNOT();
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("Bitwise NOT: Unexpected result for source %hs. Expected: %hs, Actual: %s"), Test.Input, Test.Expected, *BitArrayToString(Result)));
		}
	}

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayBitwiseANDTest, "System.Core.Containers.BitArray.BitwiseAND", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayBitwiseANDTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	using BinaryCallable   = TFunctionRef<TBitArray<>(const TBitArray<>&, const TBitArray<>&)>;
	using MutatingCallable = TFunctionRef<void(TBitArray<>&, const TBitArray<>&)>;

	// Test bitwise AND (&) with all 5 combinations of flags:
	// 	EBitwiseOperatorFlags::MinSize
	// 	EBitwiseOperatorFlags::MaxSize (| EBitwiseOperatorFlags::OneFillMissingBits)
	// 	EBitwiseOperatorFlags::MaintainSize (| EBitwiseOperatorFlags::OneFillMissingBits)

	struct FTestInput 
	{
		const char* InputA;
		const char* InputB;
	};
	struct FTestResult
	{
		const char* Expected;
	};

	auto RunBinaryTestImpl = [this](const TCHAR* Description, TArrayView<const FTestInput> Tests, TArrayView<const FTestResult> Results, BinaryCallable BinaryOp)
	{
		check(Tests.Num() == Results.Num());
		for (int32 Index = 0; Index < Tests.Num(); ++Index)
		{
			FTestInput Test = Tests[Index];
			FTestResult TestResult = Results[Index];

			const TBitArray<> InputA   = ConstructBitArray(Test.InputA);
			const TBitArray<> InputB   = ConstructBitArray(Test.InputB);
			const TBitArray<> Expected = ConstructBitArray(TestResult.Expected);

			TBitArray<> Result = BinaryOp(InputA, InputB);

			if (Result != Expected)
			{
				AddError(*FString::Printf(TEXT("%s: Unexpected result for source %hs & %hs. Expected: %hs, Actual: %s"), Description, Test.InputA, Test.InputB, TestResult.Expected, *BitArrayToString(Result)));
			}

			Result = BinaryOp(InputB, InputA);
			if (Result != Expected)
			{
				AddError(*FString::Printf(TEXT("%s: Unexpected result for source %hs & %hs. Expected: %hs, Actual: %s"), Description, Test.InputB, Test.InputA, TestResult.Expected, *BitArrayToString(Result)));
			}
		}
	};

	auto RunMutatingTestImpl = [this](const TCHAR* Description, TArrayView<const FTestInput> Tests, TArrayView<const FTestResult> Results, MutatingCallable MutatingOp)
	{
		for (int32 Index = 0; Index < Tests.Num(); ++Index)
		{
			FTestInput Test = Tests[Index];
			FTestResult TestResult = Results[Index];

			const TBitArray<> InputA   = ConstructBitArray(Test.InputA);
			const TBitArray<> InputB   = ConstructBitArray(Test.InputB);
			const TBitArray<> Expected = ConstructBitArray(TestResult.Expected);

			TBitArray<> Result = InputA;
			MutatingOp(Result, InputB);
			if (Result != Expected)
			{
				AddError(*FString::Printf(TEXT("%s: Unexpected result for source %hs & %hs. Expected: %hs, Actual: %s"), Description, Test.InputA, Test.InputB, TestResult.Expected, *BitArrayToString(Result)));
			}
		}
	};


	FTestInput Tests[] = {
		{ "0",
		  "1" },

		{ "1",
		  "1" },

		{ "0",
		  "0" },

		{ "0001",
		  "11111111" },

		{ "11111111 010",
		  "10000100 011111" },

		{ "11111111 001110 11111",
		  "10000100 001111" },

		{ "11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110",
		  "11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111" },
	};

	{
		FTestResult Results[] = {
			{ "0" },               // 0 & 1
			{ "1" },               // 1 & 1
			{ "0" },               // 0 & 0
			{ "0001" },            // 0001 & 11111111
			{ "10000100 010" },    // 11111111 010 & 10000100 011111
			{ "10000100 001110" }, // 11111111 001110 11111 & 10000100 001111
			// 11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110 &
			// 11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111
			{ "11111100 00111011 11100000 00000000 01100000 00000110 00000000 00000111 11011100" },
		};

		RunBinaryTestImpl(TEXT("BitwiseAND (MinSize)"), Tests, Results, [](const TBitArray<>& InA, const TBitArray<>& InB){ return TBitArray<>::BitwiseAND(InA, InB, EBitwiseOperatorFlags::MinSize); });
		RunMutatingTestImpl(TEXT("CombineWithBitwiseAND (MinSize)"), Tests, Results, [](TBitArray<>& Mutate, const TBitArray<>& InB){ Mutate.CombineWithBitwiseAND(InB, EBitwiseOperatorFlags::MinSize); });
	}

	{
		FTestResult Results[] = {
			{ "0" },                      // 0 & 1
			{ "1" },                      // 1 & 1
			{ "0" },                      // 0 & 0
			{ "00010000" },               // 0001 & 11111111
			{ "10000100 010000" },        // 11111111 010 & 10000100 011111
			{ "10000100 001110 00000" },  // 11111111 001110 11111 & 10000100 001111
			// 11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110 &
			// 11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111
			{ "11111100 00111011 11100000 00000000 01100000 00000110 00000000 00000111 11011100 00000000" },
		};

		RunBinaryTestImpl(TEXT("BitwiseAND (MaxSize)"), Tests, Results, [](const TBitArray<>& InA, const TBitArray<>& InB){ return TBitArray<>::BitwiseAND(InA, InB, EBitwiseOperatorFlags::MaxSize); });
		RunMutatingTestImpl(TEXT("CombineWithBitwiseAND (MaxSize)"), Tests, Results, [](TBitArray<>& Mutate, const TBitArray<>& InB){ Mutate.CombineWithBitwiseAND(InB, EBitwiseOperatorFlags::MaxSize); });
	}

	{
		FTestResult Results[] = {
			{ "0" },                     // 0 & 1
			{ "1" },                     // 1 & 1
			{ "0" },                     // 0 & 0
			{ "00011111" },              // 0001 & 11111111
			{ "10000100 010111" },       // 11111111 010 & 10000100 011111
			{ "10000100 001110 11111" }, // 11111111 001110 11111 & 10000100 001111
			// 11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110 &
			// 11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111
			{ "11111100 00111011 11100000 00000000 01100000 00000110 00000000 00000111 11011100 11111111" },
		};

		RunBinaryTestImpl(TEXT("BitwiseAND (MaxSize | OneFillMissingBits)"), Tests, Results, [](const TBitArray<>& InA, const TBitArray<>& InB){ return TBitArray<>::BitwiseAND(InA, InB, EBitwiseOperatorFlags::MaxSize | EBitwiseOperatorFlags::OneFillMissingBits); });
		RunMutatingTestImpl(TEXT("CombineWithBitwiseAND (MaxSize | OneFillMissingBits)"), Tests, Results, [](TBitArray<>& Mutate, const TBitArray<>& InB){ Mutate.CombineWithBitwiseAND(InB, EBitwiseOperatorFlags::MaxSize | EBitwiseOperatorFlags::OneFillMissingBits); });
	}

	{
		FTestResult Results[] = {
			{ "0" },                     // 0 & 1
			{ "1" },                     // 1 & 1
			{ "0" },                     // 0 & 0
			{ "0001" },                  // 0001 & 11111111
			{ "10000100 010" },          // 11111111 010 & 10000100 011111
			{ "10000100 001110 00000" }, // 11111111 001110 11111 & 10000100 001111
			// 11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110 &
			// 11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111
			{ "11111100 00111011 11100000 00000000 01100000 00000110 00000000 00000111 11011100" },
		};

		RunMutatingTestImpl(TEXT("CombineWithBitwiseAND (MaintainSize)"), Tests, Results, [](TBitArray<>& Mutate, const TBitArray<>& InB){ Mutate.CombineWithBitwiseAND(InB, EBitwiseOperatorFlags::MaintainSize); });
	}

	{
		FTestResult Results[] = {
			{ "0" },                     // 0 & 1
			{ "1" },                     // 1 & 1
			{ "0" },                     // 0 & 0
			{ "0001" },                  // 0001 & 11111111
			{ "10000100 010" },          // 11111111 010 & 10000100 011111
			{ "10000100 001110 11111" }, // 11111111 001110 11111 & 10000100 001111
			// 11111111 00111011 11111110 00000111 11110000 00000110 00001111 00000111 11111110 &
			// 11111100 01111111 11100000 11110000 01100000 00001111 11100000 01111111 11011100 11111111
			{ "11111100 00111011 11100000 00000000 01100000 00000110 00000000 00000111 11011100" },
		};

		RunMutatingTestImpl(TEXT("CombineWithBitwiseAND (MaintainSize | OneFillMissingBits)"), Tests, Results, [](TBitArray<>& Mutate, const TBitArray<>& InB){ Mutate.CombineWithBitwiseAND(InB, EBitwiseOperatorFlags::MaintainSize | EBitwiseOperatorFlags::OneFillMissingBits); });
	}

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayBitwiseORTest, "System.Core.Containers.BitArray.BitwiseOR", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayBitwiseORTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	// Test bitwise OR (|)
	struct FTest { const char *InputA, *InputB, *Expected; };
	FTest Tests[] = {
		{ "0",
		  "1",
		  "1" },
		{ "1",
		  "1",
		  "1" },
		{ "0",
		  "0",
		  "0" },
		{ "00011100",
		  "11111111",
		  "11111111" },
		{ "11111111 001110",
		  "10000100 001111",
		  "11111111 001111" },
		{ "11111111 00111011 111",
		  "10000100 001111",
		  "11111111 001111 11111" },
	};

	for (const FTest& Test : Tests)
	{
		const TBitArray<> InputA   = ConstructBitArray(Test.InputA);
		const TBitArray<> InputB   = ConstructBitArray(Test.InputB);
		const TBitArray<> Expected = ConstructBitArray(Test.Expected);

		TBitArray<> Result = TBitArray<>::BitwiseOR(InputA, InputB, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("BitwiseOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputA, Test.InputB, Test.Expected, *BitArrayToString(Result)));
		}

		Result = TBitArray<>::BitwiseOR(InputB, InputA, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("BitwiseOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputB, Test.InputA, Test.Expected, *BitArrayToString(Result)));
		}

		Result = InputA;
		Result.CombineWithBitwiseOR(InputB, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("CombineWithBitwiseOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputA, Test.InputB, Test.Expected, *BitArrayToString(Result)));
		}
	}

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBitArrayBitwiseXORTest, "System.Core.Containers.BitArray.BitwiseXOR", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FBitArrayBitwiseXORTest::RunTest(const FString& Parameters)
{
	using namespace UE::BitArrayTest;

	// Test bitwise XOR (|)
	struct FTest { const char *InputA, *InputB, *Expected; };
	FTest Tests[] = {
		{ "0",
		  "1",
		  "1" },
		{ "1",
		  "0",
		  "1" },
		{ "1",
		  "1",
		  "0" },
		{ "0",
		  "0",
		  "0" },
		{ "00011100",
		  "11111111",
		  "11100011" },
		{ "11111111 001110",
		  "10000100 001111",
		  "01111011 000001" },
		{ "11111111 00111011 111",
		  "10000100 001111",
		  "01111011 000001 11111" },
	};

	for (const FTest& Test : Tests)
	{
		const TBitArray<> InputA   = ConstructBitArray(Test.InputA);
		const TBitArray<> InputB   = ConstructBitArray(Test.InputB);
		const TBitArray<> Expected = ConstructBitArray(Test.Expected);

		TBitArray<> Result = TBitArray<>::BitwiseXOR(InputA, InputB, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("BitwiseXOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputA, Test.InputB, Test.Expected, *BitArrayToString(Result)));
		}

		Result = TBitArray<>::BitwiseXOR(InputB, InputA, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("BitwiseXOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputB, Test.InputA, Test.Expected, *BitArrayToString(Result)));
		}

		Result = InputA;
		Result.CombineWithBitwiseXOR(InputB, EBitwiseOperatorFlags::MaxSize);
		if (Result != Expected)
		{
			AddError(*FString::Printf(TEXT("CombineWithBitwiseXOR: Unexpected result for source %hs | %hs. Expected: %hs, Actual: %s"), Test.InputA, Test.InputB, Test.Expected, *BitArrayToString(Result)));
		}
	}

	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
