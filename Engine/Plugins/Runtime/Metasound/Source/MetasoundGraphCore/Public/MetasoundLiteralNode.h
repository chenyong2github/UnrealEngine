// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	namespace LiteralNodeNames
	{
		METASOUNDGRAPHCORE_API const TCHAR* GetOutputDataName();
	}

	template<typename DataType>
	class TLiteralOperator : public IOperator
	{
	public:
		using FDataWriteReference = TDataWriteReference<DataType>;

		TLiteralOperator(FDataWriteReference InDataReference)
			// Executable DataTypes require a copy of the output to operate on whereas non-executable
			// types do not. Avoid copy by assigning to reference for non-executable types.
			: InputValue(InDataReference)
			, OutputValue(TExecutableDataType<DataType>::bIsExecutable ? FDataWriteReference::CreateNew(*InDataReference) : InDataReference)
		{
		}

		virtual ~TLiteralOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs;
			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference<DataType>(LiteralNodeNames::GetOutputDataName(), OutputValue);
			return Outputs;
		}

		void Execute()
		{
			TExecutableDataType<DataType>::Execute(*InputValue, *OutputValue);
		}

		static void ExecuteFunction(IOperator* InOperator)
		{
			static_cast<TLiteralOperator<DataType>*>(InOperator)->Execute();
		}

		virtual FExecuteFunction GetExecuteFunction() override
		{
			if (TExecutableDataType<DataType>::bIsExecutable)
			{
				return &TLiteralOperator<DataType>::ExecuteFunction;
			}
			return nullptr;
		}

	private:
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

		virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

	private:
		FLiteral InitParam;
	};

	/** TLiteralNode represents a variable within a metasound graph. */
	template<typename DataType>
	class TLiteralNode : public FNode
	{
	public:

		static FVertexInterface DeclareVertexInterface()
		{
			return FVertexInterface(
				FInputVertexInterface(),
				FOutputVertexInterface(
					TOutputDataVertexModel<DataType>(LiteralNodeNames::GetOutputDataName(), FText::GetEmpty())
				)
			);
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
	TUniquePtr<IOperator> TLiteralOperatorLiteralFactory<DataType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using FLiteralNodeType = TLiteralNode<DataType>;

		// Create write reference by calling compatible constructor with literal.
		FDataWriteReference DataRef = TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, InitParam);

		return MakeUnique<TLiteralOperator<DataType>>(DataRef);
	}

} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundGraphCore

