// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Interfaces/IPluginManager.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundSource.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EngineTestMetasoundSourcePrivate
{
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

	Metasound::Frontend::FNodeHandle AddNode(Metasound::Frontend::IGraphController& InGraph, const Metasound::FNodeClassName& InClassName)
	{
		FMetasoundFrontendClass NodeClass;
		ensure(Metasound::Frontend::ISearchEngine::Get().FindClassWithHighestVersion(InClassName, NodeClass));
		Metasound::Frontend::FNodeHandle Node = InGraph.AddNode(NodeClass.Metadata);
		check(Node->IsValid());
		return Node;
	}

	FMetasoundFrontendDocument CreateMetaSoundMonoSourceDocument()
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		FMetasoundFrontendDocument Document;
		Document.InterfaceVersions.Add(FMetasoundFrontendVersion{"MonoSource", {1, 1}});

		Document.RootGraph.Metadata.SetClassName(FMetasoundFrontendClassName { "Namespace", "Unit Test Node", *LexToString(FGuid::NewGuid()) });
		Document.RootGraph.Metadata.SetType(EMetasoundFrontendClassType::Graph);

		FDocumentHandle DocumentHandle = IDocumentController::CreateDocumentHandle(Document);
		Metasound::Frontend::FGraphHandle RootGraph = DocumentHandle->GetRootGraph();
		check(RootGraph->IsValid());

		// Input on play
		FMetasoundFrontendClassInput OnPlayInput;
		OnPlayInput.Name = TEXT("On Play");
		OnPlayInput.TypeName = GetMetasoundDataTypeName<FTrigger>();
		OnPlayInput.VertexID = FGuid::NewGuid();

		FNodeHandle OnPlayInputNode = RootGraph->AddInputVertex(OnPlayInput);
		check(OnPlayInputNode->IsValid());

		// Input Frequency
		FMetasoundFrontendClassInput FrequencyInput;
		FrequencyInput.Name = TEXT("Frequency");
		FrequencyInput.TypeName = GetMetasoundDataTypeName<float>();
		FrequencyInput.VertexID = FGuid::NewGuid();
		FrequencyInput.DefaultLiteral.Set(100.f);

		FNodeHandle FrequencyInputNode = RootGraph->AddInputVertex(FrequencyInput);
		check(FrequencyInputNode->IsValid());


		// Output Is Finished
		FMetasoundFrontendClassOutput IsFinishedOutput;
		IsFinishedOutput.Name = TEXT("On Finished");
		IsFinishedOutput.TypeName = Metasound::GetMetasoundDataTypeName<FTrigger>();
		IsFinishedOutput.VertexID = FGuid::NewGuid();
		FNodeHandle IsFinishedOutputNode = RootGraph->AddOutputVertex(IsFinishedOutput);
		check(IsFinishedOutputNode->IsValid());

		// Output Audio
		FMetasoundFrontendClassOutput AudioOutput;
		AudioOutput.Name = TEXT("Audio:0");
		AudioOutput.TypeName = Metasound::GetMetasoundDataTypeName<FAudioBuffer>();
		AudioOutput.VertexID = FGuid::NewGuid();
		
		FNodeHandle OutputAudioNode = RootGraph->AddOutputVertex(AudioOutput);
		check(OutputAudioNode->IsValid());

		// osc node 
		FNodeHandle OscNode = AddNode(*RootGraph, {TEXT("UE"), TEXT("Sine"), TEXT("Audio")});

		// Make connections:

		// frequency input "Frequency" -> oscillator "Frequency"
		FOutputHandle OutputToConnect = FrequencyInputNode->GetOutputWithVertexName("Frequency");
		FInputHandle InputToConnect = OscNode->GetInputWithVertexName("Frequency");
		ensure(InputToConnect->Connect(*OutputToConnect));

		// oscillator to output
		OutputToConnect = OscNode->GetOutputWithVertexName("Audio");
		InputToConnect = OutputAudioNode->GetInputWithVertexName("Audio:0");
		ensure(InputToConnect->Connect(*OutputToConnect));

		return Document;
	}
}

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

// This test imports a metasound source from a JSON file, attempts to play it, and exports it as a json.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioMetasoundSourceTest, "Audio.Metasound.BuildAndPlayMetasoundSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioMetasoundSourceTest::RunTest(const FString& Parameters)
{
	UMetaSoundSource* MetaSoundSource = NewObject<UMetaSoundSource>(GetTransientPackage(), FName(*LexToString(FGuid::NewGuid())));;
	if (ensure(nullptr != MetaSoundSource))
	{
		MetaSoundSource->SetDocument(EngineTestMetasoundSourcePrivate::CreateMetaSoundMonoSourceDocument());

		if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
		{

			UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(MetaSoundSource);
			AddErrorIfFalse(AudioComponent != nullptr, "Couldn't create audio component!");

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

// TODO: add separate JSON test.


#endif //WITH_DEV_AUTOMATION_TESTS
