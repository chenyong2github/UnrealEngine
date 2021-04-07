// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundOscillatorNodes.h"

#include "DSP/Dsp.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_OscillatorNodes"

namespace Metasound
{			
#pragma region Common

	// Contained here are a small selection of block generators and sample generators.
	// They are templatized to allow quick construction of new oscillators, but aren't considered
	// optimal (as they contain many branches) and they should be only as a default 
	// until Vectorized versions replace them.

	namespace Generators
	{
		// Standard params passed to the Generate Block templates below
		struct FGeneratorArgs
		{
			float SampleRate = 0.f;
			float FrequencyHz = 0.f;
			float GlideEaseFactor = 0.0f;
			float PulseWidth = 0.f;
			TArrayView<float> AlignedBuffer;
			TArrayView<const float> FM;
		};

		// Functor used when we need to wrap the phase of some of the oscillators. Sinf for example does not care
		struct FWrapPhase
		{
			FORCEINLINE void operator()(float& InPhase) const
			{
				while (InPhase >= 1.f)
				{
					InPhase -= 1.f;
				}
			}
		};

		// Functor that does nothing to the phase and lets it climb indefinitely 
		struct FPhaseLetClimb
		{
			FORCEINLINE void operator()(const float&) const {};
		};

		// Smooth out the edges of the saw based on its current frequency
		// using a polynomial to smooth it at the discontinuity. This 
		// limits aliasing by avoiding the infinite frequency at the discontinuity.
		FORCEINLINE float PolySmoothSaw(const float InPhase, const float InPhaseDelta)
		{	
			float Output = 0.0f;

			// The current phase is on the left side of discontinuity
			if (InPhase > 1.0f - InPhaseDelta)
			{
				const float Dist = (InPhase - 1.0f) / InPhaseDelta;
				Output = -Dist * Dist - 2.0f * Dist - 1.0f;
			}
			// The current phase is on the right side of the discontinuity
			else if (InPhase < InPhaseDelta)
			{
				// Distance into polynomial
				const float Dist = InPhase / InPhaseDelta;
				Output = Dist * Dist - 2.0f * Dist + 1.0f;
			}
			return Output;
		}

		// Functor that does the a generic generate block with a supplied Oscillator.
		// The PhaseWrap functor controls how the phase is accumulated and wrapped
		template<
			typename Oscillator,
			typename PhaseWrap = FWrapPhase
		>
		struct TGenerateBlock
		{
			float Phase = 0.f;
			Oscillator Osc;
			PhaseWrap Wrap;
			float CurrentFade = 0.0f;
			float FadeSmooth = 0.01f;
			float CurrentFreq = -1.f;
		
			void operator()(const FGeneratorArgs& InArgs)
			{
				int32 RemainingSamplesInBlock = InArgs.AlignedBuffer.Num();
				float* Out = InArgs.AlignedBuffer.GetData();
				const float OneOverSampleRate = 1.f / InArgs.SampleRate;
				
				// Constant Freq between this an last update? 
				if (FMath::IsNearlyEqual(InArgs.FrequencyHz, CurrentFreq) || CurrentFreq < 0.f || FMath::IsNearlyEqual(InArgs.GlideEaseFactor, 1.0f))
				{
					CurrentFreq = InArgs.FrequencyHz;
					const float DeltaPhase = InArgs.FrequencyHz * OneOverSampleRate;
					while (RemainingSamplesInBlock > 0)
					{
						Wrap(Phase);

						float Output = Osc(Phase, DeltaPhase, InArgs);
						Output *= CurrentFade;
						*Out++ = Output;

						CurrentFade = CurrentFade + FadeSmooth * (1.0f - CurrentFade);

						Phase += DeltaPhase;

						RemainingSamplesInBlock--;
					}
				}
 				else
 				{			
					while (RemainingSamplesInBlock > 0)
					{
						Wrap(Phase);

						const float DeltaPhase = CurrentFreq * OneOverSampleRate;
						
						float Output = Osc(Phase, DeltaPhase, InArgs);
						Output *= CurrentFade;
						*Out++ = Output;

						CurrentFade = CurrentFade + FadeSmooth * (1.0f - CurrentFade);

						Phase += DeltaPhase;

						// lerp frequency based on the glide factor
						CurrentFreq = CurrentFreq + InArgs.GlideEaseFactor * (InArgs.FrequencyHz - CurrentFreq);

						RemainingSamplesInBlock--;
					}
				}
			}
		};

		// Functor that does the a generic generate block with a supplied Oscillator and applies an FM signal
		// to the frequency per sample.
		template<
			typename Oscillator,
			typename PhaseWrap = FWrapPhase
		>
		struct TGenerateBlockFM
		{
			float Phase = 0.f;
			Oscillator Osc;
			PhaseWrap Wrap;

			float CurrentFade = 0.0f;
			float FadeSmooth = 0.01f;
			float CurrentFreq = -1.f;

			void operator()(const FGeneratorArgs& InArgs)
			{
				int32 RemainingSamplesInBlock = InArgs.AlignedBuffer.Num();
				float* Out = InArgs.AlignedBuffer.GetData();
				const float* FM = InArgs.FM.GetData();
				const float OneOverSampleRate = 1.f / InArgs.SampleRate;

				// Constant Base Freq between this and last update? 
				if (FMath::IsNearlyEqual(InArgs.FrequencyHz, CurrentFreq) || CurrentFreq < 0.f || FMath::IsNearlyEqual(InArgs.GlideEaseFactor, 1.0f))
				{
					while (RemainingSamplesInBlock > 0)
					{
						const float PerSampleFreq = InArgs.FrequencyHz + *FM++;
						const float DeltaPhase = PerSampleFreq * OneOverSampleRate;

						Wrap(Phase);

						float Output = Osc(Phase, DeltaPhase, InArgs);
						Output *= CurrentFade;
						*Out++ = Output;
												
						CurrentFade = CurrentFade + FadeSmooth * (1.0f - CurrentFade);

						Phase += DeltaPhase;

						RemainingSamplesInBlock--;
					}

					CurrentFreq = InArgs.FrequencyHz;
				}
				else
				{	
					while (RemainingSamplesInBlock > 0)
					{
						const float ModulatedFreqSum = CurrentFreq + *FM++;
						const float DeltaPhase = ModulatedFreqSum * OneOverSampleRate;

						Wrap(Phase);

						float Output = Osc(Phase, DeltaPhase, InArgs);
						Output *= CurrentFade;
						*Out++ = Output;
						
						CurrentFade = CurrentFade + FadeSmooth * (1.0f - CurrentFade);

						Phase += DeltaPhase;

						// lerp frequency based on the glide factor
						CurrentFreq = CurrentFreq + InArgs.GlideEaseFactor * (InArgs.FrequencyHz - CurrentFreq);

						RemainingSamplesInBlock--;
					}
				}
			}
		};	
		
