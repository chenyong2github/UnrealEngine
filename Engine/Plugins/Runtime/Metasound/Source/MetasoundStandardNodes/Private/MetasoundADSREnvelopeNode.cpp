// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "Internationalization/Text.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/Dsp.h"
#include "MetasoundParamHelper.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_ADSR"

namespace Metasound
{
	namespace ADSREnvelopeVertexNames
	{
		METASOUND_PARAM(InputAttackTrigger, "Trigger Attack", "Trigger to start the attack phase of the envelope generator.");
		METASOUND_PARAM(InputReleaseTrigger, "Trigger Release", "Trigger to start the release phase of the envelope generator.");

		METASOUND_PARAM(InputAttackTime, "Attack Time", "Attack time of the envelope.");
		METASOUND_PARAM(InputDecayTime, "Decay Time", "Decay time of the envelope.");
		METASOUND_PARAM(InputSustainLevel, "Sustain Level", "The sustain level.");
		METASOUND_PARAM(InputReleaseTime, "Release Time", "Decay time of the envelope.");

		METASOUND_PARAM(InputAttackCurve, "Attack Curve", "The exponential curve factor of the attack. 1.0 = linear growth, < 1.0 logorithmic growth, > 1.0 exponential growth.");
		METASOUND_PARAM(InputDecayCurve, "Decay Curve", "The exponential curve factor of the decay. 1.0 = linear decay, < 1.0 exponential decay, > 1.0 logorithmic decay.");
		METASOUND_PARAM(InputReleaseCurve, "Release Curve", "The exponential curve factor of the release. 1.0 = linear release, < 1.0 exponential release, > 1.0 logorithmic release.");

		METASOUND_PARAM(OutputOnAttackTrigger, "On Attack Triggered", "Triggers when the envelope attack is triggered.");
		METASOUND_PARAM(OutputOnReleaseTrigger, "On Release Triggered", "Triggers when the envelope attack is triggered.");
		METASOUND_PARAM(OutputOnDone, "On Done", "Triggers when the envelope finishes.");
		METASOUND_PARAM(OutputEnvelopeValue, "Out Envelope", "The output value of the envelope.");
	}

	namespace ADSREnvelopeNodePrivate
	{
// 		enum class EEnvState : uint8
// 		{
// 			Attack,
// 			Decay,
// 			Sustain,
// 			Release
// 		};
// 
// 		class FEnvSegment
// 		{		
// 		public:
// 			FEnvSegment(EEnvState InState, int32 InSampleCount, float InCurveValue, float InTargetValue)
// 				: State(InState)
// 				, SampleCount(InSampleCount)
// 				, CurveValue(InCurveValue)
// 				, TargetValue(InTargetValue)
// 			{}
// 
// 			void Update(float InSampleCount, float InTargetValue)
// 			{
// 				SampleCount = InSampleCount;
// 				TargetValue = InTargetValue;
// 			}
// 
// 			void Start()
// 			{
// 				CurrentSampleCount = 0;
// 			}
// 
// 			// return true if done in this state
// 			bool GetNextValue(float& OutValue)
// 			{
// 				if ()
// 			}
// 
// 			EEnvState State;
// 			int32 SampleCount = 0;
// 			float TargetValue = 0.0f;
// 			int32 CurrentSampleCount = 0;
// 
// 		};

		struct FEnvState
		{
			// Where the envelope is. If INDEX_NONE, then the envelope is not triggered
			int32 CurrentSampleIndex = INDEX_NONE;

			// Number of samples for attack
			int32 AttackSampleCount = 0;

			// Number of samples for Decay
			int32 DecaySampleCount = 0;

			// Number of samples for Decay
			int32 ReleaseSampleCount = 0;
			
			// Sustain leve
			float SustainLevel = 0.0f;

			// Curve factors for attack/decay/release
			float AttackCurveFactor = 0.0f;
			float DecayCurveFactor = 0.0f;
			float ReleaseCurveFactor = 0.0f;

			Audio::FExponentialEase EnvEase;

