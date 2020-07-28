// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWavePlayerNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"

#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FWavePlayerNode)
		
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{
		public:
			FWavePlayerOperator(
				const FOperatorSettings& InSettings, 
				const FWaveReadRef& InWave, 
				TUniquePtr<Audio::IDecoderInput>&& InDecoderInput, 
				TUniquePtr<Audio::IDecoderOutput>&& InDecoderOutput,
				TUniquePtr<Audio::IDecoder>&& InDecoder )
				: OperatorSettings(InSettings)
				, Decoder(MoveTemp(InDecoder))
				, DecoderInput(MoveTemp(InDecoderInput))
				, DecoderOutput(MoveTemp(InDecoderOutput))
				, Wave(InWave)
				, AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
			{
				check(AudioBuffer->Num() == InSettings.GetNumFramesPerBlock());

				OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(AudioBuffer));
			}

			virtual const FDataReferenceCollection& GetInputs() const override
			{
				return InputDataReferences;
			}

			virtual const FDataReferenceCollection& GetOutputs() const override
			{
				return OutputDataReferences;
			}

			void Execute()
			{
				// V1. Do the decode inline, this will sound bad.
				Decoder->Decode();

				TArrayView<float> Src;
				float* Dst = AudioBuffer->GetData();
				
				int32 NumPopped = DecoderOutput->PopAudio(Src);
				int32 NumFramesToCopy = FMath::Min(NumPopped,OperatorSettings.GetNumFramesPerBlock());

				int32 i = 0;
				for (; i < NumFramesToCopy; ++i)
				{
					Dst[i] = Src[i];
				}
				for ( ; i < OperatorSettings.GetNumFramesPerBlock(); ++i)
				{
					Dst[i] = 0.0f;
				}
			}

		private:
			const FOperatorSettings OperatorSettings;
			
			Audio::ICodec::FDecoderPtr Decoder;
			TUniquePtr<Audio::IDecoderInput> DecoderInput;
			TUniquePtr<Audio::IDecoderOutput> DecoderOutput;
			
			FWaveReadRef Wave;
			FAudioBufferWriteRef AudioBuffer;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};

	const FName FWavePlayerNode::ClassName = FName(TEXT("Wave"));

	TUniquePtr<IOperator> FWavePlayerNode::FOperatorFactory::CreateOperator(
		const INode& InNode, 
		const FOperatorSettings& InOperatorSettings,
		const FDataReferenceCollection& InInputDataReferences, 
		TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
	{
		using namespace Audio;

		const FWavePlayerNode& WaveNode = static_cast<const FWavePlayerNode&>(InNode);
		FWaveReadRef Wave = FWaveReadRef::CreateNew();

		if (InInputDataReferences.ContainsDataReadReference<FWave>(TEXT("Wave")))
		{
			Wave = InInputDataReferences.GetDataReadReference<FWave>(TEXT("Wave"));
		}

		TUniquePtr<IDecoderInput> Input = IDecoderInput::Create(Wave->GetCompressedData(),0);
		if(Input)
		{
			ICodecRegistry::FCodecPtr Codec = Audio::ICodecRegistry::Get().FindCodecByFromParsingInput(Input.Get());
			if(Codec)
			{
				// V1, Ask for an output buffer the size of a frame.
				IDecoderOutput::FRequirements Reqs { Float32_Interleaved, InOperatorSettings.GetNumFramesPerBlock() };
				TUniquePtr<IDecoderOutput> Output = IDecoderOutput::Create(Reqs);
				TUniquePtr<IDecoder> Decoder = Codec->CreateDecoder(Input.Get(), Output.Get());

				return MakeUnique<FWavePlayerOperator>(
					InOperatorSettings, 
					Wave, 
					MoveTemp(Input), 
					MoveTemp(Output), 
					MoveTemp(Decoder)
				);
			}
			else
			{
				// LogMsg; Failed to Find
			}
		}
		else
		{
			// LogMsg; Failed to parse input stream, is it valid?
		}

		// Fail.
		return nullptr;
	}

	FWavePlayerNode::FWavePlayerNode(const FString& InName)
		:	FNode(InName)
	{
		AddInputDataVertex<FWave>(TEXT("Wave"), LOCTEXT("WaveTooltip", "The Wave to be decoded"));
		AddOutputDataVertex<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"));
	}

	FWavePlayerNode::FWavePlayerNode(const FNodeInitData& InInitData)
		: FWavePlayerNode(InInitData.InstanceName)
	{
	}

	FWavePlayerNode::~FWavePlayerNode()
	{
	}
	
	const FName& FWavePlayerNode::GetClassName() const
	{
		return ::Metasound::FWavePlayerNode::ClassName;
	}

	IOperatorFactory& FWavePlayerNode::GetDefaultOperatorFactory() 
	{
		return Factory;
	}
}
#undef LOCTEXT_NAMESPACE //MetasoundWaveNode
