// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodeTestGraphBuilder.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Metasound::Test::Generator
{
	using GraphBuilder = FNodeTestGraphBuilder;
	using namespace Frontend;
	
	template<typename DataType>
	TUniquePtr<FMetasoundGenerator> BuildPassthroughGraph(
		FAutomationTestBase& Test,
		const FName& InputName,
		const FName& OutputName,
		const FSampleRate SampleRate,
		const int32 NumSamplesPerBlock)
	{
		GraphBuilder Builder;
		const FNodeHandle InputNode = Builder.AddInput(InputName, Metasound::GetMetasoundDataTypeName<DataType>());
		const FNodeHandle OutputNode = Builder.AddOutput(OutputName, Metasound::GetMetasoundDataTypeName<DataType>());
		const FOutputHandle OutputToConnect = InputNode->GetOutputWithVertexName(InputName);
		const FInputHandle InputToConnect = OutputNode->GetInputWithVertexName(OutputName);
		if (!Test.TestTrue("Connected input to output", InputToConnect->Connect(*OutputToConnect)))
		{
			return nullptr;
		}
		return Builder.BuildGenerator(SampleRate, NumSamplesPerBlock);
	}
	
	template<typename DataType>
	bool RunSimpleOutputTest(FAutomationTestBase& Test, DataType ExpectedValue)
	{
		const FName InputName = "MyInput";
		const FName OutputName = "MyOutput";
		constexpr FSampleRate SampleRate = 48000;
		constexpr int32 NumSamplesPerBlock = 128;

		// Build a passthrough graph
		const TUniquePtr<FMetasoundGenerator> Generator =
			BuildPassthroughGraph<DataType>(Test, InputName, OutputName, SampleRate, NumSamplesPerBlock);
		if (!Test.TestNotNull("Generator built", Generator.Get()))
		{
			return false;
		}
		
		// Set the input value
		const TOptional<TDataWriteReference<DataType>> InputWriteRef = Generator->GetInputWriteReference<DataType>(InputName);
		if (!Test.TestTrue("Got the write ref", InputWriteRef.IsSet()))
		{
			return false;
		}
		*InputWriteRef.GetValue() = ExpectedValue;

		// Render a block
		{
			TArray<float> Buffer;
			Buffer.Reserve(NumSamplesPerBlock);
			if (!Test.TestEqual(
				"Generated the right number of samples.",
				Generator->OnGenerateAudio(Buffer.GetData(), NumSamplesPerBlock),
				NumSamplesPerBlock))
			{
				return false;
			}
		}

		// Check the output
		const TOptional<TDataReadReference<DataType>> OutputReadRef = Generator->GetOutputReadReference<DataType>(OutputName);
		if (!Test.TestTrue("Got the read ref", OutputReadRef.IsSet()))
		{
			return false;
		}
		if (!Test.TestEqual("Input was passed through", *OutputReadRef.GetValue(), ExpectedValue))
		{
			return false;
		}

		return true;
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputSimpleFloatTest,
	"Audio.Metasound.Generator.Outputs.Simple.Float",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputSimpleFloatTest::RunTest(const FString&)
	{
		return RunSimpleOutputTest<float>(*this, 123.456f);
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputSimpleIntTest,
	"Audio.Metasound.Generator.Outputs.Simple.Int",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputSimpleIntTest::RunTest(const FString&)
	{
		return RunSimpleOutputTest<int32>(*this, 123456);
	}
		
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorOutputSimpleBoolTest,
	"Audio.Metasound.Generator.Outputs.Simple.Bool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorOutputSimpleBoolTest::RunTest(const FString&)
	{
		return RunSimpleOutputTest<bool>(*this, true);
	}

	template<typename DataType>
	bool RunSetInputTest(FAutomationTestBase& Test, DataType ExpectedValue)
	{
		const FName InputName = "MyInput";
		const FName OutputName = "MyOutput";
		constexpr FSampleRate SampleRate = 48000;
		constexpr int32 NumSamplesPerBlock = 128;

		// Build a passthrough graph
		const TUniquePtr<FMetasoundGenerator> Generator =
			BuildPassthroughGraph<DataType>(Test, InputName, OutputName, SampleRate, NumSamplesPerBlock);
		if (!Test.TestNotNull("Generator built", Generator.Get()))
		{
			return false;
		}
		
		// Set the input value
		Generator->SetInputValue<DataType>(InputName, ExpectedValue);

		// Render a block
		{
			TArray<float> Buffer;
			Buffer.Reserve(NumSamplesPerBlock);
			if (!Test.TestEqual(
				"Generated the right number of samples.",
				Generator->OnGenerateAudio(Buffer.GetData(), NumSamplesPerBlock),
				NumSamplesPerBlock))
			{
				return false;
			}
		}

		// Check the output
		const TOptional<TDataReadReference<DataType>> OutputReadRef = Generator->GetOutputReadReference<DataType>(OutputName);
		if (!Test.TestTrue("Got the read ref", OutputReadRef.IsSet()))
		{
			return false;
		}
		if (!Test.TestEqual("Input was passed through", *OutputReadRef.GetValue(), ExpectedValue))
		{
			return false;
		}

		return true;
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorSetInputFloatTest,
	"Audio.Metasound.Generator.SetInput.Float",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorSetInputFloatTest::RunTest(const FString&)
	{
		return RunSetInputTest<float>(*this, 123.456f);
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorSetInputIntTest,
	"Audio.Metasound.Generator.SetInput.Int",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorSetInputIntTest::RunTest(const FString&)
	{
		return RunSetInputTest<int32>(*this, 123456);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorSetInputBoolTest,
	"Audio.Metasound.Generator.SetInput.Bool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorSetInputBoolTest::RunTest(const FString&)
	{
		return RunSetInputTest<bool>(*this, true);
	}

	template<typename DataType>
	bool RunApplyToInputValueTest(FAutomationTestBase& Test, TFunctionRef<void(DataType&)> InFunc, DataType ExpectedValue)
	{
		const FName InputName = "MyInput";
		const FName OutputName = "MyOutput";
		constexpr FSampleRate SampleRate = 48000;
		constexpr int32 NumSamplesPerBlock = 128;

		// Build a passthrough graph
		const TUniquePtr<FMetasoundGenerator> Generator =
			BuildPassthroughGraph<DataType>(Test, InputName, OutputName, SampleRate, NumSamplesPerBlock);
		if (!Test.TestNotNull("Generator built", Generator.Get()))
		{
			return false;
		}
		
		// Set the input value
		Generator->ApplyToInputValue(InputName, InFunc);

		// Render a block
		{
			TArray<float> Buffer;
			Buffer.Reserve(NumSamplesPerBlock);
			if (!Test.TestEqual(
				"Generated the right number of samples.",
				Generator->OnGenerateAudio(Buffer.GetData(), NumSamplesPerBlock),
				NumSamplesPerBlock))
			{
				return false;
			}
		}

		// Check the output
		const TOptional<TDataReadReference<DataType>> OutputReadRef = Generator->GetOutputReadReference<DataType>(OutputName);
		if (!Test.TestTrue("Got the read ref", OutputReadRef.IsSet()))
		{
			return false;
		}
		if (!Test.TestEqual("Input was passed through", *OutputReadRef.GetValue(), ExpectedValue))
		{
			return false;
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorApplyToInputFloatTest,
	"Audio.Metasound.Generator.ApplyToInput.Float",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorApplyToInputFloatTest::RunTest(const FString&)
	{
		using DataType = float;
		constexpr DataType ExpectedValue = 123.456f;
		const TUniqueFunction<void(DataType&)> Fn = [ExpectedValue](DataType& Value)
		{
			Value = ExpectedValue;
		};
		return RunApplyToInputValueTest<DataType>(*this, Fn, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorApplyToInputIntTest,
	"Audio.Metasound.Generator.ApplyToInput.Int",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorApplyToInputIntTest::RunTest(const FString&)
	{
		using DataType = int32;
		constexpr DataType ExpectedValue = 123456;
		const TUniqueFunction<void(DataType&)> Fn = [ExpectedValue](DataType& Value)
		{
			Value = ExpectedValue;
		};
		return RunApplyToInputValueTest<DataType>(*this, Fn, ExpectedValue);
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundGeneratorApplyToInputBoolTest,
	"Audio.Metasound.Generator.ApplyToInput.Bool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorApplyToInputBoolTest::RunTest(const FString&)
	{
		using DataType = bool;
		constexpr DataType ExpectedValue = true;
		const TUniqueFunction<void(DataType&)> Fn = [ExpectedValue](DataType& Value)
		{
			Value = ExpectedValue;
		};
		return RunApplyToInputValueTest<DataType>(*this, Fn, ExpectedValue);
	}
}

#endif