			// Where the envelope value was when it was triggered
			float StartingEnvelopeValue = 0.0f;
			float CurrentEnvelopeValue = 0.0f;
			float EnvelopeValueAtReleaseStart = 0.0f;

			// If this is set, we are in release mode
			bool bIsInRelease = false;
		};

		template<typename ValueType>
		struct TADSREnvelope
		{
		};

		template<>
		struct TADSREnvelope<float>
		{
			static void GetNextEnvelopeOutput(FEnvState& InState, int32 StartFrame, int32 EndFrame, TArray<int32>& OutFinishedFrames, float& OutEnvelopeValue)
			{
				// Don't need to do anything if we're not generating the envelope at the top of the block since this is a block-rate envelope
				if (StartFrame > 0 || InState.CurrentSampleIndex == INDEX_NONE)
				{
					OutEnvelopeValue = 0.0f;
					return;
				}

				// Sample count to the end of the decay phase
				int32 DecayEnvSampleCount = (InState.AttackSampleCount + InState.DecaySampleCount);

				// If we are in the release state, jump forward in our sample count
				if (InState.bIsInRelease)
				{
					int32 SampleStartOfRelease = InState.AttackSampleCount + InState.DecaySampleCount;
					if (InState.CurrentSampleIndex < SampleStartOfRelease)
					{
						InState.EnvelopeValueAtReleaseStart = InState.CurrentEnvelopeValue;
						InState.CurrentSampleIndex = InState.AttackSampleCount + InState.DecaySampleCount;
					}
				}

				// We are in attack
				if (InState.CurrentSampleIndex < InState.AttackSampleCount)
				{
					float AttackFraction = (float)InState.CurrentSampleIndex++ / InState.AttackSampleCount;
					float EnvValue = FMath::Pow(AttackFraction, InState.AttackCurveFactor);
					float TargeEnvelopeValue = InState.StartingEnvelopeValue + (1.0f - InState.StartingEnvelopeValue) * EnvValue;
					InState.EnvEase.SetValue(TargeEnvelopeValue);
					InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
					OutEnvelopeValue = InState.CurrentEnvelopeValue;
				}
				// We are in decay
				else if (InState.CurrentSampleIndex < DecayEnvSampleCount)
				{
					int32 SampleCountInDecayState = InState.CurrentSampleIndex++ - InState.AttackSampleCount;
					float DecayFracton = (float)SampleCountInDecayState / InState.DecaySampleCount;
					float TargetEnvelopeValue = 1.0f - (1.0f - InState.SustainLevel) * FMath::Pow(DecayFracton, InState.DecayCurveFactor);
					InState.EnvEase.SetValue(TargetEnvelopeValue);
					InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
					OutEnvelopeValue = InState.CurrentEnvelopeValue;
				}
				// We are in sustain
				else if (!InState.bIsInRelease)
				{
					InState.EnvEase.SetValue(InState.SustainLevel);
					InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
					OutEnvelopeValue = InState.CurrentEnvelopeValue;
					InState.EnvelopeValueAtReleaseStart = OutEnvelopeValue;
				}
				// We are in release mode or finished
				else 			
				{
					int32 ReleaseEnvSampleCount = (InState.AttackSampleCount + InState.DecaySampleCount + InState.ReleaseSampleCount);
					// We are in release
					if (InState.CurrentSampleIndex < ReleaseEnvSampleCount)
					{
						int32 SampleCountInReleaseState = InState.CurrentSampleIndex++ - InState.DecaySampleCount - InState.AttackSampleCount;
						float ReleaseFraction = (float)SampleCountInReleaseState / InState.ReleaseSampleCount;
						float TargetEnvelopeValue = InState.EnvelopeValueAtReleaseStart * (1.0f - FMath::Pow(ReleaseFraction, InState.ReleaseCurveFactor));
						InState.EnvEase.SetValue(TargetEnvelopeValue);
						InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
						OutEnvelopeValue = InState.CurrentEnvelopeValue;
					}
					// We are done
					else
					{
						InState.CurrentSampleIndex = INDEX_NONE;
						OutEnvelopeValue = 0.0f;
						OutFinishedFrames.Add(0);
					}
				}
			}

