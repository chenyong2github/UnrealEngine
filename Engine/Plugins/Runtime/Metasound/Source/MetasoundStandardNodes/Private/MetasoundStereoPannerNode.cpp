// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/Text.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/BufferVectorOperations.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_StereoPanner"

namespace Metasound
{
	namespace StereoPannerVertexNames
	{
		const FVertexName& GetInputAudioName()
		{
			static FVertexName Name = TEXT("In");
			return Name;
		}

		const FText& GetInputAudioDescription()
		{
			static FText Desc = METASOUND_LOCTEXT("StereoPannerNodeInDesc", "The input audio to pan.");
			return Desc;
		}

		const FVertexName& GetInputPanAmountName()
		{
			static FVertexName Name = TEXT("Pan Amount");
			return Name;
		}

		const FText& GetInputPanAmountDescription()
		{
			static FText Desc = METASOUND_LOCTEXT("StereoPannerNodePanAmountDesc", "The amount of pan. -1.0 is full left, 1.0 is full right.");
			return Desc;
		}

		const FVertexName& GetInputPanningLawName()
		{
			static FVertexName Name = TEXT("Panning Law");
			return Name;
		}

		const FText& GetInputPanningLawDescription()
		{
			static FText Desc = METASOUND_LOCTEXT("StereoPannerNodePanningLawDescription", "Which panning law should be used for the stereo panner.");
			return Desc;
		}

		const FVertexName& GetOutputAudioLeftName()
		{
			static FVertexName Name = TEXT("Out Left");
			return Name;
		}

		const FText& GetOutputAudioLeftDescription()
		{
			static FText Desc = METASOUND_LOCTEXT("StereoPannerNodeOutputLeftDescription", "Left channel audio output.");
			return Desc;
		}

		const FVertexName& GetOutputAudioRightName()
		{
			static FVertexName Name = TEXT("Out Right");
			return Name;
		}

		const FText& GetOutputAudioRightDescription()
		{
			static FText Desc = METASOUND_LOCTEXT("StereoPannerNodeOutputRightDescription", "Right channel audio output.");
			return Desc;
		}
	}

	enum class EPanningLaw
	{
		EqualPower = 0,
		Linear
	};

	DECLARE_METASOUND_ENUM(EPanningLaw, EPanningLaw::EqualPower, METASOUNDSTANDARDNODES_API,
	FEnumPanningLaw, FEnumPanningLawInfo, FPanningLawReadRef, FPanningLawWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(EPanningLaw, FEnumPanningLaw, "PanningLaw")
		DEFINE_METASOUND_ENUM_ENTRY(EPanningLaw::EqualPower, "PanningLawEqualPowerName", "Equal Power", "PanningLawEqualPowerTT", "The power of the audio signal is constant while panning."),
		DEFINE_METASOUND_ENUM_ENTRY(EPanningLaw::Linear, "PanningLawLinearName", "Linear", "PanningLawLinearTT", "The amplitude of the audio signal is constant while panning."),
	DEFINE_METASOUND_ENUM_END()

	class FStereoPannerOperator : public TExecutableOperator<FStereoPannerOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FStereoPannerOperator(const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput, 
			const FFloatReadRef& InPanningAmount,
			const FPanningLawReadRef& InPanningLaw);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		float GetInputDelayTimeMsec() const;
		void ComputePanGains(float InPanningAmmount, float& OutLeftGain, float& OutRightGain) const;

		// The input audio buffer
		FAudioBufferReadRef AudioInput;

		// The amount of delay time
		FFloatReadRef PanningAmount;

		// The the dry level
		FPanningLawReadRef PanningLaw;

		// The audio output
		FAudioBufferWriteRef AudioLeftOutput;
		FAudioBufferWriteRef AudioRightOutput;

		float PrevPanningAmount = 0.0f;
		float PrevLeftPan = 0.0f;
		float PrevRightPan = 0.0f;
	};

	FStereoPannerOperator::FStereoPannerOperator(const FOperatorSettings& InSettings,
		const FAudioBufferReadRef& InAudioInput, 
		const FFloatReadRef& InPanningAmount,
		const FPanningLawReadRef& InPanningLaw)
		: AudioInput(InAudioInput)
		, PanningAmount(InPanningAmount)
		, PanningLaw(InPanningLaw)
		, AudioLeftOutput(FAudioBufferWriteRef::CreateNew(InSettings))
		, AudioRightOutput(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		PrevPanningAmount = FMath::Clamp(*PanningAmount, -1.0f, 1.0f);

		ComputePanGains(PrevPanningAmount, PrevLeftPan, PrevRightPan);
	}

	FDataReferenceCollection FStereoPannerOperator::GetInputs() const
	{
		using namespace StereoPannerVertexNames;

		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(GetInputAudioName(), AudioInput);
		InputDataReferences.AddDataReadReference(GetInputPanAmountName(), PanningAmount);
		InputDataReferences.AddDataReadReference(GetInputPanningLawName(), PanningLaw);

		return InputDataReferences;
	}

