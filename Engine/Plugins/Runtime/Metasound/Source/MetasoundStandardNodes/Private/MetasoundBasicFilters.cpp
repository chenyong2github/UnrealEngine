// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundBasicFilters.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundStandardNodesCategories.h"

#include "DSP/Filter.h"
#include "DSP/InterpolatedOnePole.h"
#include "Math/NumericLimits.h"

#define LOCTEXT_NAMESPACE "MetasoundBasicFilterNodes"

namespace Metasound
{
#pragma region Ladder Filter

	class FLadderFilterOperator : public TExecutableOperator<FLadderFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;

	public:
		static const FNodeClassMetadata& GetNodeInfo();

		static FVertexInterface DeclareVertexInterface();

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(AudioInput));
			InputDataReferences.AddDataReadReference(TEXT("Freq."), FFloatReadRef(Frequency));
			InputDataReferences.AddDataReadReference(TEXT("Res."), FFloatReadRef(Resonance));

			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			// expose read access to our output buffer for other processors in the graph
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("Out"), FAudioBufferReadRef(AudioOutput));

			return OutputDataReferences;
		}

		void Execute();


	private: // members
		// input pins
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Frequency;
		FFloatReadRef Resonance;

		// cached data
		float PreviousFrequency{ InvalidValue };
		float PreviousResonance{ InvalidValue };

		// output pins
		FAudioBufferWriteRef AudioOutput;

		// data
		const int32 BlockSize;
		const float SampleRate;
		const float MaxCutoffFrequency;

		// dsp
		Audio::FLadderFilter LadderFilter;


	public:
		// ctor
		FLadderFilterOperator(
			const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FFloatReadRef& InFrequency,
			const FFloatReadRef& InResonance
		)
			: AudioInput(InAudioInput)
			, Frequency(InFrequency)
			, Resonance(InResonance)
			, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BlockSize(InSettings.GetNumFramesPerBlock())
			, SampleRate(InSettings.GetSampleRate())
			, MaxCutoffFrequency(0.5f * SampleRate)
		{
			// verify our buffer sizes:
			check(AudioOutput->Num() == BlockSize);

			LadderFilter.Init(SampleRate, 1);
		}
	};

	FLadderFilterNode::FLadderFilterNode(const FString& InInstanceName, const FGuid& InInstanceID)
		: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FLadderFilterOperator>())
	{
	}

	FLadderFilterNode::FLadderFilterNode(const FNodeInitData& InInitData)
		: FLadderFilterNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	const FNodeClassMetadata& FLadderFilterOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Ladder Filter"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_LadderFilterNodeDisplayName", "Ladder Filter");
			Info.Description = LOCTEXT("Ladder_Filter_NodeDescription", "Ladder filter"),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::Filters);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FVertexInterface FLadderFilterOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("In"), LOCTEXT("AudioInputTooltip", "Audio Input")),
				TInputDataVertexModel<float>(TEXT("Freq."), LOCTEXT("FrequencyTooltip", "Controls cutoff frequency"), 20000.f),
				TInputDataVertexModel<float>(TEXT("Res."), LOCTEXT("ResonanceTooltip", "Controls filter resonance"), 6.f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("FilterOutputToolTip", "Audio Out"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FLadderFilterOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FLadderFilterNode& LadderFilterNode = static_cast<const FLadderFilterNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// inputs
		FAudioBufferReadRef AudioIn = InputDataRefs.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("In"), InParams.OperatorSettings);
		FFloatReadRef FrequencyIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Freq."));
		FFloatReadRef ResonanceIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Res."));

		return MakeUnique<FLadderFilterOperator>(
			InParams.OperatorSettings
			, AudioIn
			, FrequencyIn
			, ResonanceIn
			);
	}

	void FLadderFilterOperator::Execute()
	{
		const float CurrentFrequency = FMath::Clamp(*Frequency, 0.f, MaxCutoffFrequency);
		const float CurrentResonance = FMath::Clamp(*Resonance, 1.0f, 10.0f);

		const bool bNeedsUpdate =
			   (!FMath::IsNearlyEqual(PreviousFrequency, CurrentFrequency))
			|| (!FMath::IsNearlyEqual(PreviousResonance, CurrentResonance));

		if (bNeedsUpdate)
		{
			LadderFilter.SetQ(*Resonance);
			LadderFilter.SetFrequency(*Frequency);

			LadderFilter.Update();

			PreviousFrequency = *Frequency;
			PreviousResonance = *Resonance;
		}

		LadderFilter.ProcessAudio(AudioInput->GetData(), AudioInput->Num(), AudioOutput->GetData());
	}

	METASOUND_REGISTER_NODE(FLadderFilterNode);