		// Sine types and generators.
		struct FSinfGenerator
		{
			FORCEINLINE float operator()(float InPhase, float, const FGeneratorArgs&)
			{
				static constexpr float TwoPi = 2.f * PI;
				return FMath::Sin(InPhase * TwoPi);
			}
		};
		struct FBhaskaraGenerator
		{
			float operator()(float InPhase, float, const FGeneratorArgs&) const
			{
				static const float TwoPi = 2.f * PI;
				float PhaseRadians = InPhase * TwoPi;
				return Audio::FastSin3(PhaseRadians - PI); // Expects [-PI, PI] 
			}
		};
		struct FSineWaveTableGenerator
		{
			static const TArray<float>& GetWaveTable()
			{
				auto MakeSineTable = []() -> const TArray<float>
				{
					int32 TableSize = 4096;
					// Generate the table
					TArray<float> WaveTable;
					WaveTable.AddUninitialized(TableSize);
					float* WaveTableData = WaveTable.GetData();
					for (int32 i = 0; i < TableSize; ++i)
					{
						static const float TwoPi = 2.f * PI;
						float Phase = (float)i / TableSize;
						WaveTableData[i] = FMath::Sin(Phase * TwoPi);
					}
					return WaveTable;
				};

				static const TArray<float> SineWaveTable = MakeSineTable();
				return SineWaveTable;
			}

			float operator()(float InPhase, float, const FGeneratorArgs&) const
			{			
				const TArray<float>& WaveTable = GetWaveTable();
				int32 LastIndex = WaveTable.Num() - 1;
				int32 TableIndex = (int32)(InPhase * (float)(LastIndex));
				TableIndex = FMath::Wrap(TableIndex, 0, LastIndex);
				return WaveTable[TableIndex];
			}
		};

		struct F2DRotatorGenerateBlock
		{
			Audio::FSinOsc2DRotation Rotator;
			F2DRotatorGenerateBlock(float InStartingLinearPhase)
				: Rotator{ 2.f * PI * InStartingLinearPhase }
			{}
			void operator()(const FGeneratorArgs& Args)
			{
				Rotator.GenerateBuffer(Args.SampleRate, Args.FrequencyHz, Args.AlignedBuffer.GetData(), Args.AlignedBuffer.Num());
			}
		};		
		
		using FSinf						= TGenerateBlock<FSinfGenerator>;
		using FSinfWithFm				= TGenerateBlockFM<FSinfGenerator>;
		
		using FBhaskara					= TGenerateBlock<FBhaskaraGenerator>;
		using FBhaskaraWithFm			= TGenerateBlockFM<FBhaskaraGenerator>;

		using FSineWaveTable			= TGenerateBlock<FSineWaveTableGenerator>;
		using FSineWaveTableWithFm		= TGenerateBlockFM<FSineWaveTableGenerator>;

		// Saws.
		struct FSawGenerator
		{
			FORCEINLINE float operator()(float InPhase, float, const FGeneratorArgs&)
			{
				return -1.f + (2.f * InPhase);
			}
		};
		struct FSawPolySmoothGenerator
		{
			FORCEINLINE float operator()(float InPhase, float InPhaseDelta, const FGeneratorArgs&)
			{
				// Two-sided wave-shaped sawtooth
				static const float A = Audio::FastTanh(1.5f);
				float Output = Audio::GetBipolar(InPhase);
				Output = Audio::FastTanh(1.5f * Output) / A;
				Output += PolySmoothSaw(InPhase, InPhaseDelta);
				return Output;
			}
		};

		using FSaw							= TGenerateBlock<FSawGenerator>;
		using FSawWithFm					= TGenerateBlockFM<FSawGenerator>;

		using FSawPolysmooth				= TGenerateBlock<FSawPolySmoothGenerator>;
		using FSawPolysmoothWithFm			= TGenerateBlockFM<FSawPolySmoothGenerator>;

		// Square.
		struct FSquareGenerator
		{
			FORCEINLINE float operator()(float InPhase, float, const FGeneratorArgs&)
			{
				// Does not obey pulse width.
				return InPhase >= 0.5f ? 1.f : -1.f;
			}
		};
		struct FSquarePolysmoothGenerator
		{
			float operator()(float InPhase, float InPhaseDelta, const FGeneratorArgs& InArgs)
			{
				// Taken piecemeal from Osc.cpp
				// Lots of branches.

				// First generate a smoothed sawtooth
				float SquareSaw1 = Audio::GetBipolar(InPhase);
				SquareSaw1 += PolySmoothSaw(InPhase, InPhaseDelta);

				// Create a second sawtooth that is phase-shifted based on the pulsewidth
				float NewPhase = 0.0f;
				if (InPhaseDelta > 0.0f)
				{
					NewPhase = InPhase + InArgs.PulseWidth;
					if (NewPhase >= 1.0f)
					{
						NewPhase -= 1.0f;
					}
				}
				else
				{
					NewPhase = InPhase - InArgs.PulseWidth;
					if (NewPhase <= 0.0f)
					{
						NewPhase += 1.0f;
					}
				}

				float SquareSaw2 = Audio::GetBipolar(NewPhase);
				SquareSaw2 += PolySmoothSaw(NewPhase, InPhaseDelta);

				// Subtracting 2 saws creates a square wave!
				float Output = 0.5f * SquareSaw1 - 0.5f * SquareSaw2;

				// Apply DC correction
				const float Correction = (InArgs.PulseWidth < 0.5f) ?
					(1.0f / (1.0f - InArgs.PulseWidth)) : (1.0f / InArgs.PulseWidth);

				Output *= Correction;
				return Output;
			}
		};

		using FSquare						= TGenerateBlock<FSquareGenerator>;
		using FSquareWithFm					= TGenerateBlockFM<FSquareGenerator>;

		using FSquarePolysmooth				= TGenerateBlock<FSquarePolysmoothGenerator>;
		using FSquarePolysmoothWithFm		= TGenerateBlockFM<FSquarePolysmoothGenerator>;

		// Triangle.
		struct FTriangleGenerator
		{
			float TriangleSign = -1.0f;
			float PrevPhase = -1.0f;
			float DPW_z1 = 0.0f;

			float operator()(float InPhase, float InPhaseDelta, const FGeneratorArgs& InArgs)
			{
				// if the oscillator phase wrapped, our previous phase will be higher than current phase
				if (PrevPhase > InPhase)
				{
					// Flip the sign if we wrapped phase
					TriangleSign *= -1.0f;
				}
				PrevPhase = InPhase;


				// Get saw trivial output
				float SawToothOutput = Audio::GetBipolar(InPhase);
				const float SawSquaredInvMod = (1.0f - SawToothOutput * SawToothOutput) * TriangleSign;

				const float Differentiated = SawSquaredInvMod - DPW_z1;
				DPW_z1 = SawSquaredInvMod;
				return Differentiated * InArgs.SampleRate / (4.0f * InArgs.FrequencyHz * (1.0f - InPhaseDelta));
			}
		};

		using FTriangle						= TGenerateBlock<FTriangleGenerator>;
		using FTriangleWithFm				= TGenerateBlockFM<FTriangleGenerator>;

		// TODO: make a true polysmooth version
		using FTrianglePolysmooth = TGenerateBlock<FTriangleGenerator>;
		using FTrianglePolysmoothWithFm = TGenerateBlockFM<FTriangleGenerator>;
	}

	// Base class of Oscillator factories which holds common the interface.
	class FOscilatorFactoryBase : public IOperatorFactory
	{
	public:
		// Common pins
		static constexpr const TCHAR* EnabledPinName = TEXT("Enabled");
		static constexpr const TCHAR* FrequencyModPinName = TEXT("Modulation");
		static constexpr const TCHAR* BaseFrequencyPinName = TEXT("Frequency");
		static constexpr const TCHAR* PhaseResetPinName = TEXT("Sync");
		static constexpr const TCHAR* PhaseOffsetPinName = TEXT("Phase Offset");
		static constexpr const TCHAR* GlideFactorPinName = TEXT("Glide");
		static constexpr const TCHAR* AudioOutPinName = TEXT("Audio");
		static constexpr const TCHAR* BiPolarPinName = TEXT("Bi Polar");
		static constexpr const TCHAR* MinOutputValuePinName = TEXT("Min Value");
		static constexpr const TCHAR* MaxOutputValuePinName = TEXT("Max Value");


		// Common to all Oscillators.
		static FVertexInterface GetCommmonVertexInterface()
		{
			static const FVertexInterface Interface
			{
				FInputVertexInterface{
					TInputDataVertexModel<bool>(EnabledPinName, LOCTEXT("OscActivateDescription", "Enable the oscillator."), true),
					TInputDataVertexModel<bool>(BiPolarPinName, LOCTEXT("OscBaseBiPolarDescription", "If the output is Bi-Polar (-1..1) or Uni-Polar (0..1)"), true),
					TInputDataVertexModel<float>(BaseFrequencyPinName, LOCTEXT("OscBaseFrequencyDescription", "Base Frequency of Oscillator in HZ"), 440.f),
					TInputDataVertexModel<FAudioBuffer>(FrequencyModPinName, LOCTEXT("OscModFrequencyDescription", "Modulation Frequency Input (for doing FM)")),
					TInputDataVertexModel<FTrigger>(PhaseResetPinName, LOCTEXT("OscPhaseResetDescription", "Phase Reset")),
					TInputDataVertexModel<float>(PhaseOffsetPinName, LOCTEXT("OscPhaseOffsetDescription", "Phase Offset In Degrees (0..360)"), 0.f),
					TInputDataVertexModel<float>(GlideFactorPinName, LOCTEXT("OscGlideFactorDescription", "The amount of glide to use when changing frequencies. 0.0 = no glide, 1.0 = lots of glide."), 0.f)
				},
				FOutputVertexInterface{
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"))
				}
			};
			return Interface;
		}
	};

	// Common set of construct params for each of the Oscillators.
	struct FOscillatorOperatorConstructParams
	{
		const FOperatorSettings& Settings;
		FBoolReadRef Enabled;
		FFloatReadRef BaseFrequency;
		FFloatReadRef PhaseOffset; 
		FTriggerReadRef PhaseReset;
		FFloatReadRef GlideFactor;
		FBoolReadRef BiPolar;
	};

	// Base Oscillator Operator CRTP.
	// Expects your Derived class to implement a Generate(int32 InStartFrame, int32 InEndFrame, float InFreq)
	template<typename GeneratorPolicy, typename Derived>
	class TOscillatorOperatorBase : public TExecutableOperator<Derived>
	{		
	public:
		TOscillatorOperatorBase(const FOscillatorOperatorConstructParams& InConstructParams)
			: Generator{*InConstructParams.PhaseOffset}
			, SampleRate(InConstructParams.Settings.GetSampleRate())
			, Nyquist(InConstructParams.Settings.GetSampleRate() / 2.0f)
			, Enabled(InConstructParams.Enabled)
			, BaseFrequency(InConstructParams.BaseFrequency)
			, PhaseReset(InConstructParams.PhaseReset)
			, PhaseOffset(InConstructParams.PhaseOffset)
			, GlideFactor(InConstructParams.GlideFactor)
			, BiPolar(InConstructParams.BiPolar)
			, AudioBuffer(FAudioBufferWriteRef::CreateNew(InConstructParams.Settings))
		{
			check(AudioBuffer->Num() == InConstructParams.Settings.GetNumFramesPerBlock());
		}

		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::EnabledPinName, Enabled);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::BaseFrequencyPinName, BaseFrequency);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::PhaseOffsetPinName, PhaseOffset);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::PhaseResetPinName, PhaseReset);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::GlideFactorPinName, GlideFactor);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::BiPolarPinName, BiPolar);
			return InputDataReferences;
		}

		FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(FOscilatorFactoryBase::AudioOutPinName, AudioBuffer);
			return OutputDataReferences;
		}

		void ResetPhase(float InPhaseInDegrees)
		{
			float LinearPhase = FMath::Clamp(InPhaseInDegrees, 0.f, 360.f) / 360.f;
			
			// Recreate the generator type with the phase requested.
			Generator = GeneratorPolicy{ LinearPhase };
		}

		void Execute()
		{
			// Clamp frequencies into Nyquist range.
			const float ClampedFreq = FMath::Clamp(*BaseFrequency, -Nyquist, Nyquist);
			const float ClampedGlideEase = Audio::GetLogFrequencyClamped(*GlideFactor, { 0.0f, 1.0f }, { 1.0f, 0.0001f });

			AudioBuffer->Zero();

			Derived* Self = static_cast<Derived*>(this);
			PhaseReset->ExecuteBlock
			(
				[Self, ClampedFreq, ClampedGlideEase](int32 InFrameStart, int32 InFrameEnd)
				{
					Self->Generate(InFrameStart, InFrameEnd, ClampedFreq, ClampedGlideEase);
				},
				[Self, ClampedFreq, ClampedGlideEase](int32 InFrameStart, int32 InFrameEnd)
				{
					Self->ResetPhase(*Self->PhaseOffset);
					Self->Generate(InFrameStart, InFrameEnd, ClampedFreq, ClampedGlideEase);
				}
			);

			// Convert to Unipolar
			if (*Enabled && !*BiPolar)
			{
				Audio::ConvertBipolarBufferToUnipolar(AudioBuffer->GetData(), AudioBuffer->Num());
			}
		}

	protected:
		GeneratorPolicy Generator;
		float SampleRate;
		float Nyquist;

		FBoolReadRef Enabled;
		FFloatReadRef BaseFrequency;
		FTriggerReadRef PhaseReset;
		FFloatReadRef PhaseOffset;
		FFloatReadRef GlideFactor;
		FBoolReadRef BiPolar;

		FAudioBufferWriteRef AudioBuffer;
	};

	// Generic Oscillator operator for NON-FM Operators. 
	template<typename GeneratorPolicy>
	class TOscillatorOperator final : public TOscillatorOperatorBase<GeneratorPolicy, TOscillatorOperator<GeneratorPolicy>>
	{
		using Super = TOscillatorOperatorBase<GeneratorPolicy, TOscillatorOperator<GeneratorPolicy>>;
	public:
		TOscillatorOperator(const FOscillatorOperatorConstructParams& InConstructParams)
			: Super(InConstructParams)
		{}

		void Generate(int32 InStartFrame, int32 InEndFrame, float InClampedFreq, float InClampedGlideEase)
		{		
			int32 NumFrames = InEndFrame - InStartFrame;

			if (*this->Enabled && NumFrames > 0)
			{
				this->Generator(
				{
					this->SampleRate, 
					InClampedFreq,
					InClampedGlideEase,
					0.f,
					MakeArrayView(this->AudioBuffer->GetData() + InStartFrame, NumFrames), // Not aligned.
				});
			}			
		}
	};

	// Generic Oscillator operator for FM Operators.
	template<typename TGeneratorPolicy>
	class TOscillatorOperatorFM final : public TOscillatorOperatorBase<TGeneratorPolicy, TOscillatorOperatorFM<TGeneratorPolicy>>
	{
		using Super = TOscillatorOperatorBase<TGeneratorPolicy, TOscillatorOperatorFM<TGeneratorPolicy>>;
	public:
		TOscillatorOperatorFM(const FOscillatorOperatorConstructParams& InCommonParams, const FAudioBufferReadRef& InFmData)
			: Super(InCommonParams), Fm(InFmData)
		{}

		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs = Super::GetInputs();
			Inputs.AddDataReadReference(FOscilatorFactoryBase::FrequencyModPinName, Fm);
			return Inputs;
		}

		void Generate(int32 InStartFrame, int32 InEndFrame, float InClampedFreq, float InClampedGlideEase)
		{
			int32 NumFrames = InEndFrame - InStartFrame;
			if (*this->Enabled && NumFrames > 0)
			{
				this->Generator(
				{
					this->SampleRate, 
					InClampedFreq,
					InClampedGlideEase,
					0.f,
					MakeArrayView(this->AudioBuffer->GetData() + InStartFrame, NumFrames), // Not aligned.
					MakeArrayView(this->Fm->GetData() + InStartFrame, NumFrames) // Not aligned.
				});
			}
		}		

	private:
		FAudioBufferReadRef Fm;
	};

	FOscilatorNodeBase::FOscilatorNodeBase(const FString& InInstanceName, const FGuid& InInstanceID, const FNodeClassMetadata& InInfo, const TSharedRef<IOperatorFactory, ESPMode::ThreadSafe>& InFactory, float InDefaultFrequency, float InDefaultGlideFactor, bool bInDefaultEnablement)
		: FNode(InInstanceName, InInstanceID, InInfo)
		, Factory(InFactory)
		, VertexInterface(GetMetadata().DefaultInterface)
		, DefaultFrequency(InDefaultFrequency)
		, DefaultGlideFactor(InDefaultGlideFactor)
		, bDefaultEnablement(bInDefaultEnablement)
	{}
	
