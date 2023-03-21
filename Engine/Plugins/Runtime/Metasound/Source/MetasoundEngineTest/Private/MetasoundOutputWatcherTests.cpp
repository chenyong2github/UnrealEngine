// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGenerator.h"
#include "MetasoundOutputWatcher.h"
#include "NodeTestGraphBuilder.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Metasound::Test::GeneratorOutputWatcher
{
	using GraphBuilder = FNodeTestGraphBuilder;
	using namespace Frontend;
	using namespace Private;
	
	template<typename DataType>
	TUniquePtr<FMetasoundGenerator> BuildPassthroughGraph(
		FAutomationTestBase& Test,
		const FName& InputName,
		const FName& OutputName,
		const FSampleRate SampleRate,
		const int32 NumSamplesPerBlock,
		FGuid* OutputGuid = nullptr)
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

		if (nullptr != OutputGuid)
		{
			*OutputGuid = OutputNode->GetID();
		}

		// have to add an audio output for the generator to render
		Builder.AddOutput("Audio", Metasound::GetMetasoundDataTypeName<FAudioBuffer>());
		
		return Builder.BuildGenerator(SampleRate, NumSamplesPerBlock);
	}

	template<typename DataType>
	bool RunSimpleOutputWatcherTest(FAutomationTestBase& Test, DataType ExpectedValue, const FName AnalyzerName, const FName ValueOutputName)
	{
		const FName InputName = "MyInput";
		const FName OutputName = "MyOutput";
		constexpr FSampleRate SampleRate = 48000;
		constexpr int32 NumSamplesPerBlock = 128;

		// Build a passthrough graph
		FGuid OutputNodeId;
		const TUniquePtr<FMetasoundGenerator> Generator =
			BuildPassthroughGraph<DataType>(Test, InputName, OutputName, SampleRate, NumSamplesPerBlock, &OutputNodeId);
		if (!Test.TestNotNull("Generator built", Generator.Get()))
		{
			return false;
		}

		// Add an analyzer to the output
		FAnalyzerAddress AnalyzerAddress;
		AnalyzerAddress.DataType = GetMetasoundDataTypeName<DataType>();
		AnalyzerAddress.InstanceID = 1234;
		AnalyzerAddress.OutputName = OutputName;
		AnalyzerAddress.AnalyzerName = AnalyzerName;
		AnalyzerAddress.AnalyzerInstanceID = FGuid::NewGuid();
		AnalyzerAddress.NodeID = OutputNodeId;
		Generator->AddOutputVertexAnalyzer(AnalyzerAddress);

		// Add an output watcher
		FMetasoundOutputWatcher Watcher{ MoveTemp(AnalyzerAddress), Generator->OperatorSettings };

		// listen for changes to the output
		DataType WatchedValue;
		bool ReceivedValue = false;
		const TFunction<void(FName, const FMetaSoundOutput&)> OnChanged =
			[&WatchedValue, &ReceivedValue](FName, const FMetaSoundOutput& Output)
			{
				ReceivedValue = Output.Get<DataType>(WatchedValue);
			};
		
		// Set the input value
		Generator->SetInputValue(InputName, ExpectedValue);

		// Render a block and update the watcher
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
			Watcher.Update(OnChanged);
		}

		// Check the output
		if (!Test.TestTrue("Received a value", ReceivedValue)
			|| !Test.TestEqual("Received the expected value", WatchedValue, ExpectedValue))
		{
			return false;
		}

		// don't set the input and expect no callbacks
		ReceivedValue = false;
		
		// Render a block and update the watcher
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
			Watcher.Update(OnChanged);
		}

		return Test.TestFalse("No more updates", ReceivedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundOutputWatcherForwardValueFloatTest,
	"Audio.Metasound.OutputWatcher.ForwardValue.Float",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundOutputWatcherForwardValueFloatTest::RunTest(const FString&)
	{
		using DataType = float;
		constexpr DataType ExpectedValue = 123.456f;
		return RunSimpleOutputWatcherTest<DataType>(*this, ExpectedValue, "UE.Forward.Float", "Value");
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundOutputWatcherForwardValueIntTest,
	"Audio.Metasound.OutputWatcher.ForwardValue.Int32",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundOutputWatcherForwardValueIntTest::RunTest(const FString&)
	{
		using DataType = int32;
		constexpr DataType ExpectedValue = 123456;
		return RunSimpleOutputWatcherTest<DataType>(*this, ExpectedValue, "UE.Forward.Int32", "Value");
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundOutputWatcherForwardValueBoolTest,
	"Audio.Metasound.OutputWatcher.ForwardValue.Bool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundOutputWatcherForwardValueBoolTest::RunTest(const FString&)
	{
		using DataType = bool;
		constexpr DataType ExpectedValue = true;
		return RunSimpleOutputWatcherTest<DataType>(*this, ExpectedValue, "UE.Forward.Bool", "Value");
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetasoundOutputWatcherForwardValueStringTest,
	"Audio.Metasound.OutputWatcher.ForwardValue.String",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundOutputWatcherForwardValueStringTest::RunTest(const FString&)
	{
		using FDataType = FString;
		const FDataType ExpectedValue{ "you're awesome" };
		return RunSimpleOutputWatcherTest<FDataType>(*this, ExpectedValue, "UE.Forward.String", "Value");
	}
}

#endif