			static bool IsAudio() { return false; }
		};

		template<>
		struct TADSREnvelope<FAudioBuffer>
		{
			static void GetNextEnvelopeOutput(FEnvState& InState, int32 StartFrame, int32 EndFrame, TArray<int32>& OutFinishedFrames, FAudioBuffer& OutEnvelopeValue)
			{
				// If we are not active zero the buffer and early exit
				if (InState.CurrentSampleIndex == INDEX_NONE)
				{
					OutEnvelopeValue.Zero();
					return;
				}

				// If we are in the release state, jump forward in our sample count
				if (InState.bIsInRelease)
				{
					int32 SampleStartOfRelease = InState.AttackSampleCount + InState.DecaySampleCount;
					if (InState.CurrentSampleIndex <= SampleStartOfRelease)
					{
						InState.EnvelopeValueAtReleaseStart = InState.CurrentEnvelopeValue;
						InState.CurrentSampleIndex = SampleStartOfRelease;
					}
				}

				float* OutEnvPtr = OutEnvelopeValue.GetData();
				for (int32 i = StartFrame; i < EndFrame; ++i)
				{
					// We are in attack
					if (InState.CurrentSampleIndex <= InState.AttackSampleCount)
					{
						float AttackFraction = (float)InState.CurrentSampleIndex++ / InState.AttackSampleCount;
						float EnvValue = FMath::Pow(AttackFraction, InState.AttackCurveFactor);
						float TargeEnvelopeValue = InState.StartingEnvelopeValue + (1.0f - InState.StartingEnvelopeValue) * EnvValue;
						InState.EnvEase.SetValue(TargeEnvelopeValue);
						InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
						OutEnvPtr[i] = InState.CurrentEnvelopeValue;
					}
					else
					{
						int32 DecayEnvSampleCount = (InState.AttackSampleCount + InState.DecaySampleCount);
						// We are in decay
						if (InState.CurrentSampleIndex < DecayEnvSampleCount)
						{
							int32 SampleCountInDecayState = InState.CurrentSampleIndex++ - InState.AttackSampleCount;
							float DecayFracton = (float)SampleCountInDecayState / InState.DecaySampleCount;
							float TargetEnvelopeValue = 1.0f - (1.0f - InState.SustainLevel) * FMath::Pow(DecayFracton, InState.DecayCurveFactor);
							InState.EnvEase.SetValue(TargetEnvelopeValue);
							InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
							OutEnvPtr[i] = InState.CurrentEnvelopeValue;
						}
						// We are in sustain
						else if (!InState.bIsInRelease)
						{
							InState.EnvEase.SetValue(InState.SustainLevel);
							InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
							InState.EnvelopeValueAtReleaseStart = InState.CurrentEnvelopeValue;
							OutEnvPtr[i] = InState.CurrentEnvelopeValue;
						}
						else 
						{
							int32 ReleaseEnvSampleCount = (InState.AttackSampleCount + InState.DecaySampleCount + InState.ReleaseSampleCount);

							// We are in release
							if (InState.CurrentSampleIndex < ReleaseEnvSampleCount)
							{
								int32 SampleCountInReleaseState = InState.CurrentSampleIndex++ - InState.DecaySampleCount - InState.AttackSampleCount;
								float ReleaseFraction = (float)SampleCountInReleaseState / InState.ReleaseSampleCount;
								float TargetEnvelopeValue = InState.EnvelopeValueAtReleaseStart * (1.0 - FMath::Pow(ReleaseFraction, InState.ReleaseCurveFactor));
								InState.EnvEase.SetValue(TargetEnvelopeValue);
								InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
								OutEnvPtr[i] = InState.CurrentEnvelopeValue;
							}
							// We're done
							else
							{
								InState.CurrentEnvelopeValue = InState.EnvEase.GetNextValue();
								OutEnvPtr[i] = InState.CurrentEnvelopeValue;

								// Envelope is done
								if (InState.EnvEase.IsDone())
								{
									// Zero out the rest of the envelope
									int32 NumSamplesLeft = EndFrame - i - 1;
									if (NumSamplesLeft > 0)
									{
										FMemory::Memzero(&OutEnvPtr[i + 1], sizeof(float) * NumSamplesLeft);
									}
									InState.CurrentSampleIndex = INDEX_NONE;
									OutFinishedFrames.Add(i);
									break;
								}
							}
						}
					}
				}
			}

