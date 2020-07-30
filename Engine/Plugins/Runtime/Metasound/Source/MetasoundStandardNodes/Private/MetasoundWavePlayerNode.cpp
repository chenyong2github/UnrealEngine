// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWavePlayerNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundBuildError.h"

#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FWavePlayerNode)
	
	// WavePlayer custom error 
	class FWavePlayerError : public FBuildErrorBase
	{
	public:
		FWavePlayerError(FName InName, FText InErrorDescription)
			: FBuildErrorBase(InName, InErrorDescription)
		{}
		virtual ~FWavePlayerError() = default;
	};
	
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{
	public:

		// Silent setup
		FWavePlayerOperator(
			const FOperatorSettings& InSettings, 
			const FWaveReadRef& InWave )
			: OperatorSettings(InSettings)
			, Wave(InWave)
			, AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
		{
			check(AudioBuffer->Num() == InSettings.GetNumFramesPerBlock());
			OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(AudioBuffer));
		}
		
		FWavePlayerOperator(
			const FOperatorSettings& InSettings,
			const FWaveReadRef& InWave, 
			FWave::FDecoderInputPtr&& InDecoderInput, 
			TUniquePtr<Audio::IDecoderOutput>&& InDecoderOutput,
			TUniquePtr<Audio::IDecoder>&& InDecoder )
			: OperatorSettings(InSettings)
			, Wave(InWave)
			, AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
			, Decoder(MoveTemp(InDecoder))
			, DecoderInput(MoveTemp(InDecoderInput))
			, DecoderOutput(MoveTemp(InDecoderOutput))
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

		// Scheduler/decoder holds weakref, and pins the sharedptr to the output when it writes.
		// FBufferedDecoderOutput
		// 1 write [... .. .. ]
		// n free
		// m queued 
		// 1 read [...........]

		// ApuMemory 2kb compress -> 512 frames

		// TCircularAudioBuffer //consider for Output object.
		
		void Execute()
		{
			int32 i = 0;
			float* Dst = AudioBuffer->GetData();

			// If we don't have a valid state, just output silence.
			if (Decoder && DecoderOutput && DecoderInput )
			{
				// V1. Do the decode inline, this will sound bad.
				Decoder->Decode();

				TArrayView<float> Src;
				int32 NumPopped = DecoderOutput->PopAudio(Src);
				int32 NumFramesToCopy = FMath::Min(NumPopped, OperatorSettings.GetNumFramesPerBlock());

				for (; i < NumFramesToCopy; ++i)
				{
					Dst[i] = Src[i];
				}
			}
			for ( ; i < OperatorSettings.GetNumFramesPerBlock(); ++i)
			{
				Dst[i] = 0.0f;
			}
		}

	private:
		const FOperatorSettings OperatorSettings;

		FWaveReadRef Wave;
		FAudioBufferWriteRef AudioBuffer;
		FDataReferenceCollection InputDataReferences;
		FDataReferenceCollection OutputDataReferences;

		// Decoder/IO. 
		Audio::ICodec::FDecoderPtr Decoder;
		FWave::FDecoderInputPtr DecoderInput;
		TUniquePtr<Audio::IDecoderOutput> DecoderOutput;
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

		FWave::FDecoderInputPtr Input = FWave::CreateDecoderInput(Wave);
		if (Input)
		{
			ICodecRegistry::FCodecPtr Codec = ICodecRegistry::Get().FindCodecByFromParsingInput(Input.Get());
			if (Codec)
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
				OutErrors.Add(MakeUnique<FWavePlayerError>(TEXT("FailedToFindCodec"),
					LOCTEXT("FailedToFindCodec", "Failed to find codec for opening the supplied Wave")));
			}
		}
		else
		{
			OutErrors.Add(MakeUnique<FWavePlayerError>(TEXT("FailedToParseInput"),
				LOCTEXT("FailedToParseInput", "Failed to parse the compressed data")));
		}

		// Create the player without any inputs, will just produce silence.
		return MakeUnique<FWavePlayerOperator>(InOperatorSettings,Wave);
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
