// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"


#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

#define DEFINE_METASOUND_MATHOP(OpName, DataTypeName, DataClass, Description, Keywords) \
	class F##OpName##DataTypeName##Node \
		: public ::Metasound::TMathOpNode<F##OpName##DataTypeName##Node, ::Metasound::TMathOp##OpName<DataClass, DataClass>, DataClass, DataClass> \
	{ \
	public: \
		static FNodeClassName GetClassName() { return { ::Metasound::StandardNodes::Namespace, TEXT(#OpName), TEXT(#DataTypeName)}; } \
		static FText GetDisplayName() { return MathOpNames::Get##OpName##DisplayName<DataClass>(); } \
		static FText GetDescription() { return Description; } \
		static FName GetImageName() { return "MetasoundEditor.Graph.Node.Math." #OpName; } \
		static TArray<FText> GetKeywords() { return Keywords; } \
F##OpName##DataTypeName##Node(const FNodeInitData& InInitData) : TMathOpNode(InInitData) { } \
	};

#define DEFINE_METASOUND_OPERAND_TYPED_MATHOP(OpName, DataTypeName, DataClass, OperandTypeName, OperandDataClass, Description, Keywords) \
	class F##OpName##DataTypeName##OperandTypeName##Node \
		: public ::Metasound::TMathOpNode<F##OpName##DataTypeName##OperandTypeName##Node, ::Metasound::TMathOp##OpName<DataClass, OperandDataClass>, DataClass, OperandDataClass> \
	{ \
	public: \
		static FNodeClassName GetClassName() { return {::Metasound::StandardNodes::Namespace, TEXT(#OpName), TEXT(#DataTypeName " by " #OperandTypeName)}; } \
		static FText GetDisplayName() { return MathOpNames::Get##OpName##DisplayName<DataClass, OperandDataClass>(); } \
		static FText GetDescription() { return Description; } \
		static FName GetImageName() { return "MetasoundEditor.Graph.Node.Math." #OpName; } \
		static TArray<FText> GetKeywords() { return Keywords; } \
		F##OpName##DataTypeName##OperandTypeName##Node(const FNodeInitData& InInitData) : TMathOpNode(InInitData) { } \
	};

namespace Metasound
{
	namespace MathOpNames
	{
		static const FVertexName PrimaryOperandName = TEXT("PrimaryOperand");

		static const FVertexName AdditionalOperandsName = TEXT("AdditionalOperands");

		static const TArray<FText> AddKeywords = { METASOUND_LOCTEXT("AddMathKeyword", "+") };
		static const TArray<FText> SubtractKeywords = { METASOUND_LOCTEXT("SubtractMathKeyword", "-") };
		static const TArray<FText> MultiplyKeywords = { METASOUND_LOCTEXT("MultiplyMathKeyword", "*") };
		static const TArray<FText> DivideKeywords = { METASOUND_LOCTEXT("DivideMathKeyword", "/") };
		static const TArray<FText> PowerKeywords = { METASOUND_LOCTEXT("PowerMathKeyword", "^") };
		static const TArray<FText> ModuloKeywords = { METASOUND_LOCTEXT("ModuloMathKeyword", "%") };

		template<typename DataType>
		const FText GetAddDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("AddNodeDisplayNamePattern1", "Add ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetAddDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("AddNodeDisplayNamePattern2", "Add ({0} to {1})",
				GetMetasoundDataTypeDisplayText<OperandDataType>(),
				GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetSubtractDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("SubtractNodeDisplayNamePattern", "Subtract ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetSubtractDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("SubtractNodeOperandTypedDisplayNamePattern", "Subtract ({0} from {1})",
				GetMetasoundDataTypeDisplayText<OperandDataType>(),
				GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetMultiplyDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("MultiplyNodeDisplayNamePattern", "Multiply ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetMultiplyDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("MultiplyNodeOperandTypedDisplayNamePattern", "Multiply ({0} by {1})",
				GetMetasoundDataTypeDisplayText<DataType>(),
				GetMetasoundDataTypeDisplayText<OperandDataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetDivideDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("DivideNodeDisplayNamePattern", "Divide ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetDivideDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("DivideNodeOperandTypedDisplayNamePattern", "Divide ({0} by {1})",
				GetMetasoundDataTypeDisplayText<DataType>(),
				GetMetasoundDataTypeDisplayText<OperandDataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetModuloDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("ModuloNodeDisplayNamePattern", "Modulo ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetModuloDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("ModuloNodeOperandTypedDisplayNamePattern", "Modulo ({0} by {1})",
				GetMetasoundDataTypeDisplayText<DataType>(),
				GetMetasoundDataTypeDisplayText<OperandDataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetPowerDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("PowerNodeDisplayNamePattern", "Power ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetPowerDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("PowerNodeOperandTypedDisplayNamePattern", "Power ({0} to the power of {1})",
				GetMetasoundDataTypeDisplayText<DataType>(),
				GetMetasoundDataTypeDisplayText<OperandDataType>());
			return DisplayName;
		}

		template<typename DataType>
		const FText GetLogarithmDisplayName()
		{
			static const FText DisplayName = METASOUND_LOCTEXT_FORMAT("LogNodeDisplayNamePattern", "Log ({0})", GetMetasoundDataTypeDisplayText<DataType>());
			return DisplayName;
		}

		template<typename DataType, typename OperandDataType>
		const FText GetLogarithmDisplayName()
		{
			static const FText DisplayName = 
				METASOUND_LOCTEXT_FORMAT("LogarithmNodeOperandTypedDisplayNamePattern", "Log ({0}-Base logarithm of {1})",
				GetMetasoundDataTypeDisplayText<DataType>(),
				GetMetasoundDataTypeDisplayText<OperandDataType>());
			return DisplayName;
		}
	}

	template <typename TDerivedClass, typename TMathOpClass, typename TDataClass, typename TOperandDataClass>
	class TMathOpNode : public FNodeFacade
	{
	private:
		class TMathOperator : public TExecutableOperator<TMathOperator>
		{
		private:
			using TDataClassReadRef = TDataReadReference<TDataClass>;
			using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;
			using TDataClassWriteRef = TDataWriteReference<TDataClass>;

			TMathOpClass InstanceData;

			TDataClassReadRef PrimaryOperandRef;
			TArray<TOperandDataClassReadRef> AdditionalOperandRefs;
			TDataClassWriteRef ValueRef;

		public:
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
					Info.MinorVersion = 1;
					Info.DisplayName = TDerivedClass::GetDisplayName();
					Info.Description = TDerivedClass::GetDescription();
					Info.Author = PluginAuthor;
					Info.PromptIfMissing = PluginNodeMissingPrompt;
					Info.DefaultInterface = TMathOpClass::GetVertexInterface();
					Info.DisplayStyle = DisplayStyle;
					Info.CategoryHierarchy = { NodeCategories::Math };
					Info.Keywords = TDerivedClass::GetKeywords();

					return Info;
				};

				static const FNodeClassMetadata Info = InitNodeInfo();
				return Info;
			}

			void Execute()
			{
				TMathOpClass::Calculate(InstanceData, PrimaryOperandRef, AdditionalOperandRefs, ValueRef);
			}

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
			{
				const TMathOpNode& MathOpNode = static_cast<const TMathOpNode&>(InParams.Node);

				const FInputVertexInterface& InputInterface = MathOpNode.GetVertexInterface().GetInputInterface();

				FLiteral DefaultValue = FLiteral::CreateInvalid();
				const FVertexName& PrimaryOperandName = MathOpNames::PrimaryOperandName;
				if (InputInterface.Contains(PrimaryOperandName))
				{
					DefaultValue = InputInterface[PrimaryOperandName].GetDefaultLiteral();
				}
				TDataClassReadRef PrimaryOperand = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TDataClass>(MathOpNames::PrimaryOperandName, TMathOpClass::GetDefault(InParams.OperatorSettings, DefaultValue));

				// TODO: Support dynamic number of inputs
				const FVertexName& OpName = MathOpNames::AdditionalOperandsName;
				if (InputInterface.Contains(OpName))
				{
					DefaultValue = InputInterface[OpName].GetDefaultLiteral();
				}
				TOperandDataClassReadRef Op1 = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<TOperandDataClass>(OpName, TMathOpClass::GetDefaultOp(InParams.OperatorSettings, DefaultValue));
				TArray<TOperandDataClassReadRef> AdditionalOperandRefs = { Op1 };

				return MakeUnique<TMathOperator>(InParams.OperatorSettings, PrimaryOperand, AdditionalOperandRefs);
			}

			TMathOperator(const FOperatorSettings& InSettings, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands)
				: PrimaryOperandRef(InPrimaryOperand)
				, AdditionalOperandRefs(InAdditionalOperands)
				, ValueRef(TDataWriteReferenceFactory<TDataClass>::CreateAny(InSettings))
			{
				// Set initial value.
				TMathOpClass::Calculate(InstanceData, PrimaryOperandRef, AdditionalOperandRefs, ValueRef);
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection InputDataReferences;
				InputDataReferences.AddDataReadReference(MathOpNames::PrimaryOperandName, PrimaryOperandRef);
				InputDataReferences.AddDataReadReference(MathOpNames::AdditionalOperandsName, AdditionalOperandRefs[0]);
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
	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpAdd
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<TDataClass>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAddNode_InitialTooltip", "Initial addend."), static_cast<TDataClass>(0)),
					TInputDataVertexModel<TOperandDataClass>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAddNode_AddendsTooltip", "Additional addend(s)."), static_cast<TOperandDataClass>(0))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataClass>(TEXT("Out"), METASOUND_LOCTEXT("MathOpAddNode_OutTooltip", "Math operation result"))
				)
			);

			return Interface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(0);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(0);
		}

		static void Calculate(TMathOpAdd<TDataClass, TOperandDataClass>& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				*OutResult += *InAdditionalOperands[i];
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpSubtract
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<TDataClass>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpSubtractNode_MinuendTooltip", "Minuend."), static_cast<TDataClass>(0)),
					TInputDataVertexModel<TOperandDataClass>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpSubtractNode_SubtrahendsTooltip", "Subtrahend(s)."), static_cast<TOperandDataClass>(0))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataClass>(TEXT("Out"), METASOUND_LOCTEXT("MathOpSubtractNode_OutTooltip", "Subtraction result"))
				)
			);

			return Interface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(0);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(0);
		}

		static void Calculate(TMathOpSubtract<TDataClass, TOperandDataClass>& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				*OutResult -= *InAdditionalOperands[i];
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpMultiply
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<TDataClass>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpMultiplyNode_InitMultiplicandTooltip", "Initial multiplicand."), static_cast<TDataClass>(1)),
					TInputDataVertexModel<TOperandDataClass>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpMultiplyNode_MultiplicandsTooltip", "Additional multiplicand(s)."), static_cast<TOperandDataClass>(1))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataClass>(TEXT("Out"), METASOUND_LOCTEXT("MathOpMultiplyNode_ResultTooltip", "Multiplication result"))
				)
			);

			return Interface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(1);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(1);
		}

		static void Calculate(TMathOpMultiply<TDataClass, TOperandDataClass>& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				*OutResult *= *InAdditionalOperands[i];
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpDivide
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<TDataClass>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpDivideNode_DividendTooltip", "Dividend."), static_cast<TDataClass>(1)),
					TInputDataVertexModel<TOperandDataClass>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpDivideNode_DivisorsTooltip", "Divisor(s)."), static_cast<TOperandDataClass>(1))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataClass>(TEXT("Out"), METASOUND_LOCTEXT("MathOpDivideNode_OutTooltip", "Division result"))
				)
			);

			return Interface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(1);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(1);
		}

		static void Calculate(TMathOpDivide& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const TDataClass& OperandValue = *InAdditionalOperands[i];
				if (OperandValue == static_cast<TDataClass>(0))
				{
					// TODO: Error here
					return;
				}

				*OutResult /= OperandValue;
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpModulo
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<TDataClass>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpModuloNode_DividendTooltip", "Dividend."), static_cast<TDataClass>(1)),
					TInputDataVertexModel<TOperandDataClass>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpModuloNode_DivisorsTooltip", "Divisor(s)."), static_cast<TOperandDataClass>(1))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataClass>(TEXT("Out"), METASOUND_LOCTEXT("MathOpModuloNode_OutTooltip", "Modulo result"))
				)
			);

			return Interface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(1);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(1);
		}

		static void Calculate(TMathOpModulo& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;

			// TODO: chaining modulo operations doesn't make too much sense... how do we forbid additional operands in some math ops?
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const TDataClass& OperandValue = *InAdditionalOperands[i];
				if (OperandValue == static_cast<TDataClass>(0))
				{
					// TODO: Error here
					return;
				}

				*OutResult %= OperandValue;
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpPower
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<float>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpPowerNode_Base", "The base of the power")),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpPowerNode_Exponent", "The exponent to take the base to the power of"))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<float>(TEXT("Out"), METASOUND_LOCTEXT("MathOpPowerNode_Result", "Returns Base to the Exponent power"))
				)
			);

			return DefaultInterface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(1);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(1);
		}

		static void Calculate(TMathOpPower& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const TDataClass& OperandValue = *InAdditionalOperands[i];

				*OutResult = FMath::Pow(*OutResult, OperandValue);
			}
		}
	};

	template <typename TDataClass, typename TOperandDataClass = TDataClass>
	class TMathOpLogarithm
	{
	public:
		using TDataClassReadRef = TDataReadReference<TDataClass>;
		using TDataClassWriteRef = TDataWriteReference<TDataClass>;

		using TOperandDataClassReadRef = TDataReadReference<TOperandDataClass>;

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<float>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpLogarithmNode_Base", "The base of the logarithm")),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpLogarithmNode_Value", "The value to find the logarithm of"))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<float>(TEXT("Out"), METASOUND_LOCTEXT("MathOpLogarithmNode_Result", "The logarithm of the inputted value"))
				)
			);

			return DefaultInterface;
		}

		static TDataClass GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TDataClass>(1);
		}

		static TOperandDataClass GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return static_cast<TOperandDataClass>(1);
		}

		static void Calculate(TMathOpLogarithm& InInstanceData, const TDataClassReadRef& InPrimaryOperand, const TArray<TOperandDataClassReadRef>& InAdditionalOperands, TDataClassWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;

			//TODO: Find out how to disallow it from additional inputs, it doesn't really make sense in the context of logarithms
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const TDataClass& OperandValue = *InAdditionalOperands[i];

				*OutResult = FMath::LogX(FMath::Max(SMALL_NUMBER, *OutResult), FMath::Max(SMALL_NUMBER, OperandValue));
			}
		}
	};

	// Specialized Math Operations
	template <>
	class TMathOpAdd<FAudioBuffer>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAddAudioBufferNameTooltip", "First addend.")),
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAddAudioBufferAdditionalTooltip", "Additional addends."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), METASOUND_LOCTEXT("MathOpOutTooltip", "Math operation result"))
				)
			);

			return Interface;
		}

		static FAudioBuffer GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FAudioBuffer(InSettings.GetNumFramesPerBlock());
		}

		static FAudioBuffer GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return GetDefault(InSettings, InVertexDefault);
		}

		static void Calculate(TMathOpAdd<FAudioBuffer>& InInstanceData, FAudioBufferReadRef& InPrimaryOperand, const TArray<FAudioBufferReadRef>& InAdditionalOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InAdditionalOperands[0]->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InPrimaryOperand->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InAdditionalOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InAdditionalOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER)
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
	class TMathOpAdd<FTime>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FTime>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAddTimeTooltip", "First addend."), 0.0f),
					TInputDataVertexModel<FTime>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAddTimeAdditionalalTooltip", "Additional addends."), 0.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTime>(TEXT("Out"), METASOUND_LOCTEXT("MathOpOutTooltip", "Math operation result"))
				)
			);

			return Interface;
		}

		static FTime GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FTime(InVertexDefault.Value.Get<float>());
		}

		static FTime GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return GetDefault(InSettings, InVertexDefault);
		}

		static void Calculate(TMathOpAdd& InInstanceData, const FTimeReadRef& InPrimaryOperand, const TArray<FTimeReadRef>& InAdditionalOperands, FTimeWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				*OutResult += *InAdditionalOperands[i];
			}
		}
	};

	template <>
	class TMathOpAdd<FAudioBuffer, float>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAddAudioTooltip", "Audio Buffer to add offset(s) to.")),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAddAdditionalTooltip", "Float addends of which to offset buffer samples."), 0.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), METASOUND_LOCTEXT("MathOpAudioFloatAddOutTooltip", "Resulting buffer"))
				)
			);

			return Interface;
		}

		static FAudioBuffer GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FAudioBuffer(InSettings.GetNumFramesPerBlock());
		}

		static float GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return InVertexDefault.Value.Get<float>();
		}

		static void Calculate(TMathOpAdd& InInstanceData, const FAudioBufferReadRef& InPrimaryOperand, const TArray<FFloatReadRef>& InAdditionalOperands, FAudioBufferWriteRef& OutResult)
		{
			FMemory::Memcpy(OutResult->GetData(), InPrimaryOperand->GetData(), sizeof(float) * OutResult->Num());

			const int32 SIMDRemainder = OutResult->Num() % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER;
			const int32 SIMDCount = OutResult->Num() - SIMDRemainder;

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				Audio::AddConstantToBufferInplace(OutResult->GetData(), SIMDCount, *InAdditionalOperands[i]);

				for (int32 j = SIMDCount; j < OutResult->Num(); ++j)
				{
					OutResult->GetData()[j] += *InAdditionalOperands[i];
				}
			}
		}
	};

	template <>
	class TMathOpSubtract<FAudioBuffer>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpBuffersMinuendTooltip", "Initial buffer to act as minuend.")),
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpSubtractBuffersSubtrahendsTooltip", "Additional buffers to act as subtrahend(s)."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), METASOUND_LOCTEXT("MathOpSubtractBuffersOutTooltip", "Resulting buffer"))
				)
			);

			return Interface;
		}

		static FAudioBuffer GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FAudioBuffer(InSettings.GetNumFramesPerBlock());
		}

		static FAudioBuffer GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return GetDefault(InSettings, InVertexDefault);
		}

		static void Calculate(TMathOpSubtract& InInstanceData, FAudioBufferReadRef& InPrimaryOperand, const TArray<FAudioBufferReadRef>& InAdditionalOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InAdditionalOperands[0]->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InPrimaryOperand->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InAdditionalOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InAdditionalOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER)
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
	class TMathOpSubtract<FTime>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FTime>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpTimeNode_MinuendTooltip", "Time minuend."), 0.0f),
					TInputDataVertexModel<FTime>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpTimeNode_SubtrahendsTooltip", "Time subtrahends."), 0.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTime>(TEXT("Out"), METASOUND_LOCTEXT("MathOpTimeNode_SubtractOutTooltip", "Resulting time value"))
				)
			);

			return Interface;
		}

		static FTime GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FTime(InVertexDefault.Value.Get<float>());
		}

		static FTime GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return GetDefault(InSettings, InVertexDefault);
		}

		static void Calculate(TMathOpSubtract& InInstanceData, const FTimeReadRef& InPrimaryOperand, const TArray<FTimeReadRef>& InAdditionalOperands, FTimeWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			*OutResult = *InPrimaryOperand;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				*OutResult -= *InAdditionalOperands[i];
			}
		}
	};

	template <>
	class TMathOpMultiply<FAudioBuffer>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAudioMultiplyNode_InitMultiplicandTooltip", "Initial audio to multiply.")),
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAudioMultiplyNode_MultiplicandsTooltip", "Additional audio to multiply sample-by-sample."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), METASOUND_LOCTEXT("MathOpAudioMultiplyNode_OutTooltip", "Resulting buffer"))
				)
			);

			return Interface;
		}

		static FAudioBuffer GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FAudioBuffer(InSettings.GetNumFramesPerBlock());
		}

		static FAudioBuffer GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return GetDefault(InSettings, InVertexDefault);
		}

		static void Calculate(TMathOpMultiply& InInstanceData, const FAudioBufferReadRef& InPrimaryOperand, const TArray<FAudioBufferReadRef>& InAdditionalOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				OutResult->Zero();
				return;
			}

			const int32 NumSamples = InPrimaryOperand->Num();
			if (!ensure(NumSamples == OutResult->Num()))
			{
				// TODO: Error
				OutResult->Zero();
				return;
			}

			FMemory::Memcpy(OutResult->GetData(), InPrimaryOperand->GetData(), sizeof(float) * NumSamples);

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const FAudioBuffer& Operand = *InAdditionalOperands[i];
				if (!ensure(NumSamples == Operand.Num()))
				{
					// TODO: Error
					OutResult->Zero();
					return;
				}

				const float* OperandData = InAdditionalOperands[i]->GetData();
				float* OutData = OutResult->GetData();

				if (NumSamples % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER)
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
	class TMathOpMultiply<FAudioBuffer, float>
	{
		bool bInit = false;
		float LastGain = 0.0f;

	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpAudioFloatMultiplyNode_FloatTooltip", "Audio multiplicand.")),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpAudioFloatMultiplyNode_MultiplicandTooltip", "Float multiplicand to apply sample-by-sample to audio. Interpolates over buffer size on value change."), 1.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), METASOUND_LOCTEXT("MathOpAudioFloatMultiplyNode_OutTooltip", "Resulting buffer"))
				)
			);

			return Interface;
		}

		static FAudioBuffer GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FAudioBuffer(InSettings.GetNumFramesPerBlock());
		}

		static float GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return InVertexDefault.Value.Get<float>();
		}

		static void Calculate(TMathOpMultiply& InInstanceData, const FAudioBufferReadRef& InPrimaryOperand, const TArray<FFloatReadRef>& InAdditionalOperands, FAudioBufferWriteRef& OutResult)
		{
			if (!InInstanceData.bInit)
			{
				InInstanceData.bInit = true;
				InInstanceData.LastGain = 1.0f;

				for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
				{
					InInstanceData.LastGain *= *InAdditionalOperands[i];
				}
			}

			FMemory::Memcpy(OutResult->GetData(), InPrimaryOperand->GetData(), sizeof(float) * InPrimaryOperand->Num());

			float NewGain = 1.0f;
			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const int32 SIMDRemainder = OutResult->Num() % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER;
				const int32 SIMDCount = OutResult->Num() - SIMDRemainder;

				Audio::FadeBufferFast(OutResult->GetData(), SIMDCount, InInstanceData.LastGain, *InAdditionalOperands[i]);

				for (int32 j = SIMDCount; j < OutResult->Num(); ++j)
				{
					OutResult->GetData()[j] *= *InAdditionalOperands[j];
				}

				NewGain *= *InAdditionalOperands[i];
			}

			InInstanceData.LastGain = NewGain;
		}
	};

	template <>
	class TMathOpMultiply<FTime, float>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FTime>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpTimeFloatMultiplyNode_MultiplyFloatTooltip", "Time multiplicand."), 1.0f),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpTimeFloatMultiplyNode_MultiplicandsTooltip", "Float multiplicand(s)."), 1.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTime>(TEXT("Out"), METASOUND_LOCTEXT("MathOpTimeFloatMultiplyNode_OutTooltip", "Time float multiplication output"))
				)
			);

			return Interface;
		}

		static FTime GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FTime(InVertexDefault.Value.Get<float>());
		}

		static float GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return 1.0f;
		}

		static void Calculate(TMathOpMultiply& InInstanceData, const FTimeReadRef& InPrimaryOperand, const TArray<FFloatReadRef>& InAdditionalOperands, FTimeWriteRef& OutResult)
		{
			*OutResult = *InPrimaryOperand;

			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const float OperandValue = *InAdditionalOperands[i];
				*OutResult *= OperandValue;
			}
		}
	};

	template <>
	class TMathOpDivide<FTime, float>
	{
	public:
		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FTime>(MathOpNames::PrimaryOperandName, METASOUND_LOCTEXT("MathOpTimeFloatDivideNode_DividendFloatTooltip", "Time dividend."), 1.0f),
					TInputDataVertexModel<float>(MathOpNames::AdditionalOperandsName, METASOUND_LOCTEXT("MathOpTimeFloatDivideNode_DivisorsTooltip", "Float divisor(s)."), 1.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTime>(TEXT("Out"), METASOUND_LOCTEXT("MathOpTimeFloatDivideNode_OutTooltip", "Time divide-by-float output"))
				)
			);

			return Interface;
		}

		static FTime GetDefault(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return FTime(InVertexDefault.Value.Get<float>());
		}

		static float GetDefaultOp(const FOperatorSettings& InSettings, const FLiteral& InVertexDefault)
		{
			return 1.0f;
		}

		static void Calculate(TMathOpDivide& InInstanceData, const FTimeReadRef& InPrimaryOperand, const TArray<FFloatReadRef>& InAdditionalOperands, FTimeWriteRef& OutResult)
		{
			*OutResult = *InPrimaryOperand;

			if (!ensure(!InAdditionalOperands.IsEmpty()))
			{
				return;
			}

			for (int32 i = 0; i < InAdditionalOperands.Num(); ++i)
			{
				const float OperandValue = *InAdditionalOperands[i];
				if (OperandValue == 0.0f)
				{
					// TODO: Error here
					continue;
				}

				*OutResult /= OperandValue;
			}
		}
	};

	// Definitions
	DEFINE_METASOUND_MATHOP(Add, Float, float, METASOUND_LOCTEXT("MathAddFloatNode_Description", "Adds floats."), MathOpNames::AddKeywords)
	DEFINE_METASOUND_MATHOP(Add, Int32, int32, METASOUND_LOCTEXT("MathAddInt32Node_Description", "Adds int32s."), MathOpNames::AddKeywords)
	DEFINE_METASOUND_MATHOP(Add, Audio, FAudioBuffer, METASOUND_LOCTEXT("MathAddBufferNode_Description", "Adds buffers together by sample."), MathOpNames::AddKeywords)
	DEFINE_METASOUND_MATHOP(Add, Time, FTime, METASOUND_LOCTEXT("MathAddTimeNode_Description", "Adds time values."), MathOpNames::AddKeywords)

	DEFINE_METASOUND_OPERAND_TYPED_MATHOP(Add, Audio, FAudioBuffer, Float, float, METASOUND_LOCTEXT("MathAddAudioFloatNode_Description", "Add floats to buffer sample-by-sample."), MathOpNames::AddKeywords)

	DEFINE_METASOUND_MATHOP(Subtract, Float, float, METASOUND_LOCTEXT("MathSubractFloatNode_Description", "Subtracts floats."), MathOpNames::SubtractKeywords)
	DEFINE_METASOUND_MATHOP(Subtract, Int32, int32, METASOUND_LOCTEXT("MathSubractInt32Node_Description", "Subtracts int32s."), MathOpNames::SubtractKeywords)
	DEFINE_METASOUND_MATHOP(Subtract, Audio, FAudioBuffer, METASOUND_LOCTEXT("MathSubtractBufferNode_Description", "Subtracts buffers sample-by-sample."), MathOpNames::SubtractKeywords)
	DEFINE_METASOUND_MATHOP(Subtract, Time, FTime, METASOUND_LOCTEXT("MathSubractTimeNode_Description", "Subtracts time values."), MathOpNames::SubtractKeywords)

	DEFINE_METASOUND_MATHOP(Multiply, Float, float, METASOUND_LOCTEXT("MathMultiplyFloatNode_Description", "Multiplies floats."), MathOpNames::MultiplyKeywords)
	DEFINE_METASOUND_MATHOP(Multiply, Int32, int32, METASOUND_LOCTEXT("MathMultiplyInt32Node_Description", "Multiplies int32s."), MathOpNames::MultiplyKeywords)
	DEFINE_METASOUND_MATHOP(Multiply, Audio, FAudioBuffer, METASOUND_LOCTEXT("MathMultiplyBufferNode_Description", "Multiplies buffers together sample-by-sample."), MathOpNames::MultiplyKeywords)

	DEFINE_METASOUND_OPERAND_TYPED_MATHOP(Multiply, Audio, FAudioBuffer, Float, float, METASOUND_LOCTEXT("MathMultiplyAudioByFloatDescription", "Multiplies buffer by float scalars."), MathOpNames::MultiplyKeywords)
	DEFINE_METASOUND_OPERAND_TYPED_MATHOP(Multiply, Time, FTime, Float, float, METASOUND_LOCTEXT("MathMultiplyTimeNode_Description", "Scales time by floats."), MathOpNames::MultiplyKeywords)

	DEFINE_METASOUND_MATHOP(Divide, Float, float, METASOUND_LOCTEXT("MathDivideFloatNode_Description", "Divide float by another float."), MathOpNames::DivideKeywords)
	DEFINE_METASOUND_MATHOP(Divide, Int32, int32, METASOUND_LOCTEXT("MathDivideInt32Node_Description", "Divide int32 by another int32."), MathOpNames::DivideKeywords)

	DEFINE_METASOUND_OPERAND_TYPED_MATHOP(Divide, Time, FTime, Float, float, METASOUND_LOCTEXT("MathDivideTimeNode_Description", "Divides time by floats."), MathOpNames::DivideKeywords)

	DEFINE_METASOUND_MATHOP(Modulo, Int32, int32, METASOUND_LOCTEXT("MathModulusInt32Node_Description", "Modulo int32 by another int32."), MathOpNames::ModuloKeywords)

	DEFINE_METASOUND_MATHOP(Power, Float, float, METASOUND_LOCTEXT("MathPowerFloatNode_Description", "Raise float to the power of another float."), MathOpNames::PowerKeywords)

	DEFINE_METASOUND_MATHOP(Logarithm, Float, float, METASOUND_LOCTEXT("MathLogarithmFloatNode_Description", "Calculate float-base logarithm of another float."), TArray<FText>())

	METASOUND_REGISTER_NODE(FAddFloatNode)
	METASOUND_REGISTER_NODE(FAddInt32Node)
	METASOUND_REGISTER_NODE(FAddTimeNode)
	METASOUND_REGISTER_NODE(FAddAudioNode)
	METASOUND_REGISTER_NODE(FAddAudioFloatNode)

	METASOUND_REGISTER_NODE(FSubtractFloatNode)
	METASOUND_REGISTER_NODE(FSubtractInt32Node)
	METASOUND_REGISTER_NODE(FSubtractTimeNode)
	METASOUND_REGISTER_NODE(FSubtractAudioNode)

	METASOUND_REGISTER_NODE(FMultiplyAudioNode)
	METASOUND_REGISTER_NODE(FMultiplyAudioFloatNode)
	METASOUND_REGISTER_NODE(FMultiplyFloatNode)
	METASOUND_REGISTER_NODE(FMultiplyInt32Node)
	METASOUND_REGISTER_NODE(FMultiplyTimeFloatNode)

	METASOUND_REGISTER_NODE(FDivideFloatNode)
	METASOUND_REGISTER_NODE(FDivideInt32Node)
	METASOUND_REGISTER_NODE(FDivideTimeFloatNode)

	METASOUND_REGISTER_NODE(FModuloInt32Node)

	METASOUND_REGISTER_NODE(FPowerFloatNode)

	METASOUND_REGISTER_NODE(FLogarithmFloatNode)
}

#undef LOCTEXT_NAMESPACE // MetasoundMathOpNode
