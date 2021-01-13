// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundBop.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"


#define LOCTEXT_NAMESPACE "MetasoundMathOpNode"
namespace Metasound
{
	template <typename TDerivedClass, typename TDataClass>
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

			static const FNodeInfo& GetNodeInfo()
			{
				auto InitNodeInfo = []() -> FNodeInfo
				{
					FNodeInfo Info;
					Info.ClassName = TDerivedClass::GetClassName();
					Info.MajorVersion = 1;
					Info.MinorVersion = 0;
					Info.Description = TDerivedClass::GetDescription();
					Info.Author = PluginAuthor;
					Info.PromptIfMissing = PluginNodeMissingPrompt;
					Info.DefaultInterface = DeclareVertexInterface();

					return Info;
				};

				static const FNodeInfo Info = InitNodeInfo();
				return Info;
			}

			void Execute()
			{
				TDerivedClass::Calculate(OperandRefs, ValueRef);
			}

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
			{
				const TMathOpNode& MathOpNode = static_cast<const TMathOpNode&>(InParams.Node);

				// TODO: Support dynamic number of inputs
				TDataClassReadRef Op1 = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TDataClass>(TEXT("Operand1"), TDerivedClass::GetDefault(InParams.OperatorSettings));
				TDataClassReadRef Op2 = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TDataClass>(TEXT("Operand2"), TDerivedClass::GetDefault(InParams.OperatorSettings));

				TArray<TDataClassReadRef> OperandRefs = { Op1, Op2 };

				return MakeUnique<TMathOperator>(InParams.OperatorSettings, OperandRefs);
			}

			TMathOperator(const FOperatorSettings& InSettings, const TArray<TDataClassReadRef>& InOperands)
			:	OperandRefs(InOperands)
			,	ValueRef(TDataClassWriteRef::CreateNew(TDerivedClass::GetDefault(InSettings)))
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
			: FNodeFacade(InInitData.InstanceName, TFacadeOperatorClass<TMathOperator>())
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

		static TDataClass GetDefault(const FOperatorSettings& InSettings)
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

		static TDataClass GetDefault(const FOperatorSettings& InSettings)
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

		static TDataClass GetDefault(const FOperatorSettings& InSettings)
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

		static TDataClass GetDefault(const FOperatorSettings& InSettings)
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

	template <typename TDataClass>
	class TMathOpRandRange
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		static TDataClass GetDefault(const FOperatorSettings& InSettings)
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
}

#define DEFINE_METASOUND_MATHOP(OpName, DataTypeName, DataClass, Description) \
	class F##OpName##DataTypeName##Node \
		: public ::Metasound::TMathOpNode<F##OpName##DataTypeName##Node, DataClass> \
		, public ::Metasound::TMathOp##OpName<DataClass> \
	{ \
	public: \
		static FName GetClassName() { return #OpName #DataTypeName; } \
		static FText GetDescription() { return Description; } \
		F##OpName##DataTypeName##Node(const FNodeInitData& InInitData) : TMathOpNode(InInitData) { } \
	}; \
	METASOUND_REGISTER_NODE(F##OpName##DataTypeName##Node)

#undef LOCTEXT_NAMESPACE // MetasoundMathOpNode