	FDataReferenceCollection FStereoPannerOperator::GetOutputs() const
	{
		using namespace StereoPannerVertexNames;

		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(GetOutputAudioLeftName(), AudioLeftOutput);
		OutputDataReferences.AddDataReadReference(GetOutputAudioRightName(), AudioRightOutput);

		return OutputDataReferences;
	}

	void FStereoPannerOperator::ComputePanGains(float InPanningAmmount, float& OutLeftGain, float& OutRightGain) const
	{
		// Convert [-1.0, 1.0] to [0.0, 1.0]
		float Fraction = 0.5f * (InPanningAmmount + 1.0f);

		if (*PanningLaw == EPanningLaw::EqualPower)
		{
			// Compute the left and right amount with one math call
			FMath::SinCos(&OutRightGain, &OutLeftGain, 0.5f * PI * Fraction);
		}
		else
		{
			OutLeftGain = Fraction;
			OutRightGain = 1.0f - Fraction;
		}
	}


	void FStereoPannerOperator::Execute()
	{
		float CurrentPanningAmount = FMath::Clamp(*PanningAmount, -1.0f, 1.0f);

		const float* InputBufferPtr = AudioInput->GetData();
		int32 InputSampleCount = AudioInput->Num();
		float* OutputLeftBufferPtr = AudioLeftOutput->GetData();
		float* OutputRightBufferPtr = AudioRightOutput->GetData();

		if (FMath::IsNearlyEqual(PrevPanningAmount, CurrentPanningAmount))
		{
			Audio::BufferMultiplyByConstant(InputBufferPtr, PrevLeftPan, OutputLeftBufferPtr, InputSampleCount);
			Audio::BufferMultiplyByConstant(InputBufferPtr, PrevRightPan, OutputRightBufferPtr, InputSampleCount);
		}
		else 
		{
			// The pan amount has changed so recompute it
			float CurrentLeftPan;
			float CurrentRightPan;
			ComputePanGains(CurrentPanningAmount, CurrentLeftPan, CurrentRightPan);

			// Copy the input to the output buffers
			FMemory::Memcpy(OutputLeftBufferPtr, InputBufferPtr, InputSampleCount * sizeof(float));
			FMemory::Memcpy(OutputRightBufferPtr, InputBufferPtr, InputSampleCount * sizeof(float));

			// Do a fast fade on the buffers from the prev left/right gains to current left/right gains
			Audio::FadeBufferFast(OutputLeftBufferPtr, InputSampleCount, PrevLeftPan, CurrentLeftPan);
			Audio::FadeBufferFast(OutputRightBufferPtr, InputSampleCount, PrevRightPan, CurrentRightPan);

			// lerp through the buffer to the target panning amount
			PrevPanningAmount = *PanningAmount;
			PrevLeftPan = CurrentLeftPan;
			PrevRightPan = CurrentRightPan;
		}
	}

	const FVertexInterface& FStereoPannerOperator::GetVertexInterface()
	{
		using namespace StereoPannerVertexNames;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(GetInputAudioName(), GetInputAudioDescription()),
				TInputDataVertexModel<float>(GetInputPanAmountName(), GetInputPanAmountDescription(), 0.0f),
				TInputDataVertexModel<FEnumPanningLaw>(GetInputPanningLawName(), GetInputPanningLawDescription())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(GetOutputAudioLeftName(), GetOutputAudioLeftDescription()),
				TOutputDataVertexModel<FAudioBuffer>(GetOutputAudioRightName(), GetOutputAudioRightDescription())
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FStereoPannerOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { StandardNodes::Namespace, TEXT("Stereo Panner"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT("Metasound_StereoPannerDisplayName", "Stereo Panner");
			Info.Description = METASOUND_LOCTEXT("Metasound_StereoPannerNodeDescription", "Pans an input audio signal to left and right outputs.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();
			Info.CategoryHierarchy.Emplace(NodeCategories::Spatialization);
			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FStereoPannerOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		using namespace StereoPannerVertexNames;

		FAudioBufferReadRef AudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetInputAudioName(), InParams.OperatorSettings);
		FFloatReadRef PanningAmount = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, GetInputPanAmountName(), InParams.OperatorSettings);
		FPanningLawReadRef PanningLaw = InputCollection.GetDataReadReferenceOrConstruct<FEnumPanningLaw>(GetInputPanningLawName());

		return MakeUnique<FStereoPannerOperator>(InParams.OperatorSettings, AudioIn, PanningAmount, PanningLaw);
	}

	class FStereoPannerNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FStereoPannerNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FStereoPannerOperator>())
		{
		}
	};


	METASOUND_REGISTER_NODE(FStereoPannerNode)
}

#undef LOCTEXT_NAMESPACE