#pragma endregion Common

#pragma region Sine

	enum class ESineGenerationType
	{
		Rotation,
		Sinf,
		FilterResonanceFeedback,
		Bhaskara,
		Wavetable,
	};

	DECLARE_METASOUND_ENUM(ESineGenerationType, ESineGenerationType::Wavetable, METASOUNDSTANDARDNODES_API,
		FEnumSineGenerationType, FEnumSineGenerationTypeInfo, FEnumSineGenerationTypeReadRef, FEnumSineGenerationTypeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(ESineGenerationType, FEnumSineGenerationType, "SineGenerationType")
		DEFINE_METASOUND_ENUM_ENTRY(ESineGenerationType::Rotation, LOCTEXT("RotationDescription", "2D Rotation"), LOCTEXT("RotationDescriptionTT", "Rotates around the unit circle generate the sine. Note: Glide and audio rate FM modulation is not supported with the 2D rotator.")),
		DEFINE_METASOUND_ENUM_ENTRY(ESineGenerationType::Sinf, LOCTEXT("SinfDescription", "Pure Math"), LOCTEXT("SinfDescriptionTT", "Uses the standard math library (Sinf) to generate the sine (most expensive)")),
		DEFINE_METASOUND_ENUM_ENTRY(ESineGenerationType::Bhaskara, LOCTEXT("BhaskaraDescription", "Bhaskara"), LOCTEXT("BhaskaraDescriptionTT", "Sine approximation using Bhaskara technique discovered in 7th century")),
		DEFINE_METASOUND_ENUM_ENTRY(ESineGenerationType::Wavetable, LOCTEXT("WavetableDescription", "Wavetable"), LOCTEXT("WavetableDescriptionTT", "Uses a wavetable to generate the sine")),
		//DEFINE_METASOUND_ENUM_ENTRY(ESineGenerationType::FilterResonanceFeedback, LOCTEXT("FRFDescription", "Filter resonance"), LOCTEXT("FRFDescriptionTT", "Filter resonance/feedback")),
	DEFINE_METASOUND_ENUM_END()

	class FSineOscilatorNode::FFactory : public FOscilatorFactoryBase
	{
	public:
		FFactory() = default;
		
		static constexpr const TCHAR* SineTypePin = TEXT("Type");

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Sine"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_SineNodeDisplayName", "Sine");
				Info.Description = LOCTEXT("Metasound_SineNodeDescription", "Emits an audio signal of a sinusoid.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Generators);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
		static const FVertexInterface& GetVertexInterface()
		{
			auto MakeInterface = []() -> FVertexInterface
			{
				FVertexInterface Interface = GetCommmonVertexInterface();
				Interface.GetInputInterface().Add(
					TInputDataVertexModel<FEnumSineGenerationType>(SineTypePin, LOCTEXT("SineTypeDescription", "Type of the Sinewave Generator"), static_cast<int32>(ESineGenerationType::Wavetable))
				);
				return Interface;
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}
		
		TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
		{
			const FSineOscilatorNode& SineNode = static_cast<const FSineOscilatorNode&>(InParams.Node);
			const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			using namespace Generators;
			FOscillatorOperatorConstructParams OpParams
			{
				Settings,
				InputCol.GetDataReadReferenceOrConstruct<bool>(EnabledPinName, SineNode.GetDefaultEnablement()),
				InputCol.GetDataReadReferenceOrConstruct<float>(BaseFrequencyPinName, SineNode.GetDefaultFrequency()),
				InputCol.GetDataReadReferenceOrConstruct<float>(PhaseOffsetPinName, SineNode.GetDefaultPhaseOffset()),
				InputCol.GetDataReadReferenceOrConstruct<FTrigger>(PhaseResetPinName, Settings),
				InputCol.GetDataReadReferenceOrConstruct<float>(GlideFactorPinName, SineNode.GetDefaultGlideFactor()),
				InputCol.GetDataReadReferenceOrConstruct<bool>(BiPolarPinName, true)
			};

			// TODO: Make this a static prop. For now its a pin.
			
			// Check to see if we have an FM input connected.
			bool bHasFM = InputCol.ContainsDataReadReference<FAudioBuffer>(FrequencyModPinName);			
			FEnumSineGenerationTypeReadRef Type = InputCol.GetDataReadReferenceOrConstruct<FEnumSineGenerationType>(SineTypePin, ESineGenerationType::Rotation);
			if (bHasFM)
			{

				// FM Oscillators.
				FAudioBufferReadRef FmBuffer = InputCol.GetDataReadReference<FAudioBuffer>(FrequencyModPinName);
				switch (*Type)
				{
				default:
				case ESineGenerationType::Sinf: return MakeUnique<TOscillatorOperatorFM<FSinfWithFm>>(OpParams, FmBuffer);
				case ESineGenerationType::Bhaskara: return MakeUnique<TOscillatorOperatorFM<FBhaskaraWithFm>>(OpParams, FmBuffer);
				case ESineGenerationType::Wavetable: return MakeUnique<TOscillatorOperatorFM<FSineWaveTableWithFm>>(OpParams, FmBuffer);
				}
			}
			else //HasFM
			{
				switch (*Type)
				{
				default:
				case ESineGenerationType::Sinf: return MakeUnique<TOscillatorOperator<FSinf>>(OpParams);
				case ESineGenerationType::Rotation: return MakeUnique<TOscillatorOperator<F2DRotatorGenerateBlock>>(OpParams);
				case ESineGenerationType::Bhaskara: return MakeUnique<TOscillatorOperator<FBhaskara>>(OpParams);
				case ESineGenerationType::Wavetable: return MakeUnique<TOscillatorOperator<FSineWaveTable>>(OpParams);
				}
			} // HasFM
			return nullptr;
		}
	private:
	};

	FSineOscilatorNode::FSineOscilatorNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultFrequency, float InDefautlGlideFactor, bool bInDefaultEnablement)
		: FOscilatorNodeBase(InInstanceName,InInstanceID, FFactory::GetNodeInfo(), MakeShared<FFactory, ESPMode::ThreadSafe>(), InDefaultFrequency, InDefautlGlideFactor, bInDefaultEnablement )
	{}

	FSineOscilatorNode::FSineOscilatorNode(const FNodeInitData& InInitData)
		: FSineOscilatorNode(InInitData.InstanceName, InInitData.InstanceID, 440.0f, 0.0f, true)
	{}

	METASOUND_REGISTER_NODE(FSineOscilatorNode);

#pragma endregion Sine

#pragma region Saw

	enum class ESawGenerationType
	{
		PolySmooth,
		Trivial,
		Wavetable
	};

	DECLARE_METASOUND_ENUM(ESawGenerationType, ESawGenerationType::PolySmooth, METASOUNDSTANDARDNODES_API,
		FEnumSawGenerationType, FEnumSawGenerationTypeInfo, FSawGenerationTypeReadRef, FEnumSawGenerationTypeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(ESawGenerationType, FEnumSawGenerationType, "SawGenerationType")
		DEFINE_METASOUND_ENUM_ENTRY(ESawGenerationType::PolySmooth, LOCTEXT("SawPolySmoothDescription", "Poly Smooth"), LOCTEXT("PolySmoothDescriptionTT", "PolySmooth (i.e. BLEP)")),
		DEFINE_METASOUND_ENUM_ENTRY(ESawGenerationType::Trivial, LOCTEXT("SawTrivialDescription", "Trivial"), LOCTEXT("TrivialDescriptionTT", "The most basic raw implementation")),
		//DEFINE_METASOUND_ENUM_ENTRY(ESawGenerationType::Wavetable, LOCTEXT("SawWavetableDescription", "Wavetable"), LOCTEXT("SawWavetableDescriptionTT", "Use a Wavetable iterpolation to generate the Waveform"))
	DEFINE_METASOUND_ENUM_END()

	class FSawOscilatorNode::FFactory : public FOscilatorFactoryBase
	{
	public:
		static constexpr const TCHAR* SawTypePin = TEXT("Type");
		
		FFactory() = default;

		TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Saw"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_SawNodeDisplayName", "Saw");
				Info.Description = LOCTEXT("Metasound_SawNodeDescription", "Emits a Saw wave");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Generators);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
		static const FVertexInterface& GetVertexInterface()
		{
			auto MakeInterface = []() -> FVertexInterface
			{
				FVertexInterface Interface = GetCommmonVertexInterface();
				Interface.GetInputInterface().Add(
					TInputDataVertexModel<FEnumSawGenerationType>(SawTypePin, LOCTEXT("Saw Type", "Which type of Saw Generator to Create"))
				);
				return Interface;
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}

	private:
	};

	TUniquePtr<Metasound::IOperator> FSawOscilatorNode::FFactory::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FSawOscilatorNode& Node = static_cast<const FSawOscilatorNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
		const FOperatorSettings& Settings = InParams.OperatorSettings;
		using namespace Generators;		

		FSawGenerationTypeReadRef Type = InputCol.GetDataReadReferenceOrConstruct<FEnumSawGenerationType>(SawTypePin);

		FOscillatorOperatorConstructParams OpParams
		{
			Settings,
			InputCol.GetDataReadReferenceOrConstruct<bool>(EnabledPinName, Node.GetDefaultEnablement()),
			InputCol.GetDataReadReferenceOrConstruct<float>(BaseFrequencyPinName, Node.GetDefaultFrequency()),
			InputCol.GetDataReadReferenceOrConstruct<float>(PhaseOffsetPinName, Node.GetDefaultPhaseOffset()),
			InputCol.GetDataReadReferenceOrConstruct<FTrigger>(PhaseResetPinName, Settings),
			InputCol.GetDataReadReferenceOrConstruct<float>(GlideFactorPinName, Node.GetDefaultGlideFactor()),
			InputCol.GetDataReadReferenceOrConstruct<bool>(BiPolarPinName, true)
		};

		bool bHasFM = InputCol.ContainsDataReadReference<FAudioBuffer>(FrequencyModPinName);

		if (bHasFM)
		{
			FAudioBufferReadRef FmBuffer = InputCol.GetDataReadReference<FAudioBuffer>(FrequencyModPinName);
			switch (*Type)
			{
			default:
			case ESawGenerationType::Trivial: return MakeUnique<TOscillatorOperatorFM<FSawWithFm>>(OpParams, FmBuffer);
			case ESawGenerationType::PolySmooth: return MakeUnique<TOscillatorOperatorFM<FSawPolysmoothWithFm>>(OpParams, FmBuffer);
			}
		}
		else
		{
			switch (*Type)
			{
			default:
			case ESawGenerationType::Trivial: return MakeUnique<TOscillatorOperator<FSaw>>(OpParams);
			case ESawGenerationType::PolySmooth: return MakeUnique<TOscillatorOperator<FSawPolysmooth>>(OpParams);
			}
		}
		return nullptr;
	}

	FSawOscilatorNode::FSawOscilatorNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultFrequency, float InDefaultGlideFactor, bool bInDefaultEnablement)
		: FOscilatorNodeBase(InInstanceName, InInstanceID, FFactory::GetNodeInfo(), MakeShared<FFactory, ESPMode::ThreadSafe>(), InDefaultFrequency, InDefaultGlideFactor, bInDefaultEnablement)
	{}

	FSawOscilatorNode::FSawOscilatorNode(const FNodeInitData& InInitData)
		: FSawOscilatorNode(InInitData.InstanceName, InInitData.InstanceID, 440.0f, 0.0f, true)
	{}

	METASOUND_REGISTER_NODE(FSawOscilatorNode);

