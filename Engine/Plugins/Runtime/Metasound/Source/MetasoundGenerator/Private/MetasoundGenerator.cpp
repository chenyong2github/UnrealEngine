// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "DSP/Dsp.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundOscNode.h"
#include "MetasoundAudioMultiplyNode.h"
#include "MetasoundADSRNode.h"
#include "MetasoundPeriodicBopNode.h"
#include "MetasoundOutputNode.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundGraph.h"
#include "MetasoundFrequency.h"

namespace Metasound
{
	FMetasoundGenerator::FMetasoundGenerator(FOperatorUniquePtr InOperator, const FAudioReadRef& InOperatorReadBuffer)
	:	Operator(MoveTemp(InOperator))
	,	OperatorReadBuffer(InOperatorReadBuffer)
	,	OperatorReadNum(InOperatorReadBuffer->Num())
	,	ExecuteOperator(nullptr)
	{
		if (Operator.IsValid())
		{
			ExecuteOperator = Operator->GetExecuteFunction();
			FDataReferenceCollection Inputs = Operator->GetInputs();

			if (Inputs.ContainsDataWriteReference<FFrequency>(TEXT("Frequency")))
			{
				FrequencyRef = Inputs.GetDataWriteReference<FFrequency>(TEXT("Frequency"));
			}

			if (Inputs.ContainsDataWriteReference<FFloatTime>(TEXT("BopPeriod")))
			{
				BopPeriodRef = Inputs.GetDataWriteReference<FFloatTime>(TEXT("BopPeriod"));
			}
		}
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
	}

	void FMetasoundGenerator::SetFrequency(float InFrequency)
	{
		FrequencyRef->SetHertz(InFrequency);
	}

	void FMetasoundGenerator::SetBopPeriod(float InPeriodInSeconds)
	{
		BopPeriodRef->SetSeconds(InPeriodInSeconds);
	}

	int32 FMetasoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
	{
		if ((NumSamples <= 0) || (nullptr == ExecuteOperator) || (OperatorReadNum <= 0))
		{
			return 0;
		}

		int32 WriteCount = FillWithBuffer(OverflowBuffer, OutAudio, NumSamples);

		if (WriteCount > 0)
		{
			NumSamples -= WriteCount;
			OverflowBuffer.RemoveAtSwap(0 /* Index */, WriteCount /* Count */, false /* bAllowShrinking */);
		}


		while (NumSamples > 0)
		{
			// TODO: figure out good wrapper for this.
			ExecuteOperator(Operator.Get());

			int32 ThisLoopWriteCount = FillWithBuffer(*OperatorReadBuffer, &OutAudio[WriteCount], NumSamples);

			NumSamples -= ThisLoopWriteCount;
			WriteCount += ThisLoopWriteCount;

			if (0 == NumSamples)
			{
				if (ThisLoopWriteCount < OperatorReadBuffer->Num())
				{
					int32 OverflowCount = OperatorReadBuffer->Num() - ThisLoopWriteCount;

					OverflowBuffer.Reset();
					OverflowBuffer.AddUninitialized(OverflowCount);

					FMemory::Memcpy(OverflowBuffer.GetData(), &(OperatorReadBuffer->GetData()[ThisLoopWriteCount]), OverflowCount);
				}
			}
		}

		return WriteCount;
	}

	int32 FMetasoundGenerator::FillWithBuffer(const Audio::AlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples)
	{
		int32 InNum = InBuffer.Num();

		if (InNum > 0)
		{
			if (InNum < MaxNumOutputSamples)
			{
				FMemory::Memcpy(OutAudio, InBuffer.GetData(), InNum * sizeof(float));
				return InNum;
			}
			else
			{
				FMemory::Memcpy(OutAudio, InBuffer.GetData(), MaxNumOutputSamples * sizeof(float));
				return MaxNumOutputSamples;
			}
		}

		return 0;
	}
} /* End namespace Metasound */


USynthComponentMetasoundGenerator::USynthComponentMetasoundGenerator(const FObjectInitializer& ObjInitializer)
:	Super(ObjInitializer)
{
}

USynthComponentMetasoundGenerator::~USynthComponentMetasoundGenerator()
{
}

void USynthComponentMetasoundGenerator::SetBopPeriod(float InBopPeriod)
{
	using namespace Metasound;

	BopPeriod = InBopPeriod;
	if (Generator.IsValid())
	{
		FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());
		MetasoundGenerator->SetBopPeriod(BopPeriod);
	}
}

