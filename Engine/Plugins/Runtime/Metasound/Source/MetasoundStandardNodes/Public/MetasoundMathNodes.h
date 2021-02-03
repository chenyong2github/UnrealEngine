// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundFrequency.h"
#include "MetasoundNode.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"


#define LOCTEXT_NAMESPACE "MetasoundMathOpNode"

#define DEFINE_METASOUND_MATHOP(OpName, DataTypeName, DataClass, DisplayName, Description) \
	class F##OpName##DataTypeName##Node \
		: public ::Metasound::TMathOpNode<F##OpName##DataTypeName##Node, ::Metasound::TMathOp##OpName<DataClass>, DataClass> \
	{ \
	public: \
		static FNodeClassName GetClassName() { return {::Metasound::StandardNodes::Namespace, TEXT(#OpName), TEXT(#DataTypeName)}; } \
		static FText GetDisplayName() { return DisplayName; } \
		static FText GetDescription() { return Description; } \
		static FName GetImageName() { return "MetasoundEditor.Graph.Node.Math." #OpName; } \
		F##OpName##DataTypeName##Node(const FNodeInitData& InInitData) : TMathOpNode(InInitData) { } \
	};

namespace Metasound
{
	namespace MathOpDisplayNames
	{
		template<typename DataType>
		const FText GetAddDisplayName()
		{
			static const FText DisplayName = FText::Format(LOCTEXT("AddNodeDisplayNamePattern", "Add {0}"), FText::FromString(GetMetasoundDataTypeString<DataType>()));
			return DisplayName;
		}

		template<typename DataType>
		const FText GetSubtractDisplayName()
		{
			static const FText DisplayName = FText::Format(LOCTEXT("SubtractNodeDisplayNamePattern", "Subtract {0}"), FText::FromString(GetMetasoundDataTypeString<DataType>()));
			return DisplayName;
		}

		template<typename DataType>
		const FText GetMultiplyDisplayName()
		{
			static const FText DisplayName = FText::Format(LOCTEXT("MultiplyNodeDisplayNamePattern", "Multiply {0}"), FText::FromString(GetMetasoundDataTypeString<DataType>()));
			return DisplayName;
		}

		template<typename DataType>
		const FText GetDivideDisplayName()
		{
			static const FText DisplayName = FText::Format(LOCTEXT("DivideNodeDisplayNamePattern", "Divide {0}"), FText::FromString(GetMetasoundDataTypeString<DataType>()));
			return DisplayName;
		}

		template<typename DataType>
		const FText GetRandRangeDisplayName()
		{
			static const FText DisplayName = FText::Format(LOCTEXT("RandRangeNodeDisplayNamePattern", "RandRange {0}"), FText::FromString(GetMetasoundDataTypeString<DataType>()));
			return DisplayName;
		}
	}

	template <typename TDerivedClass, typename TMathOpClass, typename TDataClass>
	class TMathOpNode : public FNodeFacade
	{
	private:
		class TMathOperator : public TExecutableOperator<TMathOperator>
		{
		private:
			using TDataClassReadRef = TDataReadReference<TDataClass>;
			using TDataClassWriteRef = TDataWriteReference<TDataClass>;

			TArray<TDataClassReadRef> OperandRefs;
			TDataClassWriteRef ValueRef;

		public:
			static FVertexInterface DeclareVertexInterface()
			{
				static const FVertexInterface Interface(
					FInputVertexInterface(
						TInputDataVertexModel<TDataClass>(TEXT("Operand1"), LOCTEXT("MathOpMinTooltip", "First value to operate on.")),
						TInputDataVertexModel<TDataClass>(TEXT("Operand2"), LOCTEXT("MathOpMaxTooltip", "Second value to operate on."))
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<TDataClass>(TEXT("Out"), LOCTEXT("MathOpOutTooltip", "Math operation result"))
					)
				);

				return Interface;
			}

			static const FNodeClassMetadata& GetNodeInfo()
			{
				auto InitNodeInfo = []() -> FNodeClassMetadata
				{
					FNodeDisplayStyle DisplayStyle;
					DisplayStyle.ImageName = TDerivedClass::GetImageName();
					DisplayStyle.bShowName = false;
					DisplayStyle.bShowInputNames = false;
					DisplayStyle.bShowOutputNames = false;

					FNodeClassMetadata Info;
					Info.ClassName = TDerivedClass::GetClassName();
					Info.MajorVersion = 1;
					Info.MinorVersion = 0;
					Info.DisplayName = TDerivedClass::GetDisplayName();
					Info.Description = TDerivedClass::GetDescription();
					Info.Author = PluginAuthor;
					Info.PromptIfMissing = PluginNodeMissingPrompt;
					Info.DefaultInterface = DeclareVertexInterface();
					Info.DisplayStyle = DisplayStyle;
					Info.CategoryHierarchy = { LOCTEXT("Metasound_MathCategory", "Math") };

					return Info;
				};

				static const FNodeClassMetadata Info = InitNodeInfo();
				return Info;
			}

			void Execute()
			{
				TMathOpClass::Calculate(OperandRefs, ValueRef);
			}

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
			{
				const TMathOpNode& MathOpNode = static_cast<const TMathOpNode&>(InParams.Node);

				// TODO: Support dynamic number of inputs
				TDataClassReadRef Op1 = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TDataClass>(TEXT("Operand1"), TMathOpClass::GetDefault(&InParams.OperatorSettings));
				TDataClassReadRef Op2 = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TDataClass>(TEXT("Operand2"), TMathOpClass::GetDefault(&InParams.OperatorSettings));

				TArray<TDataClassReadRef> OperandRefs = { Op1, Op2 };

				return MakeUnique<TMathOperator>(InParams.OperatorSettings, OperandRefs);
			}

			TMathOperator(const FOperatorSettings& InSettings, const TArray<TDataClassReadRef>& InOperands)
			:	OperandRefs(InOperands)
			,	ValueRef(TDataClassWriteRef::CreateNew(TMathOpClass::GetDefault(&InSettings)))
			{
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection InputDataReferences;
				InputDataReferences.AddDataReadReference(TEXT("Operand1"), OperandRefs[0]);
				InputDataReferences.AddDataReadReference(TEXT("Operand2"), OperandRefs[1]);
				return InputDataReferences;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection OutputDataReferences;
				OutputDataReferences.AddDataReadReference(TEXT("Out"), TDataClassReadRef(ValueRef));

				return OutputDataReferences;
			}
		};

	public:
		TMathOpNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TMathOperator>())
		{
		}
	};

// Standard Math Operations
	template <typename TDataClass>
	class TMathOpAdd
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings* InSettings)
		{
			return 0;
		}

		static void Calculate(const TArray<TDataClassReadRef>& InOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				*OutResult += *InOperands[i];
			}
		}
	};

	template <typename TDataClass>
	class TMathOpSubtract
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings* InSettings)
		{
			return 0;
		}

		static void Calculate(const TArray<TDataClassReadRef>& InOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				*OutResult -= *InOperands[i];
			}
		}
	};

	template <typename TDataClass>
	class TMathOpMultiply
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings* InSettings)
		{
			return 1;
		}

		static void Calculate(const TArray<TDataClassReadRef>& InOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				*OutResult *= *InOperands[i];
			}
		}
	};

	template <typename TDataClass>
	class TMathOpDivide
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings* InSettings)
		{
			return 1;
		}

		static void Calculate(const TArray<TDataClassReadRef>& InOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				const TDataClass& OperandValue = *InOperands[i];
				if (OperandValue == 0)
				{
					// TODO: Error here
					return;
				}

				*OutResult /= OperandValue;
			}
		}
	};

// Specialized Math Operations
	template <>
	class TMathOpAdd<FAudioBuffer>
	{
	public:
		static FAudioBuffer GetDefault(const FOperatorSettings* InSettings)
		{
			return FAudioBuffer(InSettings ? InSettings->GetNumFramesPerBlock() : 0);
		}

		static void Calculate(const TArray<FAudioBufferReadRef>& InOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InOperands[0]->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InOperands[0]->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_SIMD_FLOAT_ALIGNMENT)
				{
					for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
					{
						OutData[SampleIndex] += OperandData[SampleIndex];
					}
				}
				else
				{
					const float StartGain = 1.0f;
					const float EndGain = 1.0f;
					Audio::MixInBufferFast(OperandData, OutData, NumSamples, StartGain, EndGain);
				}
			}
		}
	};

	template <>
	class TMathOpSubtract<FAudioBuffer>
	{
	public:
		static FAudioBuffer GetDefault(const FOperatorSettings* InSettings)
		{
			return FAudioBuffer(InSettings ? InSettings->GetNumFramesPerBlock() : 0);
		}

		static void Calculate(TArray<FAudioBufferReadRef>& InOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InOperands[0]->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InOperands[0]->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_SIMD_FLOAT_ALIGNMENT)
				{
					for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
					{
						OutData[i] -= OperandData[i];
					}
				}
				else
				{
					Audio::BufferSubtractInPlace2Fast(OutData, OperandData, NumSamples);
				}
			}
		}
	};

	template <>
	class TMathOpMultiply<FAudioBuffer>
	{
	public:
		static FAudioBuffer GetDefault(const FOperatorSettings* InSettings)
		{
			return FAudioBuffer(InSettings ? InSettings->GetNumFramesPerBlock() : 0);
		}

		static void Calculate(const TArray<FAudioBufferReadRef>& InOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InOperands[0]->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InOperands[0]->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_SIMD_FLOAT_ALIGNMENT)
				{
					for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
					{
						OutData[SampleIndex] += OperandData[SampleIndex];
					}
				}
				else
				{
					Audio::MultiplyBuffersInPlace(OperandData, OutData, NumSamples);
				}
			}
		}
	};

	template <>
	class TMathOpMultiply<FFloatTime>
	{
	public:
		static FFloatTime GetDefault(const FOperatorSettings* InSettings)
		{
			return FFloatTime(1.0f);
		}

		static void Calculate(const TArray<FFloatTimeReadRef>& InOperands, FFloatTimeWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				OutResult->SetSeconds(OutResult->GetSeconds() * InOperands[i]->GetSeconds());
			}
		}
	};

	template <>
	class TMathOpDivide<FFloatTime>
	{
	public:
		static FFloatTime GetDefault(const FOperatorSettings* InSettings)
		{
			return FFloatTime(1.0f);
		}

		static void Calculate(const TArray<FFloatTimeReadRef>& InOperands, FFloatTimeWriteRef& OutResult)
		{
			if (!ensure(!InOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InOperands[0];
			for (int32 i = 1; i < InOperands.Num(); ++i)
			{
				const FFloatTime& OperandValue = *InOperands[i];
				if (OperandValue.GetSeconds() == 0.0f)
				{
					// TODO: Error here
					return;
				}

				OutResult->SetSeconds(OutResult->GetSeconds() / OperandValue.GetSeconds());
			}
		}
	};

	template <typename TDataClass>
	class TMathOpRandRange
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings* InSettings)
		{
			return 0;
		}

		static void Calculate(const TArray<TDataClassReadRef>& InOperands, TDataClassWriteRef& OutResult)
		{
			if (InOperands.Num() > 1)
			{
				const TDataClass& Min = *InOperands[0];
				const TDataClass& Max = *InOperands[1];
				*OutResult = FMath::RandRange(Min, Max);
			}
			else
			{
				// TODO: Error
			}
		}
	};

	template <>
	class TMathOpRandRange<FFloatTime>
	{
	public:
		static FFloatTime GetDefault(const FOperatorSettings* InSettings)
		{
			return FFloatTime(0.0f);
		}

		static void Calculate(const TArray<FFloatTimeReadRef>& InOperands, FFloatTimeWriteRef& OutResult)
		{
			if (InOperands.Num() > 1)
			{
				const FFloatTime& Min = *InOperands[0];
				const FFloatTime& Max = *InOperands[1];
				const float FloatResult = FMath::RandRange(Min.GetSeconds(), Max.GetSeconds());
				OutResult->SetSeconds(FloatResult);
			}
			else
			{
				// TODO: Error
			}
		}
	};

	template <>
	class TMathOpRandRange<FFrequency>
	{
	public:
		static FFrequency GetDefault(const FOperatorSettings* InSettings)
		{
			return FFrequency(0.0f);
		}

		static void Calculate(const TArray<FFrequencyReadRef>& InOperands, FFrequencyWriteRef& OutResult)
		{
			if (InOperands.Num() > 1)
			{
				const FFrequency& Min = *InOperands[0];
				const FFrequency& Max = *InOperands[1];
				const float FloatResult = FMath::RandRange(Min.GetHertz(), Max.GetHertz());
				OutResult->SetHertz(FloatResult);
			}
			else
			{
				// TODO: Error
			}
		}
	};

	// Definitions
	DEFINE_METASOUND_MATHOP(Add, Float, float, MathOpDisplayNames::GetAddDisplayName<float>(), LOCTEXT("Metasound_MathAddFloatNodeDescription", "Adds floats."))
	DEFINE_METASOUND_MATHOP(Add, Int32, int32, MathOpDisplayNames::GetAddDisplayName<int32>(), LOCTEXT("Metasound_MathAddInt32NodeDescription", "Adds int32s."))
	DEFINE_METASOUND_MATHOP(Add, Int64, int64, MathOpDisplayNames::GetAddDisplayName<int64>(), LOCTEXT("Metasound_MathAddInt64NodeDescription", "Adds int64s."))
	DEFINE_METASOUND_MATHOP(Add, Frequency, FFrequency, MathOpDisplayNames::GetAddDisplayName<FFrequency>(), LOCTEXT("Metasound_MathAddFrequencyNodeDescription", "Adds frequency values."))
	DEFINE_METASOUND_MATHOP(Add, FloatTime, FFloatTime, MathOpDisplayNames::GetAddDisplayName<FFloatTime>(), LOCTEXT("Metasound_MathAddFloatTimeNodeDescription", "Adds time values."))
	DEFINE_METASOUND_MATHOP(Add, AudioBuffer, FAudioBuffer, MathOpDisplayNames::GetAddDisplayName<FAudioBuffer>(), LOCTEXT("Metasound_MathAddBufferNodeDescription", "Adds buffers together by sample."))

	DEFINE_METASOUND_MATHOP(Subtract, Float, float, MathOpDisplayNames::GetSubtractDisplayName<float>(), LOCTEXT("Metasound_MathSubractFloatNodeDescription", "Subtracts floats."))
	DEFINE_METASOUND_MATHOP(Subtract, Int32, int32, MathOpDisplayNames::GetSubtractDisplayName<int32>(), LOCTEXT("Metasound_MathSubractInt32NodeDescription", "Subtracts int32s."))
	DEFINE_METASOUND_MATHOP(Subtract, Int64, int64, MathOpDisplayNames::GetSubtractDisplayName<int64>(), LOCTEXT("Metasound_MathSubractInt64NodeDescription", "Subtracts int64s."))
	DEFINE_METASOUND_MATHOP(Subtract, Frequency, FFrequency, MathOpDisplayNames::GetSubtractDisplayName<FFrequency>(), LOCTEXT("Metasound_MathSubractFrequencyNodeDescription", "Subtracts frequency values."))
	DEFINE_METASOUND_MATHOP(Subtract, FloatTime, FFloatTime, MathOpDisplayNames::GetSubtractDisplayName<FFloatTime>(), LOCTEXT("Metasound_MathSubractFloatTimeNodeDescription", "Subtracts time values."))
	DEFINE_METASOUND_MATHOP(Subtract, AudioBuffer, FAudioBuffer, MathOpDisplayNames::GetSubtractDisplayName<FAudioBuffer>(), LOCTEXT("Metasound_MathSubtractBufferNodeDescription", "Subtracts buffers by sample."))

	DEFINE_METASOUND_MATHOP(Multiply, Float, float, MathOpDisplayNames::GetMultiplyDisplayName<float>(), LOCTEXT("Metasound_MathMultiplyFloatNodeDescription", "Multiplies floats."))
	DEFINE_METASOUND_MATHOP(Multiply, Int32, int32, MathOpDisplayNames::GetMultiplyDisplayName<int32>(), LOCTEXT("Metasound_MathMultiplyInt32NodeDescription", "Multiplies int32s."))
	DEFINE_METASOUND_MATHOP(Multiply, Int64, int64, MathOpDisplayNames::GetMultiplyDisplayName<int64>(), LOCTEXT("Metasound_MathMultiplyInt64NodeDescription", "Multiplies int64s."))
	DEFINE_METASOUND_MATHOP(Multiply, Frequency, FFrequency, MathOpDisplayNames::GetMultiplyDisplayName<FFrequency>(), LOCTEXT("Metasound_MathMultiplyFrequencyNodeDescription", "Multiplies frequency values."))
	DEFINE_METASOUND_MATHOP(Multiply, FloatTime, FFloatTime, MathOpDisplayNames::GetMultiplyDisplayName<FFloatTime>(), LOCTEXT("Metasound_MathMultiplyFloatTimeNodeDescription", "Multiplies time values."))
	DEFINE_METASOUND_MATHOP(Multiply, AudioBuffer, FAudioBuffer, MathOpDisplayNames::GetMultiplyDisplayName<FAudioBuffer>(), LOCTEXT("Metasound_MathMultiplyBufferNodeDescription", "Multiplies buffers together by sample."))

	DEFINE_METASOUND_MATHOP(Divide, Float, float, MathOpDisplayNames::GetDivideDisplayName<float>(), LOCTEXT("Metasound_MathDivideFloatNodeDescription", "Divide float by another float."))
	DEFINE_METASOUND_MATHOP(Divide, Int32, int32, MathOpDisplayNames::GetDivideDisplayName<int32>(), LOCTEXT("Metasound_MathDivideInt32NodeDescription", "Divide int32 by another int32."))
	DEFINE_METASOUND_MATHOP(Divide, Int64, int64, MathOpDisplayNames::GetDivideDisplayName<int64>(), LOCTEXT("Metasound_MathDivideInt64NodeDescription", "Divide int64 by another int64."))
	DEFINE_METASOUND_MATHOP(Divide, Frequency, FFrequency, MathOpDisplayNames::GetDivideDisplayName<FFrequency>(), LOCTEXT("Metasound_MathDivideFrequencyNodeDescription", "Divide frequency by another frequency."))
	DEFINE_METASOUND_MATHOP(Divide, FloatTime, FFloatTime, MathOpDisplayNames::GetDivideDisplayName<FFloatTime>(), LOCTEXT("Metasound_MathDivideFloatTimeNodeDescription", "Divide time by another time."))

	DEFINE_METASOUND_MATHOP(RandRange, Float, float, MathOpDisplayNames::GetRandRangeDisplayName<float>(), LOCTEXT("Metasound_MathRandRangeFloatNodeDescription", "Computes random float value in the range [Min, Max]."))
	DEFINE_METASOUND_MATHOP(RandRange, Int32, int32, MathOpDisplayNames::GetRandRangeDisplayName<int32>(), LOCTEXT("Metasound_MathRandRangeInt32NodeDescription", "Computes random int32 in the range [Min, Max]."))
	DEFINE_METASOUND_MATHOP(RandRange, Int64, int64, MathOpDisplayNames::GetRandRangeDisplayName<int64>(), LOCTEXT("Metasound_MathRandRangeInt64NodeDescription", "Computes random int64 in the range [Min, Max]."))
	DEFINE_METASOUND_MATHOP(RandRange, Frequency, FFrequency, MathOpDisplayNames::GetRandRangeDisplayName<FFrequency>(), LOCTEXT("Metasound_MathRandRangeFrequencyNodeDescription", "Computes random frequency value in the range [Min, Max]."))
	DEFINE_METASOUND_MATHOP(RandRange, FloatTime, FFloatTime, MathOpDisplayNames::GetRandRangeDisplayName<FFloatTime>(), LOCTEXT("Metasound_MathRandRangeFloatTimeNodeDescription", "Computes random time value in the range [Min, Max]."))
}

#undef LOCTEXT_NAMESPACE // MetasoundMathOpNode