#pragma endregion Saw

#pragma region Square

	enum class ESquareGenerationType
	{
		PolySmooth,
		Trivial,
		Wavetable
	};

	DECLARE_METASOUND_ENUM(ESquareGenerationType, ESquareGenerationType::PolySmooth, METASOUNDSTANDARDNODES_API,
	FEnumSquareGenerationType, FEnumSquareGenerationTypeInfo, FSquareGenerationTypeReadRef, FEnumSquareGenerationTypeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(ESquareGenerationType, FEnumSquareGenerationType, "SquareGenerationType")
		DEFINE_METASOUND_ENUM_ENTRY(ESquareGenerationType::PolySmooth, LOCTEXT("SquarePolySmoothDescription", "Poly Smooth"), LOCTEXT("PolySmoothDescriptionTT", "PolySmooth (i.e. BLEP)")),
		DEFINE_METASOUND_ENUM_ENTRY(ESquareGenerationType::Trivial, LOCTEXT("SquareTrivialDescription", "Trivial"), LOCTEXT("TrivialDescriptionTT", "The most basic raw implementation")),
		//DEFINE_METASOUND_ENUM_ENTRY(ESquareGenerationType::Wavetable, LOCTEXT("SquareWavetableDescription", "Wavetable"), LOCTEXT("SquareWavetableDescriptionTT", "Use a Wavetable interpolation to generate the Waveform"))
	DEFINE_METASOUND_ENUM_END()
	
	template<typename GeneratorPolicy>
	class FSquareOperator final : public TOscillatorOperatorBase<GeneratorPolicy, FSquareOperator<GeneratorPolicy>>
	{
		using Super = TOscillatorOperatorBase<GeneratorPolicy, FSquareOperator<GeneratorPolicy>>;
	public:
		FSquareOperator(const FOscillatorOperatorConstructParams& InConstructParams, const FFloatReadRef& InPulseWidth)
			: Super(InConstructParams)
			, PulseWidth(InPulseWidth)
		{}
		void Generate(int32 InStartFrame, int32 InEndFrame, float InClampedFreq, float InClampedGlideEase)
		{
			int32 NumFrames = InEndFrame - InStartFrame;
			float ClampedPulseWidth = FMath::Clamp(*PulseWidth, 0.0f, 1.0f);
			if (*this->Enabled && NumFrames > 0)
			{
				this->Generator(
				{
					this->SampleRate,
					InClampedFreq,
					InClampedGlideEase,
					ClampedPulseWidth,
					MakeArrayView(this->AudioBuffer->GetData() + InStartFrame, NumFrames), // Not aligned.
				});
			}
		}

	private:
		FFloatReadRef PulseWidth;
	};

	template<typename GeneratorPolicy>
	class FSquareOperatorFM final : public TOscillatorOperatorBase<GeneratorPolicy, FSquareOperatorFM<GeneratorPolicy>>
	{
		using Super = TOscillatorOperatorBase<GeneratorPolicy, FSquareOperatorFM<GeneratorPolicy>>;
	public:
		FSquareOperatorFM(const FOscillatorOperatorConstructParams& InConstructParams, const FFloatReadRef& InPulseWidth, const FAudioBufferReadRef& InFm)
			: Super(InConstructParams)
			, PulseWidth(InPulseWidth)
			, FM(InFm)
		{
			check(InFm->GetData());
			check(InConstructParams.Settings.GetNumFramesPerBlock() == InFm->Num());
		}
		void Generate(int32 InStartFrame, int32 InEndFrame, float InClampedFreq, float InClampedGlideEase)
		{
			int32 NumFrames = InEndFrame - InStartFrame;
			float ClampedPulseWidth = FMath::Clamp(*PulseWidth, 0.0f, 1.0f);
			if (*this->Enabled && NumFrames > 0)
			{
				this->Generator(
				{
					this->SampleRate,
					InClampedFreq,
					InClampedGlideEase,
					ClampedPulseWidth,
					MakeArrayView(this->AudioBuffer->GetData() + InStartFrame, NumFrames), // Not aligned.
					MakeArrayView(this->FM->GetData() + InStartFrame, NumFrames), // Not aligned.
				});
			}
		}

	private:
		FFloatReadRef PulseWidth;
		FAudioBufferReadRef FM;
	};

	class FSquareOscilatorNode::FFactory : public FOscilatorFactoryBase
	{
	public:
		FFactory() = default;

		static constexpr const TCHAR* PulseWidthPinName = TEXT("Pulse Width");
		static constexpr const TCHAR* SquareTypePinName = TEXT("Type");

		TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
		{
			const FSquareOscilatorNode& Node = static_cast<const FSquareOscilatorNode&>(InParams.Node);
			const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			using namespace Generators;

			FOscillatorOperatorConstructParams OpParams
			{
				Settings,
				InputCol.GetDataReadReferenceOrConstruct<bool>(EnabledPinName, Node.GetDefaultEnablement()),
				InputCol.GetDataReadReferenceOrConstruct<float>(BaseFrequencyPinName, Node.GetDefaultFrequency()),
				InputCol.GetDataReadReferenceOrConstruct<float>(PhaseOffsetPinName, Node.GetDefaultPhaseOffset()),
				InputCol.GetDataReadReferenceOrConstruct<FTrigger>(PhaseResetPinName, Settings),
				InputCol.GetDataReadReferenceOrConstruct<float>(GlideFactorPinName, Node.GetDefaultGlideFactor()),
				InputCol.GetDataReadReferenceOrConstruct<bool>(BiPolarPinName, true)
			};

			FSquareGenerationTypeReadRef Type = InputCol.GetDataReadReferenceOrConstruct<FEnumSquareGenerationType>(SquareTypePinName);

			bool bHasFM = InputCol.ContainsDataReadReference<FAudioBuffer>(FrequencyModPinName);
			FFloatReadRef PulseWidth = InputCol.GetDataReadReferenceOrConstruct<float>(PulseWidthPinName, 0.5f);

			if (bHasFM)
			{
				FAudioBufferReadRef FmBuffer = InputCol.GetDataReadReference<FAudioBuffer>(FrequencyModPinName);
				switch (*Type)
				{
				default:				
				case ESquareGenerationType::Trivial: return MakeUnique<FSquareOperatorFM<FSquareWithFm>>(OpParams, PulseWidth, FmBuffer);
				case ESquareGenerationType::PolySmooth: return MakeUnique<FSquareOperatorFM<FSquarePolysmoothWithFm>>(OpParams, PulseWidth, FmBuffer);
				}
			}
			else
			{
				switch (*Type)
				{
				default:
				case ESquareGenerationType::Trivial: return MakeUnique<FSquareOperator<FSquare>>(OpParams, PulseWidth);
				case ESquareGenerationType::PolySmooth: return MakeUnique<FSquareOperator<FSquarePolysmooth>>(OpParams, PulseWidth);
				}
			}
			return nullptr;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Square"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_SquareNodeDisplayName", "Square");
				Info.Description = LOCTEXT("Metasound_SquareNodeDescription", "Emits a Square wave");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Generators);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
		static const FVertexInterface& GetVertexInterface()
		{
			auto MakeInterface = []() -> FVertexInterface
			{
				FVertexInterface Interface = GetCommmonVertexInterface();
				Interface.GetInputInterface().Add(TInputDataVertexModel<FEnumSquareGenerationType>(SquareTypePinName, LOCTEXT("SquareTypeDescription", "The generator type to make the squarewave")));
				Interface.GetInputInterface().Add(TInputDataVertexModel<float>(PulseWidthPinName, LOCTEXT("PhaseWidthDescription", "The Width of the square part of the wave"), 0.5f));
				return Interface;
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}
	private:
	};
	
	FSquareOscilatorNode::FSquareOscilatorNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultFrequency, float InDefaultGlideFactor, bool bInDefaultEnablement)
		: FOscilatorNodeBase(InInstanceName, InInstanceID, FFactory::GetNodeInfo(), MakeShared<FFactory, ESPMode::ThreadSafe>(), InDefaultFrequency, InDefaultGlideFactor, bInDefaultEnablement)
	{}

	FSquareOscilatorNode::FSquareOscilatorNode(const FNodeInitData& InInitData)
		: FSquareOscilatorNode(InInitData.InstanceName, InInitData.InstanceID, 440.0f, 0.0f, true)
	{}

	METASOUND_REGISTER_NODE(FSquareOscilatorNode)