#pragma endregion


#pragma region State Variable Filter

	class FStateVariableFilterOperator : public TExecutableOperator<FStateVariableFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;
	public:
		static const FNodeClassMetadata& GetNodeInfo();

		static FVertexInterface DeclareVertexInterface();

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(AudioInput));
			InputDataReferences.AddDataReadReference(TEXT("Freq."), FFloatReadRef(Frequency));
			InputDataReferences.AddDataReadReference(TEXT("Res."), FFloatReadRef(Resonance));
			InputDataReferences.AddDataReadReference(TEXT("Band-Stop Control"), FFloatReadRef(BandStopControl));

			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			// expose read access to our output buffer for other processors in the graph
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("LP"), FAudioBufferReadRef(LowPassOutput));
			OutputDataReferences.AddDataReadReference(TEXT("HP"), FAudioBufferReadRef(HighPassOutput));
			OutputDataReferences.AddDataReadReference(TEXT("BP"), FAudioBufferReadRef(BandPassOutput));
			OutputDataReferences.AddDataReadReference(TEXT("BS"), FAudioBufferReadRef(BandStopOutput));

			return OutputDataReferences;
		}

		void Execute();


	private: // members
		// input pins
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Frequency;
		FFloatReadRef Resonance;
		FFloatReadRef BandStopControl;

		// cached data
		float PreviousFrequency{ InvalidValue };
		float PreviousResonance{ InvalidValue };
		float PreviousBandStopControl{ InvalidValue };

		// output pins
		FAudioBufferWriteRef LowPassOutput;
		FAudioBufferWriteRef HighPassOutput;
		FAudioBufferWriteRef BandPassOutput;
		FAudioBufferWriteRef BandStopOutput;

		// data
		const int32 BlockSize;
		float SampleRate;
		float MaxCutoffFrequency;

		// dsp
		Audio::FStateVariableFilter StateVariableFilter;


	public:
		// ctor
		FStateVariableFilterOperator(
			const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FFloatReadRef& InFrequency,
			const FFloatReadRef& InResonance,
			const FFloatReadRef& InBandStopControl
		)
			: AudioInput(InAudioInput)
			, Frequency(InFrequency)
			, Resonance(InResonance)
			, BandStopControl(InBandStopControl)
			, LowPassOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, HighPassOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BandPassOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BandStopOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BlockSize(InSettings.GetNumFramesPerBlock())
			, SampleRate(InSettings.GetSampleRate())
			, MaxCutoffFrequency(0.5f * SampleRate)
		{
			// verify our buffer sizes:
			check(LowPassOutput->Num() == BlockSize);
			check(HighPassOutput->Num() == BlockSize);
			check(BandPassOutput->Num() == BlockSize);
			check(BandPassOutput->Num() == BlockSize);

			StateVariableFilter.Init(SampleRate, 1);
		}
	};

	FStateVariableFilterNode::FStateVariableFilterNode(const FString& InInstanceName, const FGuid& InInstanceID)
		: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FStateVariableFilterOperator>())
	{
	}

	FStateVariableFilterNode::FStateVariableFilterNode(const FNodeInitData& InInitData)
		: FStateVariableFilterNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	const FNodeClassMetadata& FStateVariableFilterOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("State Variable Filter"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_StateVariableFilterNodeDisplayName", "State Variable Filter");
			Info.Description = LOCTEXT("State_Variable_Filter_NodeDescription", "State Variable filter"),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::Filters);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FVertexInterface FStateVariableFilterOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("In"), LOCTEXT("AudioInputTooltip", "Audio Input")),
				TInputDataVertexModel<float>(TEXT("Freq."), LOCTEXT("FrequencyTooltip", "Controls cutoff frequency"), 20000.f),
				TInputDataVertexModel<float>(TEXT("Res."), LOCTEXT("ResonanceTooltip", "Controls filter resonance"), 0.f),
				TInputDataVertexModel<float>(TEXT("Band-Stop Control"), LOCTEXT("BandStopControlTooltip", "Band Stop Control"), 0.f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("LP"), LOCTEXT("FilterOutputToolTip", "Low Pass Output")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("HP"), LOCTEXT("FilterOutputToolTip", "High Pass Output")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("BP"), LOCTEXT("FilterOutputToolTip", "Band Pass Output")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("BS"), LOCTEXT("FilterOutputToolTip", "Band Stop Output"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FStateVariableFilterOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FStateVariableFilterNode& StateVariableFilterNode = static_cast<const FStateVariableFilterNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// inputs
		FAudioBufferReadRef AudioIn = InputDataRefs.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("In"), InParams.OperatorSettings);
		FFloatReadRef FrequencyIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Freq."));
		FFloatReadRef ResonanceIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Res."));
		FFloatReadRef PassBandGainCompensationIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Band-Stop Control"));

		return MakeUnique<FStateVariableFilterOperator>(
			InParams.OperatorSettings
			, AudioIn
			, FrequencyIn
			, ResonanceIn
			, PassBandGainCompensationIn
			);
	}

	void FStateVariableFilterOperator::Execute()
	{
		const float CurrentFrequency = FMath::Clamp(*Frequency, 0.f, MaxCutoffFrequency);
		const float CurrentResonance = FMath::Clamp(*Resonance, 0.f, 10.f);
		const float CurrentBandStopControl = FMath::Clamp(*BandStopControl, 0.f, 1.f);

		bool bNeedsUpdate =
			   (!FMath::IsNearlyEqual(PreviousFrequency, CurrentFrequency))
			|| (!FMath::IsNearlyEqual(PreviousResonance, CurrentResonance))
			|| (!FMath::IsNearlyEqual(PreviousBandStopControl, CurrentBandStopControl));

		if (bNeedsUpdate)
		{
			StateVariableFilter.SetQ(CurrentResonance);
			StateVariableFilter.SetFrequency(CurrentFrequency);
			StateVariableFilter.SetBandStopControl(CurrentBandStopControl);

			StateVariableFilter.Update();

			PreviousFrequency = CurrentFrequency;
			PreviousResonance = CurrentResonance;
			PreviousBandStopControl = CurrentBandStopControl;
		}

		StateVariableFilter.ProcessAudio(
			AudioInput->GetData()
			, AudioInput->Num()
			, LowPassOutput->GetData()
			, HighPassOutput->GetData()
			, BandPassOutput->GetData()
			, BandStopOutput->GetData()
		);
	}

	METASOUND_REGISTER_NODE(FStateVariableFilterNode);

#pragma endregion


#pragma region OnePoleLowPass Filter

	class FOnePoleLowPassFilterOperator : public TExecutableOperator<FOnePoleLowPassFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;
	public:
		static const FNodeClassMetadata& GetNodeInfo();

		static FVertexInterface DeclareVertexInterface();

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(AudioInput));
			InputDataReferences.AddDataReadReference(TEXT("Freq."), FFloatReadRef(Frequency));

			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			// expose read access to our output buffer for other processors in the graph
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("Out"), FAudioBufferReadRef(AudioOutput));

			return OutputDataReferences;
		}

		void Execute();


	private: // members
		// input pins
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Frequency;

		// output pins
		FAudioBufferWriteRef AudioOutput;

		// data
		const int32 BlockSize;
		float SampleRate;

		// dsp
		Audio::FInterpolatedLPF OnePoleLowPassFilter;


	public:
		// ctor
		FOnePoleLowPassFilterOperator(
			const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FFloatReadRef& InFrequency
		)
			: AudioInput(InAudioInput)
			, Frequency(InFrequency)
			, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BlockSize(InSettings.GetNumFramesPerBlock())
			, SampleRate(InSettings.GetSampleRate())
		{
			// verify our buffer sizes:
			check(AudioOutput->Num() == BlockSize);

			OnePoleLowPassFilter.Init(SampleRate, 1); // mono
		}
	};

	FOnePoleLowPassFilterNode::FOnePoleLowPassFilterNode(const FString& InInstanceName, const FGuid& InInstanceID)
		: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FOnePoleLowPassFilterOperator>())
	{
	}

	FOnePoleLowPassFilterNode::FOnePoleLowPassFilterNode(const FNodeInitData& InInitData)
		: FOnePoleLowPassFilterNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	const FNodeClassMetadata& FOnePoleLowPassFilterOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("One-Pole Low Pass Filter"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_OnePoleLpfNodeDisplayName", "One-Pole Low Pass Filter");
			Info.Description = LOCTEXT("One_Pole_Low_Pass_Filter_NodeDescription", "One-Pole Low Pass Filter"),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::Filters);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FVertexInterface FOnePoleLowPassFilterOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("In"), LOCTEXT("AudioInputTooltip", "Audio Input")),
				TInputDataVertexModel<float>(TEXT("Freq."), LOCTEXT("FrequencyTooltip", "Controls filter cutoff"), 20000.f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("FilterOutputToolTip", "Audio Output"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FOnePoleLowPassFilterOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FOnePoleLowPassFilterNode& OnePoleLowPassFilterNode = static_cast<const FOnePoleLowPassFilterNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// inputs
		FAudioBufferReadRef AudioIn = InputDataRefs.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("In"), InParams.OperatorSettings);
		FFloatReadRef FrequencyIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Freq."));

		return MakeUnique<FOnePoleLowPassFilterOperator>(
			InParams.OperatorSettings
			, AudioIn
			, FrequencyIn
			);
	}

	void FOnePoleLowPassFilterOperator::Execute()
	{
		OnePoleLowPassFilter.StartFrequencyInterpolation(*Frequency);
		OnePoleLowPassFilter.ProcessAudioBuffer(AudioInput->GetData(), AudioOutput->GetData(), AudioInput->Num());
		OnePoleLowPassFilter.StopFrequencyInterpolation();
	}

	METASOUND_REGISTER_NODE(FOnePoleLowPassFilterNode);

