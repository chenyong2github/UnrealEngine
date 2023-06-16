// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "IAudioParameterInterfaceRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundSource.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "../../MetasoundGraphCore/Public/MetasoundDataReference.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EngineTestMetasoundSourcePrivate
{
	struct FInitTestBuilderOutput
	{
		FMetaSoundBuilderNodeOutputHandle OnPlayOutput;
		FMetaSoundBuilderNodeInputHandle OnFinishedInput;
		TArray<FMetaSoundBuilderNodeInputHandle> AudioOutNodeInputs;
	};

	static FString GetPluginContentDirectory()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Metasound"));
		if (ensure(Plugin.IsValid()))
		{
			return Plugin->GetContentDir();
		}
		return FString();
	}
	static FString GetPathToTestFilesDir()
	{
		FString OutPath =  FPaths::Combine(GetPluginContentDirectory(), TEXT("Test"));

		OutPath = FPaths::ConvertRelativePathToFull(OutPath);
		FPaths::NormalizeDirectoryName(OutPath);
		
		return OutPath;
	}

	static FString GetPathToGeneratedFilesDir()
	{
		FString OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Metasounds"));

		OutPath = FPaths::ConvertRelativePathToFull(OutPath);
		FPaths::NormalizeDirectoryName(OutPath);
		
		return OutPath;
	}

	static FString GetPathToGeneratedAssetsDir()
	{
		FString OutPath = TEXT("/Game/Metasound/Generated/");
		FPaths::NormalizeDirectoryName(OutPath);
		return OutPath;
	}

	Metasound::Frontend::FNodeHandle AddNode(Metasound::Frontend::IGraphController& InGraph, const Metasound::FNodeClassName& InClassName, int32 InMajorVersion)
	{
		Metasound::Frontend::FNodeHandle Node = Metasound::Frontend::INodeController::GetInvalidHandle();
		FMetasoundFrontendClass NodeClass;
		if (ensure(Metasound::Frontend::ISearchEngine::Get().FindClassWithHighestMinorVersion(InClassName, InMajorVersion, NodeClass)))
		{
			Node = InGraph.AddNode(NodeClass.Metadata);
			check(Node->IsValid());
		}
		return Node;
	}

	UMetaSoundSourceBuilder& CreateMetaSoundSourceBuilder(EMetaSoundOutputAudioFormat OutputFormat, bool bIsOneShot, FInitTestBuilderOutput& Output)
	{
		using namespace Audio;
		using namespace Metasound;
		using namespace Metasound::Engine;
		using namespace Metasound::Frontend;

		EMetaSoundBuilderResult Result;
		UMetaSoundSourceBuilder* Builder = UMetaSoundBuilderSubsystem::GetChecked().CreateSourceBuilder(
			"Unit Test Graph Builder",
			Output.OnPlayOutput,
			Output.OnFinishedInput,
			Output.AudioOutNodeInputs,
			Result,
			OutputFormat,
			bIsOneShot);
		checkf(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to create MetaSoundSourceBuilder"));

		return *Builder;
	}

	UMetaSoundSourceBuilder& CreateMetaSoundMonoSourceSinGenBuilder(
		FAutomationTestBase& Test,
		FMetaSoundBuilderNodeInputHandle* GenInputNodeFreq = nullptr,
		FMetaSoundBuilderNodeInputHandle* MonoOutNodeInput = nullptr,
		float InDefaultFreq = 100.f)
	{
		using namespace EngineTestMetasoundSourcePrivate;
		using namespace Metasound;
		using namespace Metasound::Engine;
		using namespace Metasound::Frontend;

		constexpr EMetaSoundOutputAudioFormat OutputFormat = EMetaSoundOutputAudioFormat::Mono;
		constexpr bool bIsOneShot = false;
		FInitTestBuilderOutput Output;
		UMetaSoundSourceBuilder& Builder = CreateMetaSoundSourceBuilder(EMetaSoundOutputAudioFormat::Mono, bIsOneShot, Output);

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		if (MonoOutNodeInput)
		{
			*MonoOutNodeInput = { };
		}

		// Input on Play
		const FMetaSoundNodeHandle OnPlayOutputNode = Builder.FindGraphInputNode(SourceInterface::Inputs::OnPlay, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && OnPlayOutputNode.IsSet(), TEXT("Failed to create MetaSound OnPlay input"));

		// Input Frequency
		FMetasoundFrontendLiteral DefaultFreq;
		DefaultFreq.Set(InDefaultFreq);
		const FMetaSoundBuilderNodeOutputHandle FrequencyNodeOutput = Builder.AddGraphInputNode("Frequency", GetMetasoundDataTypeName<float>(), DefaultFreq, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && FrequencyNodeOutput.IsSet(), TEXT("Failed to create new MetaSound graph input"));

		// Sine Oscillator Node
		const FMetaSoundNodeHandle OscNode = Builder.AddNodeByClassName({ "UE", "Sine", "Audio" }, 1, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && OscNode.IsSet(), TEXT("Failed to create new MetaSound node by class name"));

		// Make connections:
		const FMetaSoundBuilderNodeInputHandle OscNodeFrequencyInput = Builder.FindNodeInputByName(OscNode, "Frequency", Result);
		if (GenInputNodeFreq)
		{
			*GenInputNodeFreq = OscNodeFrequencyInput;
		}
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && OscNodeFrequencyInput.IsSet(), TEXT("Failed to find Sine Oscillator node input 'Frequency'"));

		const FMetaSoundBuilderNodeOutputHandle OscNodeAudioOutput = Builder.FindNodeOutputByName(OscNode, "Audio", Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && OscNodeAudioOutput.IsSet(), TEXT("Failed to find Sine Oscillator node output 'Audio'"));

		// Frequency input "Frequency" -> oscillator "Frequency"
		Builder.ConnectNodes(FrequencyNodeOutput, OscNodeFrequencyInput, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to connect 'Frequency' input to node input 'Frequency'"));

		// Oscillator to Output Node
		Test.AddErrorIfFalse(Output.AudioOutNodeInputs.Num() == 1, TEXT("Should only ever have one output node for mono"));
		if (MonoOutNodeInput)
		{
			*MonoOutNodeInput = Output.AudioOutNodeInputs.Last();
		}

		Builder.ConnectNodes(OscNodeAudioOutput, Output.AudioOutNodeInputs.Last(), Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to connect 'Audio' Sine Oscillator output to MetaSound graph's 'Mono Output'"));

		return Builder;
	}

	FMetasoundFrontendDocument CreateMetaSoundMonoSourceDocument()
	{
		using namespace Audio;
		using namespace Metasound;
		using namespace Metasound::Engine;
		using namespace Metasound::Frontend;

		FMetasoundFrontendDocument Document;

		Document.RootGraph.Metadata.SetClassName(FMetasoundFrontendClassName { "Namespace", "Unit Test Node", *LexToString(FGuid::NewGuid()) });
		Document.RootGraph.Metadata.SetType(EMetasoundFrontendClassType::Graph);

		FDocumentHandle DocumentHandle = IDocumentController::CreateDocumentHandle(Document);
		FGraphHandle RootGraph = DocumentHandle->GetRootGraph();
		check(RootGraph->IsValid());

		// Add default source & mono interface members (OnPlay, OnFinished & Mono Out)
		FModifyRootGraphInterfaces InterfaceTransform(
		{ },
		{
			SourceInterface::GetVersion(),
			SourceOneShotInterface::GetVersion(),
			OutputFormatMonoInterface::GetVersion()
		}); 
		InterfaceTransform.Transform(DocumentHandle);

		// Input on Play
		FNodeHandle OnPlayOutputNode = RootGraph->GetInputNodeWithName(SourceInterface::Inputs::OnPlay);
		check(OnPlayOutputNode->IsValid());

		// Input Frequency
		FMetasoundFrontendClassInput FrequencyInput;
		FrequencyInput.Name = "Frequency";
		FrequencyInput.TypeName = GetMetasoundDataTypeName<float>();
		FrequencyInput.VertexID = FGuid::NewGuid();
		FrequencyInput.DefaultLiteral.Set(100.f);
		FNodeHandle FrequencyInputNode = RootGraph->AddInputVertex(FrequencyInput);
		check(FrequencyInputNode->IsValid());

		// Output On Finished
		FNodeHandle OnFinishedOutputNode = RootGraph->GetOutputNodeWithName(SourceOneShotInterface::Outputs::OnFinished);
		check(OnFinishedOutputNode->IsValid());

		// Output Audio
		FNodeHandle AudioOutputNode = RootGraph->GetOutputNodeWithName(OutputFormatMonoInterface::Outputs::MonoOut);
		check(AudioOutputNode->IsValid());

		// osc node
		FNodeHandle OscNode = AddNode(*RootGraph, { "UE", "Sine", "Audio" }, 1);

		// Make connections:

		// frequency input "Frequency" -> oscillator "Frequency"
		FOutputHandle OutputToConnect = FrequencyInputNode->GetOutputWithVertexName("Frequency");
		FInputHandle InputToConnect = OscNode->GetInputWithVertexName("Frequency");
		ensure(InputToConnect->Connect(*OutputToConnect));

		// oscillator to output
		OutputToConnect = OscNode->GetOutputWithVertexName("Audio");
		InputToConnect = AudioOutputNode->GetInputWithVertexName(OutputFormatMonoInterface::Outputs::MonoOut);
		ensure(InputToConnect->Connect(*OutputToConnect));

		return Document;
	}
} // EngineTestMetasoundSourcePrivate

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FAudioComponentPlayLatentCommand, UAudioComponent*, AudioComponent);