#pragma endregion Square

#pragma region Triangle

	enum class ETriangleGenerationType
	{
		PolySmooth,
		Trivial,
		Wavetable
	};

	DECLARE_METASOUND_ENUM(ETriangleGenerationType, ETriangleGenerationType::PolySmooth, METASOUNDSTANDARDNODES_API,
	FEnumTriangleGenerationType, FEnumTriangleGenerationTypeInfo, FTriangleGenerationTypeReadRef, FEnumTriangleGenerationTypeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(ETriangleGenerationType, FEnumTriangleGenerationType, "TriangleGenerationType")
		DEFINE_METASOUND_ENUM_ENTRY(ETriangleGenerationType::PolySmooth, LOCTEXT("TrianglePolySmoothDescription", "Poly Smooth"), LOCTEXT("PolySmoothDescriptionTT", "PolySmooth (i.e. BLEP)")),
		DEFINE_METASOUND_ENUM_ENTRY(ETriangleGenerationType::Trivial, LOCTEXT("TriangleTrivialDescription", "Trivial"), LOCTEXT("TrivialDescriptionTT", "The most basic raw implementation")),
		//DEFINE_METASOUND_ENUM_ENTRY(ETriangleGenerationType::Wavetable, LOCTEXT("TriangleWavetableDescription", "Wavetable"), LOCTEXT("TriangleWavetableDescriptionTT", "Use a Wavetable iterpolation to generate the Waveform"))
	DEFINE_METASOUND_ENUM_END()

	class FTriangleOscilatorNode::FFactory : public FOscilatorFactoryBase
	{
	public:
		FFactory() = default;

		static constexpr const TCHAR* TriangeTypePinName = TEXT("Type");

		TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
		{
			const FTriangleOscilatorNode& Node = static_cast<const FTriangleOscilatorNode&>(InParams.Node);
			const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			using namespace Generators;
			
			FTriangleGenerationTypeReadRef Type = InputCol.GetDataReadReferenceOrConstruct<FEnumTriangleGenerationType>(TriangeTypePinName);
		
			FOscillatorOperatorConstructParams OpParams
			{
				Settings,
				InputCol.GetDataReadReferenceOrConstruct<bool>(EnabledPinName, Node.GetDefaultEnablement()),
				InputCol.GetDataReadReferenceOrConstruct<float>(BaseFrequencyPinName, Node.GetDefaultFrequency()),
				InputCol.GetDataReadReferenceOrConstruct<float>(PhaseOffsetPinName, Node.GetDefaultPhaseOffset()),
				InputCol.GetDataReadReferenceOrConstruct<FTrigger>(PhaseResetPinName, Settings),
				InputCol.GetDataReadReferenceOrConstruct<float>(GlideFactorPinName, Node.GetDefaultGlideFactor()),
				InputCol.GetDataReadReferenceOrConstruct<bool>(BiPolarPinName, true)
			};

			bool bHasFM = InputCol.ContainsDataReadReference<FAudioBuffer>(FrequencyModPinName);
			
			if (bHasFM)
			{
				FAudioBufferReadRef FmBuffer = InputCol.GetDataReadReference<FAudioBuffer>(FrequencyModPinName);
				switch (*Type)
				{
				default:
				case ETriangleGenerationType::PolySmooth: return MakeUnique<TOscillatorOperatorFM<FTrianglePolysmoothWithFm>>(OpParams, FmBuffer);
				case ETriangleGenerationType::Trivial: return MakeUnique<TOscillatorOperatorFM<FTriangleWithFm>>(OpParams, FmBuffer);
				}
			}
			else
			{
				switch (*Type)
				{
				default:
				case ETriangleGenerationType::PolySmooth: return MakeUnique<TOscillatorOperator<FTrianglePolysmooth>>(OpParams);
				case ETriangleGenerationType::Trivial: return MakeUnique<TOscillatorOperator<FTriangle>>(OpParams);
				}
			}
			return nullptr;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Triangle"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_TriangleNodeDisplayName", "Triangle");
				Info.Description = LOCTEXT("Metasound_TriangleNodeDescription", "Emits a Triangle wave");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Generators);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
		static const FVertexInterface& GetVertexInterface()
		{
			auto MakeInterface = []() -> FVertexInterface
			{
				FVertexInterface Interface = GetCommmonVertexInterface();
				Interface.GetInputInterface().Add(
					TInputDataVertexModel<FEnumTriangleGenerationType>(TriangeTypePinName, LOCTEXT("TriangleTypeDescription", "The generator type to make the triangle wave"))
				);
				return Interface;
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}
	private:
	};
	
	FTriangleOscilatorNode::FTriangleOscilatorNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultFrequency, float InDefaultGlideFactor, bool bInDefaultEnablement)
		: FOscilatorNodeBase(InInstanceName, InInstanceID, FFactory::GetNodeInfo(), MakeShared<FFactory, ESPMode::ThreadSafe>(), InDefaultFrequency, InDefaultGlideFactor, bInDefaultEnablement)
	{

	}

	FTriangleOscilatorNode::FTriangleOscilatorNode(const FNodeInitData& InInitData)
		: FTriangleOscilatorNode(InInitData.InstanceName, InInitData.InstanceID, 440.0f, 0.0f, true)
	{}

	METASOUND_REGISTER_NODE(FTriangleOscilatorNode);
