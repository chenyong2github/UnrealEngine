// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynthComponentMetasoundExample.h"

#include "MetasoundAudioFormats.h"
#include "MetasoundEnvironment.h"
#include "MetasoundGenerator.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"

#include "MetasoundOscNode.h"
#include "MetasoundAudioMultiplyNode.h"
#include "MetasoundADSRNode.h"
#include "MetasoundPeriodicBopNode.h"
#include "MetasoundOutputNode.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundGraph.h"
#include "MetasoundFrequency.h"
#include "MetasoundTime.h"

struct FSynthComponentMetasoundExampleGraph
{
	TUniquePtr<Metasound::FGraph> Graph;
	TArray<TUniquePtr<Metasound::INode>> Nodes;
};

static TUniquePtr<FSynthComponentMetasoundExampleGraph> CreateSynthComponentMetasoundExampleGraph()
{
	using namespace Metasound;
	
	TUniquePtr<TInputNode<FFloatTime>> BopPeriodInputNode = MakeUnique<TInputNode<FFloatTime>>(TEXT("Input Bop Period Node"), TEXT("BopPeriod"), 1.f, ETimeResolution::Seconds);

	TUniquePtr<TInputNode<FFrequency>> FrequencyInputNode = MakeUnique<TInputNode<FFrequency>>(TEXT("Input Frequency Node"), TEXT("Frequency"), 100.f, EFrequencyResolution::Hertz);

	TUniquePtr<FPeriodicBopNode> BopNode = MakeUnique<FPeriodicBopNode>(TEXT("Bop"), 1.f);

	TUniquePtr<FOscNode> OscNode = MakeUnique<FOscNode>(TEXT("Osc"), 100.f);

	TUniquePtr<FADSRNode> ADSRNode = MakeUnique<FADSRNode>(TEXT("ADSR"), 10.f /* AttackMs */, 10.f /* DecayMs */, 50.f /* SustainMs */, 10.f /* ReleaseMs */);

	TUniquePtr<FAudioMultiplyNode> MultiplyNode = MakeUnique<FAudioMultiplyNode>(TEXT("Multiply"));

	TUniquePtr<TOutputNode<FAudioBuffer>> OutputNode = MakeUnique<TOutputNode<FAudioBuffer>>(TEXT("Output Audio Node"), TEXT("Audio"));

	TUniquePtr<FGraph> Graph = MakeUnique<FGraph>(TEXT("Graph"));

	// Add rate of bop.
	Graph->AddDataEdge(*BopPeriodInputNode, TEXT("BopPeriod"), *BopNode, TEXT("Period"));
	// Add frequency controls
	Graph->AddDataEdge(*FrequencyInputNode, TEXT("Frequency"), *OscNode, TEXT("Frequency"));

	Graph->AddDataEdge(*BopNode, TEXT("Bop"), *ADSRNode, TEXT("Bop"));

	// Hookup multiply nodes
	Graph->AddDataEdge(*OscNode, TEXT("Audio"), *MultiplyNode, TEXT("InputBuffer1"));
	Graph->AddDataEdge(*ADSRNode, TEXT("Envelope"), *MultiplyNode, TEXT("InputBuffer2"));

	// Route to output.
	Graph->AddDataEdge(*MultiplyNode, TEXT("Audio"), *OutputNode, TEXT("Audio"));

	Graph->AddInputDataDestination(*BopPeriodInputNode, TEXT("BopPeriod"));
	Graph->AddInputDataDestination(*FrequencyInputNode, TEXT("Frequency"));
	Graph->AddOutputDataSource(*OutputNode, TEXT("Audio"));

	// Add everything to this handy-dandy container.
	TUniquePtr<FSynthComponentMetasoundExampleGraph> ExampleGraph = MakeUnique<FSynthComponentMetasoundExampleGraph>();

	ExampleGraph->Graph = MoveTemp(Graph);
	ExampleGraph->Nodes.Add(MoveTemp(BopPeriodInputNode));
	ExampleGraph->Nodes.Add(MoveTemp(FrequencyInputNode));
	ExampleGraph->Nodes.Add(MoveTemp(BopNode));
	ExampleGraph->Nodes.Add(MoveTemp(OscNode));
	ExampleGraph->Nodes.Add(MoveTemp(ADSRNode));
	ExampleGraph->Nodes.Add(MoveTemp(MultiplyNode));
	ExampleGraph->Nodes.Add(MoveTemp(OutputNode));

	return MoveTemp(ExampleGraph);
}