#pragma endregion


#pragma region OnePoleHighPass Filter

	class FOnePoleHighPassFilterOperator : public TExecutableOperator<FOnePoleHighPassFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;
	public:
		static const FNodeClassMetadata& GetNodeInfo();

		static FVertexInterface DeclareVertexInterface();

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(AudioInput));
			InputDataReferences.AddDataReadReference(TEXT("Freq."), FFloatReadRef(Frequency));

			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			// expose read access to our output buffer for other processors in the graph
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("Out"), FAudioBufferReadRef(AudioOutput));

			return OutputDataReferences;
		}

		void Execute();


	private: // members
		// input pins
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Frequency;

		// output pins
		FAudioBufferWriteRef AudioOutput;

		// data
		const int32 BlockSize;
		float SampleRate;

		// dsp
		Audio::FInterpolatedHPF OnePoleHighPassFilter;


	public:
		// ctor
		FOnePoleHighPassFilterOperator(
			const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FFloatReadRef& InFrequency
		)
			: AudioInput(InAudioInput)
			, Frequency(InFrequency)
			, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BlockSize(InSettings.GetNumFramesPerBlock())
			, SampleRate(InSettings.GetSampleRate())
		{
			// verify our buffer sizes:
			check(AudioOutput->Num() == BlockSize);

			OnePoleHighPassFilter.Init(SampleRate, 1); // mono
		}
	};

	FOnePoleHighPassFilterNode::FOnePoleHighPassFilterNode(const FString& InInstanceName, const FGuid& InInstanceID)
		: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FOnePoleHighPassFilterOperator>())
	{
	}

	FOnePoleHighPassFilterNode::FOnePoleHighPassFilterNode(const FNodeInitData& InInitData)
		: FOnePoleHighPassFilterNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	const FNodeClassMetadata& FOnePoleHighPassFilterOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("One-Pole High Pass Filter"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_OnePoleHpfNodeDisplayName", "One-Pole High Pass Filter");
			Info.Description = LOCTEXT("One_Pole_High_Pass_Filter_NodeDescription", "One-Pole High Pass Filter"),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::Filters);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FVertexInterface FOnePoleHighPassFilterOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("In"), LOCTEXT("AudioInputTooltip", "Audio In")),
				TInputDataVertexModel<float>(TEXT("Freq."), LOCTEXT("FrequencyTooltip", "Freq In"), 10.f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("FilterOutputToolTip", "Audio Out"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FOnePoleHighPassFilterOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FOnePoleHighPassFilterNode& OnePoleHighPassFilterNode = static_cast<const FOnePoleHighPassFilterNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// inputs
		FAudioBufferReadRef AudioIn = InputDataRefs.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("In"), InParams.OperatorSettings);
		FFloatReadRef FrequencyIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Freq."));

		return MakeUnique<FOnePoleHighPassFilterOperator>(
			InParams.OperatorSettings
			, AudioIn
			, FrequencyIn
			);
	}

	void FOnePoleHighPassFilterOperator::Execute()
	{
		OnePoleHighPassFilter.StartFrequencyInterpolation(*Frequency);
		OnePoleHighPassFilter.ProcessAudioBuffer(AudioInput->GetData(), AudioOutput->GetData(), AudioInput->Num());
		OnePoleHighPassFilter.StopFrequencyInterpolation();
	}

	METASOUND_REGISTER_NODE(FOnePoleHighPassFilterNode);