			static bool IsAudio() { return true; }
		};

	}

	template<typename ValueType>
	class TADSREnvelopeNodeOperator : public TExecutableOperator<TADSREnvelopeNodeOperator<ValueType>>
	{
	public:
		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ADSREnvelopeVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(METASOUND_GET_PARAM_NAME_AND_TT(InputAttackTrigger)),
					TInputDataVertexModel<FTrigger>(METASOUND_GET_PARAM_NAME_AND_TT(InputReleaseTrigger)),
					TInputDataVertexModel<FTime>(METASOUND_GET_PARAM_NAME_AND_TT(InputAttackTime), 0.01f),
					TInputDataVertexModel<FTime>(METASOUND_GET_PARAM_NAME_AND_TT(InputDecayTime), 0.2f),
					TInputDataVertexModel<float>(METASOUND_GET_PARAM_NAME_AND_TT(InputSustainLevel), 0.5f),
					TInputDataVertexModel<FTime>(METASOUND_GET_PARAM_NAME_AND_TT(InputReleaseTime), 1.0f),
					TInputDataVertexModel<float>(METASOUND_GET_PARAM_NAME_AND_TT(InputAttackCurve), 1.0f),
					TInputDataVertexModel<float>(METASOUND_GET_PARAM_NAME_AND_TT(InputDecayCurve), 1.0f),
					TInputDataVertexModel<float>(METASOUND_GET_PARAM_NAME_AND_TT(InputReleaseCurve), 1.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTrigger>(METASOUND_GET_PARAM_NAME_AND_TT(OutputOnAttackTrigger)),
					TOutputDataVertexModel<FTrigger>(METASOUND_GET_PARAM_NAME_AND_TT(OutputOnReleaseTrigger)),
					TOutputDataVertexModel<FTrigger>(METASOUND_GET_PARAM_NAME_AND_TT(OutputOnDone)),
					TOutputDataVertexModel<ValueType>(METASOUND_GET_PARAM_NAME_AND_TT(OutputEnvelopeValue))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ValueType>();
				FName OperatorName = TEXT("ADSR Envelope");
				FText NodeDisplayName = FText::Format(LOCTEXT("ADSREnvelopeDisplayNamePattern", "ADSR Envelope ({0})"), FText::FromString(GetMetasoundDataTypeString<ValueType>()));
				FText NodeDescription = LOCTEXT("ADSREnevelopeDesc", "Generates an attack-decay-sustain-release envelope value output when triggered.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				FNodeClassMetadata Metadata
				{
					FNodeClassName{FName("ADSR Envelope"), OperatorName, DataTypeName},
					1, // Major Version
					0, // Minor Version
					NodeDisplayName,
					NodeDescription,
					PluginAuthor,
					PluginNodeMissingPrompt,
					NodeInterface,
					{LOCTEXT("EnvelopeCat", "Envelopes")},
					{TEXT("Envelope")},
					FNodeDisplayStyle{}
				};

				return Metadata;
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ADSREnvelopeVertexNames;

			const FInputVertexInterface& InputInterface = GetDefaultInterface().GetInputInterface();

			FTriggerReadRef TriggerAttackIn = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(METASOUND_GET_PARAM_NAME(InputAttackTrigger), InParams.OperatorSettings);
			FTriggerReadRef TriggerReleaseIn = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(METASOUND_GET_PARAM_NAME(InputReleaseTrigger), InParams.OperatorSettings);

			FTimeReadRef AttackTime = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, METASOUND_GET_PARAM_NAME(InputAttackTime), InParams.OperatorSettings);
			FTimeReadRef DecayTime = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, METASOUND_GET_PARAM_NAME(InputDecayTime), InParams.OperatorSettings);
			FFloatReadRef SustainLevel = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InputSustainLevel), InParams.OperatorSettings);
			FTimeReadRef ReleaseTime = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, METASOUND_GET_PARAM_NAME(InputReleaseTime), InParams.OperatorSettings);

			FFloatReadRef AttackCurveFactor = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InputAttackCurve), InParams.OperatorSettings);
			FFloatReadRef DecayCurveFactor = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InputDecayCurve), InParams.OperatorSettings);
			FFloatReadRef ReleaseCurveFactor = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InputReleaseCurve), InParams.OperatorSettings);

			return MakeUnique<TADSREnvelopeNodeOperator<ValueType>>(InParams.OperatorSettings, TriggerAttackIn, TriggerReleaseIn, AttackTime, DecayTime, SustainLevel, ReleaseTime, AttackCurveFactor, DecayCurveFactor, ReleaseCurveFactor);
		}

		TADSREnvelopeNodeOperator(const FOperatorSettings& InSettings,
			const FTriggerReadRef& InTriggerAttackIn,
			const FTriggerReadRef& InTriggerReleaseIn,
			const FTimeReadRef& InAttackTime,
			const FTimeReadRef& InDecayTime,
			const FFloatReadRef& InSustainLevel,
			const FTimeReadRef& InReleaseTime,
			const FFloatReadRef& InAttackCurveFactor,
			const FFloatReadRef& InDecayCurveFactor,
			const FFloatReadRef& InReleaseCurveFactor)
			: TriggerAttackIn(InTriggerAttackIn)
			, TriggerReleaseIn(InTriggerReleaseIn)
			, AttackTime(InAttackTime)
			, DecayTime(InDecayTime)
			, SustainLevel(InSustainLevel)
			, ReleaseTime(InReleaseTime)
			, AttackCurveFactor(InAttackCurveFactor)
			, DecayCurveFactor(InDecayCurveFactor)
			, ReleaseCurveFactor(InReleaseCurveFactor)
			, OnAttackTrigger(TDataWriteReferenceFactory<FTrigger>::CreateAny(InSettings))
			, OnReleaseTrigger(TDataWriteReferenceFactory<FTrigger>::CreateAny(InSettings))
			, OnDone(TDataWriteReferenceFactory<FTrigger>::CreateAny(InSettings))
			, OutputEnvelope(TDataWriteReferenceFactory<ValueType>::CreateAny(InSettings))
		{
			NumFramesPerBlock = InSettings.GetNumFramesPerBlock();

			EnvState.EnvEase.SetEaseFactor(0.01f);

			if (ADSREnvelopeNodePrivate::TADSREnvelope<ValueType>::IsAudio())
			{
				SampleRate = InSettings.GetSampleRate();
			}
			else
			{
				SampleRate = InSettings.GetActualBlockRate();
			}
		}

		virtual ~TADSREnvelopeNodeOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ADSREnvelopeVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputAttackTrigger), TriggerAttackIn);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputAttackTrigger), TriggerReleaseIn);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputAttackTime), AttackTime);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputDecayTime), DecayTime);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputSustainLevel), SustainLevel);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputReleaseTime), ReleaseTime);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputAttackCurve), AttackCurveFactor);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputDecayCurve), DecayCurveFactor);
			Inputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(InputReleaseCurve), ReleaseCurveFactor);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ADSREnvelopeVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputOnAttackTrigger), OnAttackTrigger);
			Outputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputOnReleaseTrigger), OnReleaseTrigger);
			Outputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputOnDone), OnDone);
			Outputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputEnvelopeValue), OutputEnvelope);

			return Outputs;
		}

		void UpdateParams()
		{
			float AttackTimeSeconds = AttackTime->GetSeconds();
			float DecayTimeSeconds = DecayTime->GetSeconds();
			float ReleaseTimeSeconds = ReleaseTime->GetSeconds();
			EnvState.AttackSampleCount = SampleRate * FMath::Max(0.0f, AttackTimeSeconds);
			EnvState.DecaySampleCount = SampleRate * FMath::Max(0.0f, DecayTimeSeconds);
			EnvState.SustainLevel = FMath::Max(0.0f, *SustainLevel);
			EnvState.ReleaseSampleCount = SampleRate * FMath::Max(0.0f, ReleaseTimeSeconds);
			EnvState.AttackCurveFactor = FMath::Max(KINDA_SMALL_NUMBER, *AttackCurveFactor);
			EnvState.DecayCurveFactor = FMath::Max(KINDA_SMALL_NUMBER, *DecayCurveFactor);
			EnvState.ReleaseCurveFactor = FMath::Max(KINDA_SMALL_NUMBER, *ReleaseCurveFactor);
		}


		void Execute()
		{
			using namespace ADSREnvelopeNodePrivate;

			OnAttackTrigger->AdvanceBlock();
			OnReleaseTrigger->AdvanceBlock();
			OnDone->AdvanceBlock();
 
			// check for any updates to input params
			UpdateParams();
 		
			TriggerReleaseIn->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				// OnTrigger
				[&](int32 StartFrame, int32 EndFrame)
				{
					// This schedules a release sample count
					EnvState.bIsInRelease = true;
					OnReleaseTrigger->TriggerFrame(StartFrame);
				}
			);

			TriggerAttackIn->ExecuteBlock(
				// OnPreTrigger
				[&](int32 StartFrame, int32 EndFrame)
				{
					TArray<int32> FinishedFrames;
					TADSREnvelope<ValueType>::GetNextEnvelopeOutput(EnvState, StartFrame, EndFrame, FinishedFrames, *OutputEnvelope);

					for (int32 FrameFinished : FinishedFrames)
					{
						OnDone->TriggerFrame(FrameFinished);
					}
				},
				// OnTrigger
				[&](int32 StartFrame, int32 EndFrame)
				{
					// Get latest params
					UpdateParams();

					// Set the sample index to the top of the envelope
					EnvState.CurrentSampleIndex = 0;
					EnvState.StartingEnvelopeValue = EnvState.CurrentEnvelopeValue;
					EnvState.bIsInRelease = false;
					// Generate the output (this will no-op if we're block rate)
					TArray<int32> FinishedFrames;
					TADSREnvelope<ValueType>::GetNextEnvelopeOutput(EnvState, StartFrame, EndFrame, FinishedFrames, *OutputEnvelope);
					for (int32 FrameFinished : FinishedFrames)
					{
						OnDone->TriggerFrame(FrameFinished);
					}

					// Forward the trigger
					OnAttackTrigger->TriggerFrame(StartFrame);
				}
			);
		}

	private:
		FTriggerReadRef TriggerAttackIn;
		FTriggerReadRef TriggerReleaseIn;
		FTimeReadRef AttackTime;
		FTimeReadRef DecayTime;
		FFloatReadRef SustainLevel;
		FTimeReadRef ReleaseTime;
		FFloatReadRef AttackCurveFactor;
		FFloatReadRef DecayCurveFactor;
		FFloatReadRef ReleaseCurveFactor;

		FTriggerWriteRef OnAttackTrigger;
		FTriggerWriteRef OnReleaseTrigger;
		FTriggerWriteRef OnDone;
		TDataWriteReference<ValueType> OutputEnvelope;

		// This will either be the block rate or sample rate depending on if this is block-rate or audio-rate envelope
		float SampleRate = 0.0f;
		int32 NumFramesPerBlock = 0;

		ADSREnvelopeNodePrivate::FEnvState EnvState;
	};

	/** TADSREnvelopeNode
	 *
	 *  Creates an Attack/Decay envelope node.
	 */
	template<typename ValueType>
	class METASOUNDSTANDARDNODES_API TADSREnvelopeNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TADSREnvelopeNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TADSREnvelopeNodeOperator<ValueType>>())
		{}

		virtual ~TADSREnvelopeNode() = default;
	};

	using FADSREnvelopeNodeFloat = TADSREnvelopeNode<float>;
	METASOUND_REGISTER_NODE(FADSREnvelopeNodeFloat)

	using FADSREnvelopeAudioBuffer = TADSREnvelopeNode<FAudioBuffer>;
	METASOUND_REGISTER_NODE(FADSREnvelopeAudioBuffer)
}

#undef LOCTEXT_NAMESPACE