#pragma endregion Triangle

#pragma region LFO

	enum class ELfoWaveshapeType
	{
		Sine,
		Saw,
		Triangle,
		Square,
	};

	DECLARE_METASOUND_ENUM(ELfoWaveshapeType, ELfoWaveshapeType::Sine, METASOUNDSTANDARDNODES_API,
		FEnumLfoWaveshapeType, FEnumLfoWaveshapeTypeInfo, FEnumLfoWaveshapeTypeReadRef, FEnumLfoWaveshapeTypeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(ELfoWaveshapeType, FEnumLfoWaveshapeType, "LfoWaveshapeType")
		DEFINE_METASOUND_ENUM_ENTRY(ELfoWaveshapeType::Sine, LOCTEXT("LfoWaveShapeSineDescription", "Sine"), LOCTEXT("LfoWaveShapeSineDescriptionTT", "Sinewave Low Frequency Oscillator")),
		DEFINE_METASOUND_ENUM_ENTRY(ELfoWaveshapeType::Saw, LOCTEXT("LfoWaveShapeSawDescription", "Saw"), LOCTEXT("LfoWaveShapeSawDescriptionTT", "Sawtooth Low Frequency Oscillator")),
		DEFINE_METASOUND_ENUM_ENTRY(ELfoWaveshapeType::Triangle, LOCTEXT("LfoWaveShapeTriangleDescription", "Triangle"), LOCTEXT("LfoWaveShapeTriangleDescriptionTT", "Triangle shape Frequency Oscillator")),
		DEFINE_METASOUND_ENUM_ENTRY(ELfoWaveshapeType::Square, LOCTEXT("LfoWaveShapeSquareDescription", "Square"), LOCTEXT("LfoWaveShapeSquareDescriptionTT", "Square shape Low Frequency Oscillator"))
	DEFINE_METASOUND_ENUM_END()

	// Block rate Oscillator Swiss Army Knife
	class FLfoOperator : public TExecutableOperator<FLfoOperator>
	{
	public:
		// Common pins
		static constexpr const TCHAR* WaveshapePinName = TEXT("Shape");
		static constexpr const TCHAR* LfoOutPinName = TEXT("Out");
	
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface
			{
				FInputVertexInterface{
					TInputDataVertexModel<float>(FOscilatorFactoryBase::BaseFrequencyPinName, LOCTEXT("LfoFrequencyDescription", "Frequency of LFO (Hz), clamped at blockrate"), 5.f),
					TInputDataVertexModel<FEnumLfoWaveshapeType>(WaveshapePinName, LOCTEXT("LfoShapeDescription", "Waveshape of the LFO")),
					TInputDataVertexModel<float>(FOscilatorFactoryBase::MinOutputValuePinName, LOCTEXT("LfoMinValueDescription", "The minimum output value."), -1.0f),
					TInputDataVertexModel<float>(FOscilatorFactoryBase::MaxOutputValuePinName, LOCTEXT("LfoMaxValueDescription", "The maximum output value."), 1.0f),
					TInputDataVertexModel<FTrigger>(FOscilatorFactoryBase::PhaseResetPinName, LOCTEXT("LfoPhaseResetDescription", "Phase Reset (block rate only)")),
					TInputDataVertexModel<float>(FOscilatorFactoryBase::PhaseOffsetPinName, LOCTEXT("LfoPhaseOffsetDescription", "Phase Offset In Degrees (0..360)"), 0.f),
					TInputDataVertexModel<float>(FSquareOscilatorNode::FFactory::PulseWidthPinName, LOCTEXT("LfoPulseWidthDescription", "Pulse Width (0..1)"), 0.5f)
				},
				FOutputVertexInterface{
					TOutputDataVertexModel<float>(LfoOutPinName, LOCTEXT("LfoOutputDescription", "Output of the LFO (blockrate)"))
				}
			};
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("LFO"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_LfoNodeDisplayName", "LFO");
				Info.Description = LOCTEXT("Metasound_LfoNodeDescription", "Low frequency oscillator < blockrate");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Generators);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
		
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
		{
			const FLfoNode& Node = static_cast<const FLfoNode&>(InParams.Node);
			const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

			return MakeUnique<FLfoOperator>(
				  Settings
				, InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, FOscilatorFactoryBase::BaseFrequencyPinName, Settings)
				, InputCol.GetDataReadReferenceOrConstruct<FEnumLfoWaveshapeType>(WaveshapePinName)
				, InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, FOscilatorFactoryBase::MinOutputValuePinName, Settings)
				, InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, FOscilatorFactoryBase::MaxOutputValuePinName, Settings)
				, InputCol.GetDataReadReferenceOrConstruct<FTrigger>(FOscilatorFactoryBase::PhaseResetPinName, Settings)
				, InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface,FOscilatorFactoryBase::PhaseOffsetPinName, Settings)
				, InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface,FSquareOscilatorNode::FFactory::PulseWidthPinName, Settings)
			);
		}

		FLfoOperator(const FOperatorSettings& InSettings, FFloatReadRef&& InFrequency, FEnumLfoWaveshapeTypeReadRef&& InType, FFloatReadRef&& InMinValue, FFloatReadRef&& InMaxValue,
			FTriggerReadRef&& InPhaseReset, FFloatReadRef&& InPhaseOffset, FFloatReadRef&& InPulseWidth)
			: BlockRate{InSettings.GetActualBlockRate()}
			, Phase{0.0f}
			, Frequency{MoveTemp(InFrequency)}
			, Waveshape{MoveTemp(InType)}
			, MinValue{MoveTemp(InMinValue)}
			, MaxValue{MoveTemp(InMaxValue)}
			, PhaseReset{MoveTemp(InPhaseReset)}
			, PhaseOffset{MoveTemp(InPhaseOffset)}
			, PulseWidth{MoveTemp(InPulseWidth)}
			, Output{FFloatWriteRef::CreateNew(0.f)}
		{
			ResetPhase();
		}

		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::BaseFrequencyPinName, Frequency);
			InputDataReferences.AddDataReadReference(WaveshapePinName, Waveshape);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::MinOutputValuePinName, MinValue);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::MaxOutputValuePinName, MaxValue);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::PhaseOffsetPinName, PhaseOffset);
			InputDataReferences.AddDataReadReference(FOscilatorFactoryBase::PhaseResetPinName, PhaseReset);
			InputDataReferences.AddDataReadReference(FSquareOscilatorNode::FFactory::PulseWidthPinName, PulseWidth);
			return InputDataReferences;
		}

		FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(LfoOutPinName, Output);
			return OutputDataReferences;
		}

		void ResetPhase()
		{
			float ClampedDegrees = FMath::Clamp(*PhaseOffset, 0.f, 360.f);
			Phase = ClampedDegrees / 360.f;
		}

		void Execute()
		{
			using namespace Generators;
		
			// Prevent LFO going faster than the block rate Nyquist
			const float Nyquist = BlockRate / 2.f;
			const float ClampedFreq = FMath::Clamp(*Frequency, 0.f, Nyquist);
			const float DeltaPhase = ClampedFreq * (1.f / BlockRate);
			const float ClampPulseWidth = FMath::Clamp(*PulseWidth, 0.f, 1.f);

			FGeneratorArgs Args{ BlockRate, ClampedFreq, 0.0f, ClampPulseWidth };

			// We are not sample accurate.
			if (PhaseReset->IsTriggeredInBlock()) 
			{
				ResetPhase();
			}
			
			// Wrap phase. (0..1)
			Wrap(Phase);

			float Value = 0.f;
			switch (*Waveshape)
			{
				case ELfoWaveshapeType::Sine:
				{
					Value = SineGenerator(Phase, DeltaPhase, Args);
					break;
				}
				case ELfoWaveshapeType::Saw:
				{
					Value = SawGenerator(Phase, DeltaPhase, Args);
					break;
				}
				case ELfoWaveshapeType::Triangle:
				{
					Value = TriangleGenerator(Phase, DeltaPhase, Args);
					break;
				}
				case ELfoWaveshapeType::Square:
				{
					Value = SquareGenerator(Phase, DeltaPhase, Args);
					break;
				}
				default:
				{
					checkNoEntry();
					break;
				}
			}

			Value = FMath::GetMappedRangeValueClamped({ -1.0f, 1.0f }, { *MinValue, *MaxValue }, Value);

			*Output = Value;

			Phase += DeltaPhase;
		}

	private:
		float BlockRate = 0.f;
		float Phase = 0.f;
		Generators::FWrapPhase Wrap;
		FFloatReadRef Frequency;
		FEnumLfoWaveshapeTypeReadRef Waveshape;
		FFloatReadRef MinValue;
		FFloatReadRef MaxValue;
		FTriggerReadRef PhaseReset;
		FFloatReadRef PhaseOffset;
		FFloatReadRef PulseWidth;
		FFloatWriteRef Output;

		Generators::FSawPolySmoothGenerator SawGenerator;
		Generators::FSineWaveTableGenerator SineGenerator;
		Generators::FTriangleGenerator TriangleGenerator;
		Generators::FSquarePolysmoothGenerator SquareGenerator;

	};

	FLfoNode::FLfoNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FLfoOperator>())
	{}

	METASOUND_REGISTER_NODE(FLfoNode);

#pragma endregion LFO
}
#undef LOCTEXT_NAMESPACE //MetasoundStandardNodes