bool FAudioComponentPlayLatentCommand::Update()
{
	if (AudioComponent)
	{
		AudioComponent->Play();
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FAudioComponentStopLatentCommand, UAudioComponent*, AudioComponent);

bool FAudioComponentStopLatentCommand::Update()
{
	if (AudioComponent)
	{
		AudioComponent->Stop();
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FMetaSoundSourceBuilderAuditionLatentCommand, UMetaSoundSourceBuilder*, Builder, UAudioComponent*, AudioComponent, bool, bEnableLiveUpdates);

bool FMetaSoundSourceBuilderAuditionLatentCommand::Update()
{
	if (Builder && AudioComponent)
	{
		Builder->Audition(nullptr, AudioComponent, { }, true);
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FMetaSoundSourceBuilderDisconnectInputLatentCommand, FAutomationTestBase&, Test, UMetaSoundBuilderBase*, Builder, FMetaSoundBuilderNodeInputHandle, InputToDisconnect);

bool FMetaSoundSourceBuilderDisconnectInputLatentCommand::Update()
{
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	if (Builder)
	{
		Builder->DisconnectNodeInput(InputToDisconnect, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to disconnect MetaSound node's input"));
	}

	return Result == EMetaSoundBuilderResult::Succeeded;
}

DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(FMetaSoundSourceBuilderSetLiteralLatentCommand, FAutomationTestBase&, Test, UMetaSoundBuilderBase*, Builder, FMetaSoundBuilderNodeInputHandle, NodeInput, FMetasoundFrontendLiteral, NewValue);

bool FMetaSoundSourceBuilderSetLiteralLatentCommand::Update()
{
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	if (Builder)
	{
		Builder->SetNodeInputDefault(NodeInput, NewValue, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to disconnect MetaSound node's input"));
	}

	return Result == EMetaSoundBuilderResult::Succeeded;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FMetaSoundSourceBuilderRemoveNodeDefaultLiteralLatentCommand, FAutomationTestBase&, Test, UMetaSoundBuilderBase*, Builder, FMetaSoundBuilderNodeInputHandle, NodeInput);

bool FMetaSoundSourceBuilderRemoveNodeDefaultLiteralLatentCommand::Update()
{
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	if (Builder)
	{
		Builder->RemoveNodeInputDefault(NodeInput, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to disconnect MetaSound node's input"));
	}

	return Result == EMetaSoundBuilderResult::Succeeded;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FMetaSoundSourceBuilderCreateAndConnectTriGeneratorNodeLatentCommand, FAutomationTestBase&, Test, UMetaSoundSourceBuilder*, Builder, FMetaSoundBuilderNodeInputHandle, AudioOutNodeInput);

bool FMetaSoundSourceBuilderCreateAndConnectTriGeneratorNodeLatentCommand::Update()
{
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	if (Builder)
	{
		// Tri Oscillator Node
		FMetaSoundNodeHandle TriNode = Builder->AddNodeByClassName({ "UE", "Triangle", "Audio" }, 1, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded && TriNode.IsSet(), TEXT("Failed to create node by class name 'UE:Triangle:Audio"));

		FMetaSoundBuilderNodeOutputHandle TriNodeAudioOutput = Builder->FindNodeOutputByName(TriNode, "Audio", Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to find Triangle Oscillator node output 'Audio'"));

		Builder->ConnectNodes(TriNodeAudioOutput, AudioOutNodeInput, Result);
		Test.AddErrorIfFalse(Result == EMetaSoundBuilderResult::Succeeded, TEXT("Failed to connect 'Audio' Triangle Oscillator output to MetaSound graph's 'Mono Output'"));
	}

	return Result == EMetaSoundBuilderResult::Succeeded;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FAudioComponentRemoveFromRootLatentCommand, UAudioComponent*, AudioComponent);

bool FAudioComponentRemoveFromRootLatentCommand::Update()
{
	if (AudioComponent)
	{
		AudioComponent->RemoveFromRoot();
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FBuilderRemoveFromRootLatentCommand, UMetaSoundBuilderBase*, Builder);

bool FBuilderRemoveFromRootLatentCommand::Update()
{
	if (Builder)
	{
		Builder->RemoveFromRoot();
		return true;
	}
	return false;
}


// This test creates a MetaSound from the legacy controller document editing system and attempts to play it directly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioMetasoundSourceTest, "Audio.Metasound.PlayMetasoundSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioMetasoundSourceTest::RunTest(const FString& Parameters)
{
	UMetaSoundSource* MetaSoundSource = NewObject<UMetaSoundSource>(GetTransientPackage(), FName(*LexToString(FGuid::NewGuid())));;
	if (ensure(nullptr != MetaSoundSource))
	{
		MetaSoundSource->SetDocument(EngineTestMetasoundSourcePrivate::CreateMetaSoundMonoSourceDocument());

		if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
		{

			UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(MetaSoundSource);
			AddErrorIfFalse(AudioComponent != nullptr, "Failed to create audio component");

			if (AudioComponent)
			{
				AudioComponent->bIsUISound = true;
				AudioComponent->bAllowSpatialization = false;
				AudioComponent->SetVolumeMultiplier(1.0f);
				AudioComponent->AddToRoot();

				ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentPlayLatentCommand(AudioComponent));
				ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.f));
				ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentStopLatentCommand(AudioComponent));
				ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
				ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentRemoveFromRootLatentCommand(AudioComponent));
			}
		}
	}

	return true;
 }

// This test creates a MetaSound source from a SourceBuilder, adds a simple sine tone generator with a connected graph input frequency, and attempts to audition it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioMetasoundSourceBuilderTest, "Audio.Metasound.AuditionMetasoundSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioMetasoundSourceBuilderTest::RunTest(const FString& Parameters)
{
	using namespace EngineTestMetasoundSourcePrivate;
// 	using namespace Metasound;
// 	using namespace Metasound::Engine;
// 	using namespace Metasound::Frontend;

	FMetaSoundBuilderNodeInputHandle MonoOutNodeInput;
	UMetaSoundSourceBuilder& Builder = CreateMetaSoundMonoSourceSinGenBuilder(*this, nullptr, &MonoOutNodeInput);
	Builder.AddToRoot();

	if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
	{
		constexpr bool bAddToRegistry = false;
		TScriptInterface<IMetaSoundDocumentInterface> BuiltDocumentInterface = Builder.Build(nullptr, FMetaSoundBuilderOptions { "BuildAndPlayMetasoundSource", bAddToRegistry });
		UMetaSoundSource* MetaSoundSource = CastChecked<UMetaSoundSource>(BuiltDocumentInterface.GetObject());
		UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(MetaSoundSource);
		AddErrorIfFalse(AudioComponent != nullptr, "Failed to create audio component");

		if (AudioComponent)
		{
			AudioComponent->bAutoActivate = false;
			AudioComponent->bIsUISound = true; // play while "paused"
			AudioComponent->AudioDeviceID = AudioDevice->DeviceID;

			AudioComponent->bAllowSpatialization = false;
			AudioComponent->SetVolumeMultiplier(1.0f);
			AudioComponent->AddToRoot();

			constexpr bool bEnableLiveUpdate = false;
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.f));
			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentStopLatentCommand(AudioComponent));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
			ADD_LATENT_AUTOMATION_COMMAND(FBuilderRemoveFromRootLatentCommand(&Builder));
			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentRemoveFromRootLatentCommand(AudioComponent));
		}
	}

	return true;
}

