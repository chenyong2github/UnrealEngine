// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundInputNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	/** A writable variable (if non-const) and a readable output that is only accessible within a given graph. */
	template<typename DataType>
	class TGetVariableOperator : public TInputOperator<DataType>
	{
		public:
			TGetVariableOperator(const FVertexKey& InDataReferenceName, TDataWriteReference<DataType> InDataReference)
				: TInputOperator<DataType>(InDataReferenceName, InDataReference)
			{
			}

			virtual ~TGetVariableOperator() = default;

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Inputs;
				Inputs.AddDataWriteReference<DataType>(TInputOperator<DataType>::DataReferenceName, TInputOperator<DataType>::InputValue);
				return Inputs;
			}
	};

	/** TGetVariableOperatorLiteralFactory creates an input by passing it a literal. */
	template<typename DataType>
	class TGetVariableOperatorLiteralFactory : public IOperatorFactory
	{
	public:
		// If the data type is parsable from a literal type, then the data type 
		// can be registered as an input type with the frontend.  To make a 
		// DataType registrable, either create a constructor for the data type
		// which accepts the one of the supported literal types with an optional 
		// FOperatorSettings argument, or create a default constructor, or specialize
		// this factory with an implementation for that specific data type.
		static constexpr bool bCanRegister = TLiteralTraits<DataType>::bIsParsableFromAnyLiteralType;

		using FDataWriteReference = TDataWriteReference<DataType>;

		TGetVariableOperatorLiteralFactory(FLiteral&& InInitParam)
			: InitParam(MoveTemp(InInitParam))
		{
		}

		virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

	private:
		FLiteral InitParam;
	};

	/** TGetVariableOperatorFactory initializes the DataType at construction. It uses
	 * the DataType's copy operator to create additional version.
	 */
	template<typename DataType>
	class TGetVariableOperatorFactory : public IOperatorFactory
	{
	public:

		using FDataWriteReference = TDataWriteReference<DataType>;

		template<typename... ArgTypes>
		TGetVariableOperatorFactory(ArgTypes&&... Args)
			: Data(Forward<ArgTypes>(Args)...)
		{
		}

		virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

	private:
		DataType Data;
	};

	/** TGetVariableNode represents a variable within a metasound graph. */
	template<typename DataType>
	class TGetVariableNode : public FNode
	{
		public:

			// If true, this node can be instantiated by the Frontend.
			static constexpr bool bCanRegister = TGetVariableOperatorLiteralFactory<DataType>::bCanRegister;

			static FVertexInterface DeclareVertexInterface(const FVertexKey& InVertexName)
			{
				return FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<DataType>(InVertexName, FText::GetEmpty())
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<DataType>(InVertexName, FText::GetEmpty())
					)
				);
			}

			static FNodeClassMetadata GetNodeInfo(const FVertexKey& InVertexName)
			{
				FNodeClassMetadata Info;

				Info.ClassName = { TEXT("Variable"), GetMetasoundDataTypeName<DataType>(), TEXT("")};
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = FText::Format(LOCTEXT("Metasound_VariableNodeDisplayNameFormat", "Variable {0}"), FText::FromName(GetMetasoundDataTypeName<DataType>()));
				Info.Description = LOCTEXT("Metasound_VariableNodeDescription", "Variable accessible within a parent Metasound graph.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface(InVertexName);

				return Info;
			}

			/* Construct a TGetVariableNode using the TGetVariableOperatorFactory<> and forwarding
			 * Args to the TGetVariableOperatorFactory constructor.*/
			template<typename... ArgTypes>
			TGetVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID, const FVertexKey& InVertexName, ArgTypes&&... Args)
			:	FNode(InNodeDescription, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			,	Factory(MakeOperatorFactoryRef<TGetVariableOperatorFactory<DataType>>(Forward<ArgTypes>(Args)...))
			{
			}

			/* Construct a TGetVariableNode using the TGetVariableOperatorLiteralFactory<> and moving
			 * InParam to the TGetVariableOperatorLiteralFactory constructor.*/
			explicit TGetVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID, const FVertexKey& InVertexName, FLiteral&& InParam)
			:	FNode(InNodeDescription, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			, 	Factory(MakeOperatorFactoryRef<TGetVariableOperatorLiteralFactory<DataType>>(MoveTemp(InParam)))
			{
			}

			const FVertexKey& GetVertexName() const
			{
				return VertexName;
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
			FVertexKey VertexName;

			FVertexInterface Interface;
			FOperatorFactorySharedRef Factory;
	};

	template<typename DataType>
	TUniquePtr<IOperator> TGetVariableOperatorLiteralFactory<DataType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using FVariableNodeType = TGetVariableNode<DataType>;

		// Create write reference by calling compatible constructor with literal.
		FDataWriteReference DataRef = TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, InitParam);

		const FVariableNodeType& VariableNode = static_cast<const FVariableNodeType&>(InParams.Node);

		return MakeUnique<TGetVariableOperator<DataType>>(VariableNode.GetVertexName(), DataRef);
	}

	template<typename DataType>
	TUniquePtr<IOperator> TGetVariableOperatorFactory<DataType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using FVariableNodeType = TGetVariableNode<DataType>;

		// Create write reference by calling copy constructor.
		FDataWriteReference DataRef = TDataWriteReferenceFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Data);

		const FVariableNodeType& VariableNode = static_cast<const FVariableNodeType&>(InParams.Node);

		return MakeUnique<TGetVariableOperator<DataType>>(VariableNode.GetVertexName(), DataRef);
	}
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundFrontend
