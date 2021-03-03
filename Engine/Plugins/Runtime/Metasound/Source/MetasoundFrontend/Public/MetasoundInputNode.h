// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	/** TInputOperator supplies a writable input and a readable output. */
	template<typename DataType>
	class TInputOperator : public IOperator
	{
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;

			TInputOperator(const FVertexKey& InDataReferenceName, FDataWriteReference InDataReference)
			{
				Inputs.AddDataWriteReference<DataType>(InDataReferenceName, InDataReference);
				Outputs.AddDataReadReference<DataType>(InDataReferenceName, InDataReference);
			}

			virtual ~TInputOperator() {}

			virtual FDataReferenceCollection GetInputs() const override
			{
				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				return Outputs;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

		private:
			FDataReferenceCollection Outputs;
			FDataReferenceCollection Inputs;
	};

	/** TInputOperatorLiteralFactory creates an input by passing it a literal. */
	template<typename DataType>
	class TInputOperatorLiteralFactory : public IOperatorFactory
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

			TInputOperatorLiteralFactory(FLiteral&& InInitParam)
			:	InitParam(MoveTemp(InInitParam))
			{
			}

			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

		private:

			FLiteral InitParam;
	};

	/** TInputOperatorFactory initializes the DataType at construction. It uses
	 * the DataType's copy operator to create additional version. 
	 */
	template<typename DataType>
	class TInputOperatorFactory : public IOperatorFactory
	{
		public:

			using FDataWriteReference = TDataWriteReference<DataType>;

			template<typename... ArgTypes>
			TInputOperatorFactory(ArgTypes&&... Args)
			:	Data(Forward<ArgTypes>(Args)...)
			{
			}

			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

		private:
			DataType Data;
	};

	/** TInputNode represents an input to a metasound graph. */
	template<typename DataType>
	class TInputNode : public FNode
	{

		public:

			// If true, this node can be instantiated by the FrontEnd.
			static constexpr bool bCanRegister = TInputOperatorLiteralFactory<DataType>::bCanRegister;

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

				Info.ClassName = {TEXT("Input"), GetMetasoundDataTypeName<DataType>(), TEXT("")};
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = FText::Format(LOCTEXT("Metasound_InputNodeDisplayNameFormat", "Input {0}"), FText::FromName(GetMetasoundDataTypeName<DataType>()));
				Info.Description = LOCTEXT("Metasound_InputNodeDescription", "Input into the parent Metasound graph.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface(InVertexName);

				return Info;
			}


			/* Construct a TInputNode using the TInputOperatorFactory<> and forwarding 
			 * Args to the TInputOperatorFactory constructor.*/
			template<typename... ArgTypes>
			TInputNode(const FString& InNodeDescription, const FGuid& InInstanceID, const FVertexKey& InVertexName, ArgTypes&&... Args)
			:	FNode(InNodeDescription, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			,	Factory(MakeOperatorFactoryRef<TInputOperatorFactory<DataType>>(Forward<ArgTypes>(Args)...))
			{
			}

			/* Construct a TInputNode using the TInputOperatorLiteralFactory<> and moving
			 * InParam to the TInputOperatorLiteralFactory constructor.*/
			explicit TInputNode(const FString& InNodeDescription, const FGuid& InInstanceID, const FVertexKey& InVertexName, FLiteral&& InParam)
			:	FNode(InNodeDescription, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			, 	Factory(MakeOperatorFactoryRef<TInputOperatorLiteralFactory<DataType>>(MoveTemp(InParam)))
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
	TUniquePtr<IOperator> TInputOperatorLiteralFactory<DataType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using FInputNodeType = TInputNode<DataType>;

		// Create write reference by calling compatible constructor with literal.
		FDataWriteReference DataRef = TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, InitParam);

		const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);

		return MakeUnique<TInputOperator<DataType>>(InputNode.GetVertexName(), DataRef);
	}

	template<typename DataType>
	TUniquePtr<IOperator> TInputOperatorFactory<DataType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using FInputNodeType = TInputNode<DataType>;

		// Create write reference by calling copy constructor.
		FDataWriteReference DataRef = TDataWriteReferenceFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Data);

		const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);

		return MakeUnique<TInputOperator<DataType>>(InputNode.GetVertexName(), DataRef);
	}

} // namespace Metasound
#undef LOCTEXT_NAMESPACE //MetasoundOutputNode