// This test creates a MetaSound source from a SourceBuilder, adds a simple sine tone generator with a connected graph input frequency, attempts to audition it, and then switches to a new tri tone generator during playback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioMetasoundSourceBuilderLiveUpdateNode, "Audio.Metasound.LiveUpdateNodeMetaSoundSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioMetasoundSourceBuilderLiveUpdateNode::RunTest(const FString& Parameters)
{
	using namespace EngineTestMetasoundSourcePrivate;
	using namespace Metasound;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	FMetaSoundBuilderNodeInputHandle MonoOutNodeInput;
	UMetaSoundSourceBuilder& Builder = CreateMetaSoundMonoSourceSinGenBuilder(*this, nullptr, &MonoOutNodeInput);
	Builder.AddToRoot();

	if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
	{
		UAudioComponent* AudioComponent = NewObject<UAudioComponent>();
		AddErrorIfFalse(AudioComponent != nullptr, "Failed to create audio component");

		if (AudioComponent)
		{
			AudioComponent->bAutoActivate = false;
			AudioComponent->bIsUISound = true; // play while "paused"
			AudioComponent->AudioDeviceID = AudioDevice->DeviceID;

			AudioComponent->bAllowSpatialization = false;
			AudioComponent->SetVolumeMultiplier(1.0f);
			AudioComponent->AddToRoot();

			constexpr bool bEnableLiveUpdate = true;
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.f));

			// Disconnects graph audio output from existing sinosc output
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderDisconnectInputLatentCommand(*this, &Builder, MonoOutNodeInput));

			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderCreateAndConnectTriGeneratorNodeLatentCommand(*this, &Builder, MonoOutNodeInput));

			// TODO: Once we have the builder hooked up to the dynamic graph backend, remove this and voila!
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));

			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.f));
			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentStopLatentCommand(AudioComponent));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentRemoveFromRootLatentCommand(AudioComponent));
			ADD_LATENT_AUTOMATION_COMMAND(FBuilderRemoveFromRootLatentCommand(&Builder));
		}
	}

	return true;
}