USynthComponentMetasoundExample::USynthComponentMetasoundExample(const FObjectInitializer& ObjInitializer)
:	Super(ObjInitializer)
{
	NumChannels = 1;
}

USynthComponentMetasoundExample::~USynthComponentMetasoundExample()
{
}

void USynthComponentMetasoundExample::SetFloatParameter(const FString& InName, float InValue)
{
	using namespace Metasound;

	FloatParameters.Add(InName, InValue);

	if (Generator.IsValid())
	{
		FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());

		SynthCommand([=]() 
			{
				MetasoundGenerator->SetInputValue(InName, InValue);
			}
		);
	}
}


#if WITH_EDITOR
void USynthComponentMetasoundExample::PostEditChangeProperty(struct FPropertyChangedEvent & PropertyChangedEvent)
{
	using namespace Metasound;
	using FFloatMap = TMap<FString, float>;
	using FStringFloatPair = TMap<FString, float>::ElementType;

	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName FloatParamFName = GET_MEMBER_NAME_CHECKED(USynthComponentMetasoundExample, FloatParameters);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (Generator.IsValid())
		{
			if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
			{
				const FName& Name = PropertyThatChanged->GetFName();
				if (Name == FloatParamFName)
				{
					FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());

					SynthCommand([=]() 
						{
							for (const FStringFloatPair& Pair : FloatParameters)
							{
								MetasoundGenerator->SetInputValue(Pair.Get<0>(), Pair.Get<1>());
							}
						}
					);
				}
			}
		}
	}

}
#endif

void USynthComponentMetasoundExample::SetGraphOperator(FOperatorUniquePtr InGraphOperator, const FOperatorSettings& InSettings, const FString& InOutputAudioName)
{
	using namespace Metasound;

	if (InGraphOperator.IsValid())
	{
		FDataReferenceCollection Outputs = InGraphOperator->GetOutputs();
		FAudioBufferReadRef Audio = Outputs.GetDataReadReferenceOrConstruct<FAudioBuffer>(InOutputAudioName, 256);
		FBopReadRef OnFinished = FBopReadRef::CreateNew(InSettings); // unused in this example
		
		// Multichannel version:
		//NumChannels = Audio->GetNumChannels();

		// Single channel version:
		NumChannels = 1;

		FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(InGraphOperator),
			Audio,
			OnFinished
		};

		Generator = MakeShared<Metasound::FMetasoundGenerator, ESPMode::ThreadSafe>(MoveTemp(InitParams));
	}
	else
	{
		NumChannels = 0;
		Generator.Reset();
	}
}

bool USynthComponentMetasoundExample::PushGraphOperator(FOperatorUniquePtr InGraphOperator, const FOperatorSettings& InOperatorSettings, const FString& InOutputAudioName)
{
	using namespace Metasound;

	if (!Generator.IsValid())
	{
		return false;
	}

	FDataReferenceCollection Outputs = InGraphOperator->GetOutputs();
	FAudioBufferReadRef AudioReadRef = Outputs.GetDataReadReferenceOrConstruct<FAudioBuffer>(InOutputAudioName, 256);
	
	// On Finished isn't used in this example.
	FBopReadRef OnFinishedRef = FBopReadRef::CreateNew(InOperatorSettings);

	FMetasoundGeneratorInitParams GeneratorInitParams =
	{
		MoveTemp(InGraphOperator),
		AudioReadRef,
		OnFinishedRef
	};

	
	GeneratorInitParams.GraphOutputAudioRef = Outputs.GetDataReadReferenceOrConstruct<FAudioBuffer>(InOutputAudioName, 256);
	GeneratorInitParams.GraphOperator = MoveTemp(InGraphOperator);


	if (Generator->UpdateGraphOperator(MoveTemp(GeneratorInitParams)))
	{
		return true;
	}

	return false;
}

ISoundGeneratorPtr USynthComponentMetasoundExample::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	using namespace Metasound;

	// Build everything

	TUniquePtr<FSynthComponentMetasoundExampleGraph> ExampleGraph = CreateSynthComponentMetasoundExampleGraph();

	FOperatorBuilder Builder(FOperatorBuilderSettings::GetDefaultSettings());

	FOperatorSettings Settings(InParams.SampleRate, InParams.NumFramesPerCallback);
	FMetasoundEnvironment Environment;
	TArray<IOperatorBuilder::FBuildErrorPtr> Errors;
	FOperatorUniquePtr Operator = Builder.BuildGraphOperator(*ExampleGraph->Graph, Settings, Environment, Errors);

	SetGraphOperator(MoveTemp(Operator), Settings, TEXT("Audio"));

	return Generator;
}