#pragma endregion


#pragma region Biquad Filter
	DECLARE_METASOUND_ENUM(Audio::EBiquadFilter::Type, Audio::EBiquadFilter::Lowpass,
	METASOUNDSTANDARDNODES_API, FEnumEBiquadFilterType, FEnumBiQuadFilterTypeInfo, FEnumBiQuadFilterReadRef, FEnumBiQuadFilterWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(Audio::EBiquadFilter::Type, FEnumEBiquadFilterType, "BiquadFilterType")
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::Lowpass, LOCTEXT("LpDescription", "Low Pass"), LOCTEXT("LpDescriptionTT", "Low Pass Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::Highpass, LOCTEXT("HpDescription", "High Pass"), LOCTEXT("HpDescriptionTT", "High Pass Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::Bandpass, LOCTEXT("BpDescription", "Band Pass"), LOCTEXT("BpDescriptionTT", "Band Pass Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::Notch, LOCTEXT("NotchDescription", "Notch "), LOCTEXT("NotchDescriptionTT", "Notch Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::ParametricEQ, LOCTEXT("ParaEqDescription", "Parametric EQ"), LOCTEXT("ParaEqDescriptionTT", "Parametric EQ Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::LowShelf, LOCTEXT("LowShelfDescription", "Low Shelf"), LOCTEXT("LowShelfDescriptionTT", "Low Shelf Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::HighShelf, LOCTEXT("HighShelfDescription", "High Shelf"), LOCTEXT("HighShelfDescriptionTT", "High Shelf Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::AllPass, LOCTEXT("AllPassDescription", "All Pass"), LOCTEXT("AllPassDescriptionTT", "All Pass Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::ButterworthLowPass, LOCTEXT("LowPassButterDescription", "Butterworth Low Pass"), LOCTEXT("LowPassButterDescriptionTT", "Butterworth Low Pass Biquad Filter")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EBiquadFilter::ButterworthHighPass, LOCTEXT("HighPassButterDescription", "Butterworth High Pass"), LOCTEXT("HighPassButterDescriptionTT", "Butterworth High Pass Biquad Filter"))
		DEFINE_METASOUND_ENUM_END()

		class FBiquadFilterOperator : public TExecutableOperator<FBiquadFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;
	public:
		static const FNodeClassMetadata& GetNodeInfo();

		static FVertexInterface DeclareVertexInterface();

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(AudioInput));
			InputDataReferences.AddDataReadReference(TEXT("Freq."), FFloatReadRef(Frequency));
			InputDataReferences.AddDataReadReference(TEXT("Bandwidth"), FFloatReadRef(Bandwidth));
			InputDataReferences.AddDataReadReference(TEXT("FilterGainDb"), FFloatReadRef(FilterGainDb));
			InputDataReferences.AddDataReadReference(TEXT("Type"), FEnumBiQuadFilterReadRef(FilterType));

			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			// expose read access to our output buffer for other processors in the graph
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("Out"), FAudioBufferReadRef(AudioOutput));

			return OutputDataReferences;
		}

		void Execute();


	private: // members
		// input pins
		FAudioBufferReadRef AudioInput;
		FFloatReadRef Frequency;
		FFloatReadRef Bandwidth;
		FFloatReadRef FilterGainDb;
		FEnumBiQuadFilterReadRef FilterType;

		// cached data
		float PreviousFrequency{ InvalidValue };
		float PreviousBandwidth{ InvalidValue };
		float PreviousFilterGainDb{ InvalidValue };

		// output pins
		FAudioBufferWriteRef AudioOutput;

		// data
		const int32 BlockSize;
		float SampleRate;
		float MaxCutoffFrequency;

		// dsp
		Audio::EBiquadFilter::Type PreviousFilterType;
		Audio::FBiquadFilter BiquadFilter;


	public:
		// ctor
		FBiquadFilterOperator(
			const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FFloatReadRef& InFrequency,
			const FFloatReadRef& InBandwidth,
			const FFloatReadRef& InFilterGainDb,
			const FEnumBiQuadFilterReadRef& InFilterType
		)
			: AudioInput(InAudioInput)
			, Frequency(InFrequency)
			, Bandwidth(InBandwidth)
			, FilterGainDb(InFilterGainDb)
			, FilterType(InFilterType)
			, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
			, BlockSize(InSettings.GetNumFramesPerBlock())
			, SampleRate(InSettings.GetSampleRate())
			, MaxCutoffFrequency(0.5f * SampleRate)
			, PreviousFilterType(*FilterType)
		{
			// verify our buffer sizes:
			check(AudioOutput->Num() == BlockSize);
			BiquadFilter.Init(SampleRate, 1, *FilterType);
		}
	};

	FBiquadFilterNode::FBiquadFilterNode(const FString& InInstanceName, const FGuid& InInstanceID)
		: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FBiquadFilterOperator>())
	{
	}

	FBiquadFilterNode::FBiquadFilterNode(const FNodeInitData& InInitData)
		: FBiquadFilterNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	const FNodeClassMetadata& FBiquadFilterOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Biquad Filter"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_BiquadFilterNodeDisplayName", "Biquad Filter");
			Info.Description = LOCTEXT("Biquad_Filter_NodeDescription", "Biquad filter"),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::Filters);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FVertexInterface FBiquadFilterOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("In"), LOCTEXT("AudioInputTooltip", "Audio Input")),
				TInputDataVertexModel<float>(TEXT("Freq."), LOCTEXT("FrequencyTooltip", "Controls Filter Cutoff"), 20000.f),
				TInputDataVertexModel<float>(TEXT("Bandwidth"), LOCTEXT("BandwidthTooltip", "Bandwidth Control"), 1.f),
				TInputDataVertexModel<float>(TEXT("FilterGainDb"), LOCTEXT("FilterGainDbTooltip", "Filter Gain Control (decibels)"), 0.f), 
				TInputDataVertexModel<FEnumEBiquadFilterType>(TEXT("Type"), LOCTEXT("FilterTypeDescription", "Biquad Filter Type"))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("FilterOutputToolTip", "Audio Output"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FBiquadFilterOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FBiquadFilterNode& BiquadFilterNode = static_cast<const FBiquadFilterNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// inputs
		FAudioBufferReadRef AudioIn = InputDataRefs.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("In"), InParams.OperatorSettings);
		FFloatReadRef FrequencyIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Freq."));
		FFloatReadRef BandwidthIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("Bandwidth"));
		FFloatReadRef FilterGainDbIn = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("FilterGainDb"));
			FEnumBiQuadFilterReadRef FilterType = InputDataRefs.GetDataReadReferenceOrConstruct<FEnumEBiquadFilterType>(TEXT("Type"));

		return MakeUnique<FBiquadFilterOperator>(
			InParams.OperatorSettings
			, AudioIn
			, FrequencyIn
			, BandwidthIn
			, FilterGainDbIn
			, FilterType
			);
	}

	void FBiquadFilterOperator::Execute()
	{
		const float CurrentFrequency = FMath::Clamp(*Frequency, 0.f, MaxCutoffFrequency);
		const float CurrentBandwidth = FMath::Max(*Bandwidth, 0.f);
		const float CurrentFilterGainDb = *FilterGainDb;

		if (!FMath::IsNearlyEqual(PreviousFrequency, CurrentFrequency))
		{
			BiquadFilter.SetFrequency(CurrentFrequency);
			PreviousFrequency = CurrentFrequency;
		}

		if (!FMath::IsNearlyEqual(PreviousBandwidth, CurrentBandwidth))
		{
			BiquadFilter.SetBandwidth(CurrentBandwidth);
			PreviousBandwidth = CurrentBandwidth;
		}

		if (!FMath::IsNearlyEqual(PreviousFilterGainDb, CurrentFilterGainDb))
		{
			BiquadFilter.SetGainDB(CurrentFilterGainDb);
			PreviousFilterGainDb = CurrentFilterGainDb;
		}

		if (*FilterType != PreviousFilterType)
		{
			BiquadFilter.SetType(*FilterType);
			PreviousFilterType = *FilterType;
		}

		BiquadFilter.ProcessAudio(AudioInput->GetData(), AudioInput->Num(), AudioOutput->GetData());
	}

	METASOUND_REGISTER_NODE(FBiquadFilterNode);

#pragma endregion


} // namespace Metasound



#undef LOCTEXT_NAMESPACE //MetasoundBasicFilterNodes