// This test creates a MetaSound source from a SourceBuilder, adds a simple sine tone generator with a connected graph input frequency, attempts to audition it,
// disconnects frequency input, sets the sinosc frequency literal value to a new value, and finally removes the literal value default to have it return to the
// class default.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioMetasoundSourceBuilderLiveUpdateLiteral, "Audio.Metasound.LiveUpdateLiteralMetaSoundSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioMetasoundSourceBuilderLiveUpdateLiteral::RunTest(const FString& Parameters)
{
	using namespace EngineTestMetasoundSourcePrivate;

	FMetaSoundBuilderNodeInputHandle MonoOutNodeInput;
	FMetaSoundBuilderNodeInputHandle GenNodeFreqInput;
	UMetaSoundSourceBuilder& Builder = CreateMetaSoundMonoSourceSinGenBuilder(*this, &GenNodeFreqInput , &MonoOutNodeInput, 220.f);
	Builder.AddToRoot();

	if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
	{
		UAudioComponent* AudioComponent = NewObject<UAudioComponent>();
		AddErrorIfFalse(AudioComponent != nullptr, "Failed to create audio component");

		if (AudioComponent)
		{
			AudioComponent->bAutoActivate = false;
			AudioComponent->bIsUISound = true; // play while "paused"
			AudioComponent->AudioDeviceID = AudioDevice->DeviceID;

			AudioComponent->bAllowSpatialization = false;
			AudioComponent->SetVolumeMultiplier(1.0f);
			AudioComponent->AddToRoot();

			constexpr bool bEnableLiveUpdate = true;
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.25f));

			// TODO: Once we have the builder hooked up to the dynamic graph backend, remove additional calls to
			// FMetaSoundSourceBuilderAuditionLatentCommand. The dynamic graph should automatically make changes audible.

			// Disconnects graph audio output from existing sinosc output
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderDisconnectInputLatentCommand(*this, &Builder, GenNodeFreqInput));
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.25f));

			FName DataTypeName;
			FMetasoundFrontendLiteral NewValue = UMetaSoundBuilderSubsystem::GetChecked().CreateFloatMetaSoundLiteral(880.f, DataTypeName);
			AddErrorIfFalse(DataTypeName == Metasound::GetMetasoundDataTypeName<float>(),
				"Setting MetaSound Float literal returns non-float DataTypeName.");
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderSetLiteralLatentCommand(*this, &Builder, GenNodeFreqInput, NewValue));
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.25f));

			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderRemoveNodeDefaultLiteralLatentCommand(*this, &Builder, GenNodeFreqInput));
			ADD_LATENT_AUTOMATION_COMMAND(FMetaSoundSourceBuilderAuditionLatentCommand(&Builder, AudioComponent, bEnableLiveUpdate));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.25f));

			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentStopLatentCommand(AudioComponent));
			ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.25f));

			ADD_LATENT_AUTOMATION_COMMAND(FAudioComponentRemoveFromRootLatentCommand(AudioComponent));
			ADD_LATENT_AUTOMATION_COMMAND(FBuilderRemoveFromRootLatentCommand(&Builder));
		}
	}

	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