void USynthComponentMetasoundGenerator::SetFrequency(float InFrequency)
{
	using namespace Metasound;

	Frequency = InFrequency;
	if (Generator.IsValid())
	{
		FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());
		MetasoundGenerator->SetFrequency(Frequency);
	}
}

#if WITH_EDITOR
void USynthComponentMetasoundGenerator::PostEditChangeProperty(struct FPropertyChangedEvent & PropertyChangedEvent)
{
	using namespace Metasound;

	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName FrequencyFName = GET_MEMBER_NAME_CHECKED(USynthComponentMetasoundGenerator, Frequency);
	static const FName BopPeriodFName = GET_MEMBER_NAME_CHECKED(USynthComponentMetasoundGenerator, BopPeriod);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (Generator.IsValid())
		{
			if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
			{
				const FName& Name = PropertyThatChanged->GetFName();
				if (Name == FrequencyFName)
				{
					FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());
					MetasoundGenerator->SetFrequency(Frequency);
				}
				else if (Name == BopPeriodFName)
				{
					FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());
					MetasoundGenerator->SetBopPeriod(BopPeriod);
				}
			}
		}
	}

}
#endif

ISoundGeneratorPtr USynthComponentMetasoundGenerator::CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels)
{
	using namespace Metasound;
	using FDataEdge = Metasound::FDataEdge; // Need to make explicit to disambiguate with existing FDataEdge.
	using FOperatorUniquePtr = TUniquePtr<Metasound::IOperator>;

	TInputNode<FFloatTime> BopPeriodInputNode(TEXT("Input Bop Period Node"), TEXT("BopPeriod"), 1.f, ETimeResolution::Seconds);
	TInputNode<FFrequency> FrequencyInputNode(TEXT("Input Frequency Node"), TEXT("Frequency"), 100.f, EFrequencyResolution::Hertz);

	FPeriodicBopNode BopNode(TEXT("Bop"), 1.f);
	FOscNode OscNode(TEXT("Osc"), 100.f);
	FADSRNode ADSRNode(TEXT("ADSR"), 10.f /* AttackMs */, 10.f /* DecayMs */, 50.f /* SustainMs */, 10.f /* ReleaseMs */);
	FAudioMultiplyNode MultiplyNode(TEXT("Multiply"));

	TOutputNode<FAudioBuffer> OutputNode(TEXT("Output Audio Node"), TEXT("Audio"));

	FGraph Graph(TEXT("Graph"));

	// Add rate of bop.
	Graph.AddDataEdge(BopPeriodInputNode, TEXT("BopPeriod"), BopNode, TEXT("Period"));
	// Add frequency controls
	Graph.AddDataEdge(FrequencyInputNode, TEXT("Frequency"), OscNode, TEXT("Frequency"));

	// TODO: maybe nodes should have name accessors to inputs and outputs instead of an array?
	Graph.AddDataEdge(BopNode, TEXT("Bop"), ADSRNode, TEXT("Bop"));

	// Hookup multiply nodes
	Graph.AddDataEdge(OscNode, TEXT("Audio"), MultiplyNode, TEXT("InputBuffer1"));
	Graph.AddDataEdge(ADSRNode, TEXT("Envelope"), MultiplyNode, TEXT("InputBuffer2"));

	// Route to output.
	Graph.AddDataEdge(MultiplyNode, TEXT("Audio"), OutputNode, TEXT("Audio"));

	Graph.AddInputDataDestination(BopPeriodInputNode, TEXT("BopPeriod"));
	Graph.AddInputDataDestination(FrequencyInputNode, TEXT("Frequency"));
	Graph.AddOutputDataSource(OutputNode, TEXT("Audio"));


	// Build everything
	
	FOperatorSettings Settings(InSampleRate, 256);
	FOperatorBuilder Builder(Settings);

	TArray<IOperatorBuilder::FBuildErrorPtr> Errors;
	FOperatorUniquePtr Operator = Builder.BuildGraphOperator(Graph, Errors);

	if (!Operator.IsValid())
	{
		return ISoundGeneratorPtr(nullptr);
	}

	FDataReferenceCollection OutputCollection = Operator->GetOutputs();
	FAudioBufferReadRef OutAudio = OutputCollection.GetDataReadReference<FAudioBuffer>(TEXT("Audio"));

	Generator = MakeShared<FMetasoundGenerator, ESPMode::ThreadSafe>(MoveTemp(Operator), OutAudio);
	FMetasoundGenerator* MetasoundGenerator = static_cast<FMetasoundGenerator*>(Generator.Get());
	MetasoundGenerator->SetFrequency(Frequency);
	MetasoundGenerator->SetBopPeriod(BopPeriod);

	return Generator;
}




