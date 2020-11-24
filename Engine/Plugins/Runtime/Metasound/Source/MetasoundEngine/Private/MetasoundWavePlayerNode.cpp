// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWavePlayerNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundBuildError.h"

#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	// WavePlayer custom error 
	class FWavePlayerError : public FBuildErrorBase
	{
	public:
		FWavePlayerError(const FWavePlayerNode& InNode, FText InErrorDescription)
			: FBuildErrorBase(ErrorType, InErrorDescription)
		{
			AddNode(InNode);
		}

		virtual ~FWavePlayerError() = default;

		static const FName ErrorType;
	};

	const FName FWavePlayerError::ErrorType = FName(TEXT("WavePlayerError"));
	
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{
	public:

		// Silent setup
		FWavePlayerOperator(
			const FOperatorSettings& InSettings, 
			const FWaveAssetReadRef& InWave )
			: OperatorSettings(InSettings)
			, Wave(InWave)
			, AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
		{
			check(AudioBuffer->Num() == InSettings.GetNumFramesPerBlock());
			OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(AudioBuffer));
		}
		
		FWavePlayerOperator(
			const FOperatorSettings& InSettings,
			const FWaveAssetReadRef& InWave, 
			FWaveAsset::FDecoderInputPtr&& InDecoderInput, 
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
			int32 NumPopped = 0;
			float* Dst = AudioBuffer->GetData();

			// If we don't have a valid state, just output silence.
			if (Decoder && DecoderOutput && DecoderInput )
			{
				// V1. Do the decode inline, this will sound bad.
				Decoder->Decode();

				Audio::IDecoderOutput::FPushedAudioDetails Details;
				NumPopped = DecoderOutput->PopAudio(MakeArrayView(Dst, AudioBuffer->Num()), Details);
			}

			// Pad with Silence if we didn't pop enough
			for ( int32 i = NumPopped; i < OperatorSettings.GetNumFramesPerBlock(); ++i)
			{
				Dst[i] = 0.0f;
			}
		}

	private:
		const FOperatorSettings OperatorSettings;

		FWaveAssetReadRef Wave;
		FAudioBufferWriteRef AudioBuffer;
		FDataReferenceCollection InputDataReferences;
		FDataReferenceCollection OutputDataReferences;

		// Decoder/IO. 
		Audio::ICodec::FDecoderPtr Decoder;
		FWaveAsset::FDecoderInputPtr DecoderInput;
		TUniquePtr<Audio::IDecoderOutput> DecoderOutput;
	};


	TUniquePtr<IOperator> FWavePlayerNode::FOperatorFactory::CreateOperator(
		const FCreateOperatorParams& InParams, 
		FBuildErrorArray& OutErrors) 
	{
		using namespace Audio;

		const FWavePlayerNode& WaveNode = static_cast<const FWavePlayerNode&>(InParams.Node);

		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;

		FWaveAssetReadRef Wave = InputCol.GetDataReadReferenceOrConstruct<FWaveAsset>(TEXT("Wave"));

		if(Wave->GetSoundWave())
		{
			FWaveAsset::FDecoderInputPtr Input = FWaveAsset::CreateDecoderInput(Wave);
			if (Input)
			{
				ICodecRegistry::FCodecPtr Codec = ICodecRegistry::Get().FindCodecByParsingInput(Input.Get());
				if (Codec)
				{
					IDecoderOutput::FRequirements Reqs 
					{ 
						Float32_Interleaved, 
						InParams.OperatorSettings.GetNumFramesPerBlock(), 
						static_cast<int32>(InParams.OperatorSettings.GetSampleRate()) 
					};
					TUniquePtr<IDecoderOutput> Output = IDecoderOutput::Create(Reqs);
					TUniquePtr<IDecoder> Decoder = Codec->CreateDecoder(Input.Get(), Output.Get());

					return MakeUnique<FWavePlayerOperator>(
						InParams.OperatorSettings, 
						Wave, 
						MoveTemp(Input), 
						MoveTemp(Output), 
						MoveTemp(Decoder)
					);
				}
				else
				{
					AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("FailedToFindCodec", "Failed to find codec for opening the supplied Wave"));
				}
			}
			else
			{
				AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("FailedToParseInput", "Failed to parse the compressed data"));
			}
		}
		else
		{
			AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("NoSoundWave", "No Sound Wave"));
		}

		// Create the player without any inputs, will just produce silence.
		return MakeUnique<FWavePlayerOperator>(InParams.OperatorSettings, Wave);
	}

	FVertexInterface FWavePlayerNode::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FWaveAsset>(TEXT("Wave"), LOCTEXT("WaveTooltip", "The Wave to be decoded"))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"))
			)
		);
	}

	const FNodeInfo& FWavePlayerNode::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("Wave Player"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a supplied Wave");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	}

	FWavePlayerNode::FWavePlayerNode(const FString& InName)
		:	FNode(InName, GetNodeInfo())
		,	Factory(MakeOperatorFactoryRef<FWavePlayerNode::FOperatorFactory>())
		,	Interface(DeclareVertexInterface())
	{
	}

	FWavePlayerNode::FWavePlayerNode(const FNodeInitData& InInitData)
		: FWavePlayerNode(InInitData.InstanceName)
	{
	}

	FOperatorFactorySharedRef FWavePlayerNode::GetDefaultOperatorFactory() const 
	{
		return Factory;
	}



	const FVertexInterface& FWavePlayerNode::GetVertexInterface() const
	{
		return Interface;
	}

	bool FWavePlayerNode::SetVertexInterface(const FVertexInterface& InInterface)
	{
		return InInterface == Interface;
	}

	bool FWavePlayerNode::IsVertexInterfaceSupported(const FVertexInterface& InInterface) const
	{
		return InInterface == Interface;
	}

	METASOUND_REGISTER_NODE(FWavePlayerNode)
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundWaveNode
