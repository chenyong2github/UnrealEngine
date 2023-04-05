// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundInputNode.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundParamHelper.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	namespace LiteralNodeNames
	{
		METASOUND_PARAM(OutputValue, "Value", "Value")
	}

	template<typename DataType>
	class UE_DEPRECATED(5.3, "The TLiteralOperator will be removed.") TLiteralOperator : public FNoOpOperator
	{
		static_assert(!TExecutableDataType<DataType>::bIsExecutable, "TLiteralOperator is only suitable for non-executable data types");

	public:
		TLiteralOperator(TDataValueReference<DataType> InValue)
		: Value (InValue)
		{
		}

		virtual ~TLiteralOperator() = default;

		virtual void Bind(FVertexInterfaceData& InVertexData) const
		{
			using namespace LiteralNodeNames;
			InVertexData.GetOutputs().BindValueVertex(METASOUND_GET_PARAM_NAME(OutputValue), Value);
		}

	private:
		TDataValueReference<DataType> Value;
	};

	/** TExecutableLiteralOperator is used for executable types. The majority of literals
	 * are constant values, but executable data types cannot be constant values.
	 * 
	 * Note: this is supporting for FTrigger literals which are deprecated.
	 */
	template<typename DataType>
	class UE_DEPRECATED(5.3, "The TExecutableLiteralOperator will be removed.") TExecutableLiteralOperator : public TExecutableOperator<TExecutableLiteralOperator<DataType>>
	{
		static_assert(TExecutableDataType<DataType>::bIsExecutable, "TExecutableLiteralOperator is only suitable for executable data types");

	public:
		using FDataWriteReference = TDataWriteReference<DataType>;

		TExecutableLiteralOperator(const FOperatorSettings& InSettings, const FLiteral& InLiteral)
			// Executable DataTypes require a copy of the output to operate on whereas non-executable
			// types do not. Avoid copy by assigning to reference for non-executable types.
			: Literal(InLiteral)
			, InputValue(TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, InLiteral))
			, OutputValue(TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, InLiteral))
		{
		}

		virtual ~TExecutableLiteralOperator() = default;

		virtual void Bind(FVertexInterfaceData& InVertexData) const
		{
			using namespace LiteralNodeNames;
			InVertexData.GetOutputs().BindReadVertex(METASOUND_GET_PARAM_NAME(OutputValue), OutputValue);
		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs;
			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace LiteralNodeNames;
			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference<DataType>(METASOUND_GET_PARAM_NAME(OutputValue), OutputValue);
			return Outputs;
		}

		void Execute()
		{
			TExecutableDataType<DataType>::Execute(*InputValue, *OutputValue);
		}

		void Reset(const IOperator::FResetParams& InParams)
		{
			*OutputValue = *InputValue = TDataTypeLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Literal);
		}

	private:
		FLiteral Literal;
		FDataWriteReference InputValue;
		FDataWriteReference OutputValue;
	};


	/** TLiteralOperatorLiteralFactory creates an input by passing it a literal. */
	template<typename DataType>
	class TLiteralOperatorLiteralFactory : public IOperatorFactory
	{
	public:

		using FDataWriteReference = TDataWriteReference<DataType>;

		TLiteralOperatorLiteralFactory(FLiteral&& InInitParam)
			: InitParam(MoveTemp(InInitParam))
		{
		}

		virtual TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults) override;

	private:

		class FLiteralOperator : public TInputOperator<DataType>
		{
		public:
			using TInputOperator<DataType>::TInputOperator;
			virtual void Bind(FVertexInterfaceData& InVertexData) const override
			{
				// Do not bind inputs as there are no inputs exposed from the literal node. 
				TInputOperator<DataType>::BindOutputs(InVertexData.GetOutputs());
			}
		};

		FLiteral InitParam;
	};

	/** TLiteralNode represents a variable within a metasound graph. */
	template<typename DataType>
	class TLiteralNode : public FNode
	{
		// Executable data types handle Triggers which need to be advanced every
		// block.
		static constexpr bool bIsExecutableDataType = TExecutableDataType<DataType>::bIsExecutable;
		static constexpr bool bIsPostExecutableDataType = TPostExecutableDataType<DataType>::bIsPostExecutable;

	public:

		static FVertexInterface DeclareVertexInterface()
		{
			using namespace LiteralNodeNames; 
			static const FDataVertexMetadata OutputMetadata
			{
				  FText::GetEmpty() // description
				, METASOUND_GET_PARAM_DISPLAYNAME(OutputValue) // display name
			};

			if constexpr (bIsExecutableDataType || bIsPostExecutableDataType)
			{
				return FVertexInterface(
					FInputVertexInterface(),
					FOutputVertexInterface(
						TOutputDataVertex<DataType>(METASOUND_GET_PARAM_NAME(OutputValue), OutputMetadata)
					)
				);
			}
			else
			{
				// If the data type is not executable, we treat it as a constructor
				// vertex to produce a constant value.
				return FVertexInterface(
					FInputVertexInterface(),
					FOutputVertexInterface(
						TOutputConstructorVertex<DataType>(METASOUND_GET_PARAM_NAME(OutputValue), OutputMetadata)
					)
				);
			}
		}

		static FNodeClassMetadata GetNodeInfo()
		{
			FNodeClassMetadata Info;

			Info.ClassName = {"Literal", GetMetasoundDataTypeName<DataType>(), ""};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
#if WITH_EDITOR
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_LiteralNodeDisplayNameFormat", "Literal {0}"), FText::FromName(GetMetasoundDataTypeName<DataType>()));
			Info.Description = LOCTEXT("Metasound_LiteralNodeDescription", "Literal accessible within a parent Metasound graph.");
#endif // WITH_EDITOR
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		}

		/* Construct a TLiteralNode using the TLiteralOperatorLiteralFactory<> and moving
		 * InParam to the TLiteralOperatorLiteralFactory constructor.*/
		explicit TLiteralNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, FLiteral&& InParam)
		:	FNode(InInstanceName, InInstanceID, GetNodeInfo())
		,	Interface(DeclareVertexInterface())
		, 	Factory(MakeOperatorFactoryRef<TLiteralOperatorLiteralFactory<DataType>>(MoveTemp(InParam)))
		{
		}

		virtual const FVertexInterface& GetVertexInterface() const override
		{
			return Interface;
		}

		virtual bool SetVertexInterface(const FVertexInterface& InInterface) override
		{
			return Interface == InInterface;
		}

		virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override
		{
			return Interface == InInterface;
		}

		virtual TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FVertexInterface Interface;
		FOperatorFactorySharedRef Factory;
	};

	template<typename DataType>
	TUniquePtr<IOperator> TLiteralOperatorLiteralFactory<DataType>::CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		using namespace LiteralNodeNames;
		return MakeUnique<FLiteralOperator>(METASOUND_GET_PARAM_NAME(OutputValue), InParams.OperatorSettings, InitParam);
	}

} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundGraphCore

