// Copyright Epic Games, Inc. All Rights Reserved.
#include "DSP/Dsp.h"
#include "DSP/BufferVectorOperations.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_MixerNode"


namespace Metasound
{
# pragma region Operator Declaration
	template<uint32 NumInputs, uint32 NumChannels>
	class TAudioMixerNodeOperator : public TExecutableOperator<TAudioMixerNodeOperator<NumInputs, NumChannels>>
	{
	public:
		// ctor
		TAudioMixerNodeOperator(const FOperatorSettings& InSettings, const TArray<FAudioBufferReadRef>&& InInputBuffers, const TArray<FFloatReadRef>&& InGainValues)
			: Gains(InGainValues)
			, Inputs (InInputBuffers)
			, Settings(InSettings)
		{
			// create write refs
			for (uint32 i = 0; i < NumChannels; ++i)
			{
				Outputs.Add(FAudioBufferWriteRef::CreateNew(InSettings));
			}

			// init previous gains to current values
			PrevGains.Reset();
			PrevGains.AddUninitialized(NumInputs);
			for (uint32 i = 0; i < NumInputs; ++i)
			{
				PrevGains[i] = *Gains[i];
			}
		}

		// dtor
		virtual ~TAudioMixerNodeOperator() = default;

		static const FVertexInterface& GetDefaultInterface()
		{
			auto CreateDefaultInterface = []()-> FVertexInterface
			{
				// inputs
				FInputVertexInterface InputInterface;
				for (uint32 InputIndex = 0; InputIndex < NumInputs; ++InputIndex)
				{
					// audio channels
					for (uint32 ChanIndex = 0; ChanIndex < NumChannels; ++ChanIndex)
					{
						InputInterface.Add(TInputDataVertexModel<FAudioBuffer>(GetAudioInputName(InputIndex, ChanIndex), GetAudioInputDescription(InputIndex, ChanIndex)));
					}

					// gain scalar
					InputInterface.Add(TInputDataVertexModel<float>(GetGainInputName(InputIndex), GetGainInputDescription(InputIndex), 1.0f));
				}

				// outputs
				FOutputVertexInterface OutputInterface;
				for (uint32 i = 0; i < NumChannels; ++i)
				{
					OutputInterface.Add(TOutputDataVertexModel<FAudioBuffer>(GetAudioOutputName(i), GetAudioOutputDescription(i)));
				}

				return FVertexInterface(InputInterface, OutputInterface);
			}; // end lambda: CreateDefaultInterface()

			static const FVertexInterface DefaultInterface = CreateDefaultInterface();
			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			// used if NumChannels == 1
			auto CreateNodeClassMetadataMono = []() -> FNodeClassMetadata
			{
				FName OperatorName = *FString::Printf(TEXT("Audio Mixer (Mono, %d)"), NumInputs);
				FText NodeDisplayName = FText::Format(LOCTEXT("AudioMixerDisplayNamePattern", "Mono Mixer ({0})"), NumInputs);
				FText NodeDescription = LOCTEXT("MixerDescription", "Will scale input channels by their corresponding gain value and sum them together.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return CreateNodeClassMetadata(OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			// used if NumChannels == 2
			auto CreateNodeClassMetadataStereo = []() -> FNodeClassMetadata
			{
				FName OperatorName = *FString::Printf(TEXT("Audio Mixer (Stereo, %d)"), NumInputs);
				FText NodeDisplayName = FText::Format(LOCTEXT("AudioMixerDisplayNamePattern", "Stereo Mixer ({0})"), NumInputs);
				FText NodeDescription = LOCTEXT("MixerDescription", "Will scale input channels by their corresponding gain value and sum them together.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return  CreateNodeClassMetadata(OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			// used if NumChannels > 2
			auto CreateNodeClassMetadataMultiChan = []() -> FNodeClassMetadata
			{
				FName OperatorName = *FString::Printf(TEXT("Audio Mixer (%d-Channel, %d)"), NumChannels, NumInputs);
				FText NodeDisplayName = FText::Format(LOCTEXT("AudioMixerDisplayNamePattern", "{0}-channel Mixer ({1})"), NumChannels, NumInputs);
				FText NodeDescription = LOCTEXT("MixerDescription", "Will scale input audio by their corresponding gain value and sum them together.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return  CreateNodeClassMetadata(OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = (NumChannels == 1)? CreateNodeClassMetadataMono()
														: (NumChannels == 2)? CreateNodeClassMetadataStereo() : CreateNodeClassMetadataMultiChan();
			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

			TArray<FAudioBufferReadRef> InputBuffers;
			TArray<FFloatReadRef> InputGains;

			for (uint32 i = 0; i < NumInputs; ++i)
			{
				for (uint32 Chan = 0; Chan < NumChannels; ++Chan)
				{
					InputBuffers.Add(InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetAudioInputName(i, Chan), InParams.OperatorSettings));
				}

				InputGains.Add(InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, GetGainInputName(i), InParams.OperatorSettings));
			}

			return MakeUnique<TAudioMixerNodeOperator<NumInputs, NumChannels>>(InParams.OperatorSettings, MoveTemp(InputBuffers), MoveTemp(InputGains));
		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputPins;
			for (uint32 i = 0; i < NumInputs; ++i)
			{
				for (uint32 Chan = 0; Chan < NumChannels; ++Chan)
				{
					InputPins.AddDataReadReference(GetAudioInputName(i, Chan), Inputs[i * NumChannels + Chan]);
				}

				InputPins.AddDataReadReference(GetGainInputName(i), Gains[i]);
			}

			return InputPins;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputPins;

			for (uint32 i = 0; i < NumChannels; ++i)
			{
				OutputPins.AddDataReadReference(GetAudioOutputName(i), Outputs[i]);
			}

			return OutputPins;
		}

		void Execute()
		{
			// zero the outputs
			for (uint32 i = 0; i < NumChannels; ++i)
			{
				FMemory::Memzero(Outputs[i]->GetData(), sizeof(float) * Outputs[i]->Num());
			}

			// for each input
			for (uint32 InputIndex = 0; InputIndex < NumInputs; ++InputIndex)
			{
				// for each channel of audio
				for (uint32 ChanIndex = 0; ChanIndex < NumChannels; ++ChanIndex)
				{
					// Outputs[Chan] += Gains[i] * Inputs[i][Chan]
					const float* InputPtr = Inputs[InputIndex * NumChannels + ChanIndex]->GetData();
					const float NextGain = *Gains[InputIndex];
					const float PrevGain = PrevGains[InputIndex];
					float* OutputPtr = Outputs[ChanIndex]->GetData();

					Audio::MixInBufferFast(InputPtr,  OutputPtr, Settings.GetNumFramesPerBlock() , PrevGain, NextGain);

					PrevGains[InputIndex] = NextGain;
				}
			}
		}


	private:
		TArray<FFloatReadRef> Gains;
		TArray<FAudioBufferReadRef> Inputs;
		TArray<FAudioBufferWriteRef> Outputs;

		TArray<float> PrevGains;

		FOperatorSettings Settings;

		static FNodeClassMetadata CreateNodeClassMetadata(const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("AudioMixer"), InOperatorName, TEXT("")},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{ StandardNodes::Audio },
				{TEXT("AudioMixer")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}

#pragma region Name Gen
		static const FString GetAudioInputName(uint32 InputIndex, uint32 ChannelIndex)
		{
			if (NumChannels == 1)
			{
				return FString::Printf(TEXT("In %i"), InputIndex);
			}
			else if (NumChannels == 2)
			{
				return FString::Printf(TEXT("In %i %s"), InputIndex, (ChannelIndex == 0) ? TEXT("L") : TEXT("R"));
			}

			return FString::Printf(TEXT("In %i, %i"), InputIndex, ChannelIndex);
		}

		static const FText GetAudioInputDescription(uint32 InputIndex, uint32 ChannelIndex)
		{
			return FText::Format(LOCTEXT("AudioMixerAudioInputDescription", "Audio Input #: {0}, Channel: {1}"), InputIndex, ChannelIndex);
		}

		static const FString GetGainInputName(uint32 InputIndex)
		{
			return FString::Printf(TEXT("Gain %i"), InputIndex);
		}

		static const FText GetGainInputDescription(uint32 InputIndex)
		{
			return FText::Format(LOCTEXT("AudioMixerGainInputDescription", "Gain Input #: {0}"), InputIndex);
		}

		static const FString GetAudioOutputName(uint32 ChannelIndex)
		{
			if (NumChannels == 1)
			{
				return TEXT("Out");
			}
			else if (NumChannels == 2)
			{
				return FString::Printf(TEXT("Out %s"), (ChannelIndex == 0) ? TEXT("L") : TEXT("R"));
			}

			return FString::Printf(TEXT("Out %i"), ChannelIndex);
		}

		static const FText GetAudioOutputDescription(uint32 ChannelIndex)
		{
			return FText::Format(LOCTEXT("AudioMixerAudioOutputDescription", "Summed output for channel: {0}"), ChannelIndex);
		}
#pragma endregion
	}; // class TAudioMixerNodeOperator
#pragma endregion



#pragma region Node Definition
	template<uint32 NumInputs, uint32 NumChannels>
	class METASOUNDSTANDARDNODES_API TAudioMixerNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TAudioMixerNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TAudioMixerNodeOperator<NumInputs, NumChannels>>())
		{}

		virtual ~TAudioMixerNode() = default;
	};
#pragma endregion


#pragma region Node Registration
	#define REGISTER_AUDIOMIXER_NODE(A, B) \
		using FAudioMixerNode_##A ## _ ##B = TAudioMixerNode<A, B>; \
		METASOUND_REGISTER_NODE(FAudioMixerNode_##A ## _ ##B) \


	// mono
	REGISTER_AUDIOMIXER_NODE(2, 1)
	REGISTER_AUDIOMIXER_NODE(3, 1)
	REGISTER_AUDIOMIXER_NODE(4, 1)
	REGISTER_AUDIOMIXER_NODE(5, 1)
	REGISTER_AUDIOMIXER_NODE(6, 1)
	REGISTER_AUDIOMIXER_NODE(7, 1)
	REGISTER_AUDIOMIXER_NODE(8, 1)

	// stereo
 	REGISTER_AUDIOMIXER_NODE(2, 2)
	REGISTER_AUDIOMIXER_NODE(3, 2)
	REGISTER_AUDIOMIXER_NODE(4, 2)
	REGISTER_AUDIOMIXER_NODE(5, 2)
	REGISTER_AUDIOMIXER_NODE(6, 2)
	REGISTER_AUDIOMIXER_NODE(7, 2)
	REGISTER_AUDIOMIXER_NODE(8, 2)

	// test
//	REGISTER_AUDIOMIXER_NODE(8, 6)

#pragma endregion




} // namespace Metasound

#undef LOCTEXT_NAMESPACE // "MetasoundStandardNodes_MixerNode"