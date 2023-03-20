// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorOutput.h"
#include "MetasoundPrimitives.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Metasound::Test::GeneratorOutput
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputIsTypeTest,
	"Audio.Metasound.GeneratorOutput.IsType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputIsTypeTest::RunTest(const FString&)
	{
		FMetasoundGeneratorOutput Output;
		
		Output.Init(float{ 0.0f });
		UTEST_TRUE("float is float", Output.IsType<float>());
		UTEST_FALSE("float is not int32", Output.IsType<int32>());
		UTEST_FALSE("float is not bool", Output.IsType<bool>());
		UTEST_FALSE("float is not FString", Output.IsType<FString>());

		Output.Init(int32{ 0 });
		UTEST_FALSE("int32 is not float", Output.IsType<float>());
		UTEST_TRUE("int32 is int32", Output.IsType<int32>());
		UTEST_FALSE("int32 is not bool", Output.IsType<bool>());
		UTEST_FALSE("int32 is not FString", Output.IsType<FString>());
		
		Output.Init(bool{ false });
		UTEST_FALSE("bool is not float", Output.IsType<float>());
		UTEST_FALSE("bool is not int32", Output.IsType<int32>());
		UTEST_TRUE("bool is bool", Output.IsType<bool>());
		UTEST_FALSE("bool is not FString", Output.IsType<FString>());
		
		Output.Init(FString{ "hi!" });
		UTEST_FALSE("FString is not float", Output.IsType<float>());
		UTEST_FALSE("FString is not int32", Output.IsType<int32>());
		UTEST_FALSE("FString is not bool", Output.IsType<bool>());
		UTEST_TRUE("FString is FString", Output.IsType<FString>());
		
		return true;
	}

	template<typename DataType>
	bool RunOutputGetSetTest(FAutomationTestBase& Test, const DataType& InitialValue, const DataType& ExpectedValue)
	{
		FMetasoundGeneratorOutput Output;
		Output.Init<DataType>(InitialValue);
		DataType Value;
		
		if (!Test.TestTrue("Got value", Output.Get<DataType>(Value)))
		{
			return false;
		}
		if (!Test.TestEqual("Value equals initial", Value, InitialValue))
		{
			return false;
		}
		if (!Test.TestTrue("Set value", Output.Set<DataType>(ExpectedValue)))
		{
			return false;
		}
		if (!Test.TestTrue("Got value", Output.Get<DataType>(Value)))
		{
			return false;
		}
		return Test.TestEqual("Value equals expected", Value, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputGetSetFloatTest,
	"Audio.Metasound.GeneratorOutput.GetSet.Float",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputGetSetFloatTest::RunTest(const FString&)
	{
		using DataType = float;
		constexpr DataType InitialValue = 321.654f;
		constexpr DataType ExpectedValue = 123.456f;
		return RunOutputGetSetTest<DataType>(*this, InitialValue, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputGetSetInt32Test,
	"Audio.Metasound.GeneratorOutput.GetSet.Int32",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputGetSetInt32Test::RunTest(const FString&)
	{
		using DataType = int32;
		constexpr DataType InitialValue = 321654;
		constexpr DataType ExpectedValue = 123456;
		return RunOutputGetSetTest<DataType>(*this, InitialValue, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputGetSetBoolTest,
	"Audio.Metasound.GeneratorOutput.GetSet.Bool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputGetSetBoolTest::RunTest(const FString&)
	{
		using DataType = bool;
		constexpr DataType InitialValue = false;
		constexpr DataType ExpectedValue = true;
		return RunOutputGetSetTest<DataType>(*this, InitialValue, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputGetSetStringTest,
	"Audio.Metasound.GeneratorOutput.GetSet.String",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputGetSetStringTest::RunTest(const FString&)
	{
		using FDataType = FString;
		const FDataType InitialValue{ "hello" };
		const FDataType ExpectedValue{ "goodbye" };
		return RunOutputGetSetTest<FDataType>(*this, InitialValue, ExpectedValue);
	}
}

#endif