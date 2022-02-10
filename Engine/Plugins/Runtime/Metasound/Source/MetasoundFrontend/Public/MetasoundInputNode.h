// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundDataReference.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundTrigger.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	/** A writable input and a readable output. */
	template<typename DataType>
	class TInputOperator : public IOperator
	{
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;

			TInputOperator(const FVertexName& InDataReferenceName, FDataWriteReference InDataReference)
				: DataReferenceName(InDataReferenceName)
				// Executable DataTypes require a copy of the output to operate on whereas non-executable
				// types do not. Avoid copy by assigning to reference for non-executable types.
				, InputValue(InDataReference)
				, OutputValue(TExecutableDataType<DataType>::bIsExecutable ? FDataWriteReference::CreateNew(*InDataReference) : InDataReference)
			{
			}

			virtual ~TInputOperator() = default;

			virtual FDataReferenceCollection GetInputs() const override
			{
				// TODO: Expose a readable reference instead of a writable reference.
				//
				// If data needs to be written to, outside entities should create
				// it and pass it in as a readable reference. Currently, the workflow
				// is to have the input node create a writable reference which is then
				// queried by the outside world. Exposing writable references causes 
				// code maintainability issues where TInputNode<> specializations need
				// to handle multiple situations which can happen in an input node.
				//
				// The only reason that this code is not removed immediately is because
				// of the `TExecutableDataType<>` which primarily supports the FTrigger.
				// The TExecutableDataType<> advances the trigger within the graph. But,
				// with graph composition, the owner of the data type becomes more 
				// complicated and hence triggers advancing should be managed by a 
				// different object. Preferably the graph operator itself, or an
				// explicit trigger manager tied to the environment.
				FDataReferenceCollection Inputs;
				Inputs.AddDataWriteReference<DataType>(DataReferenceName, InputValue);
				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Outputs;
				Outputs.AddDataReadReference<DataType>(DataReferenceName, OutputValue);
				return Outputs;
			}

			void Execute()
			{
				TExecutableDataType<DataType>::Execute(*InputValue, *OutputValue);
			}

			void ExecutPassThrough()
			{
				if (TExecutableDataType<DataType>::bIsExecutable)
				{
					*OutputValue = *InputValue;
				}
			}

			static void ExecuteFunction(IOperator* InOperator)
			{
				static_cast<TInputOperator<DataType>*>(InOperator)->Execute();
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				if (TExecutableDataType<DataType>::bIsExecutable)
				{
					return &TInputOperator<DataType>::ExecuteFunction;
				}
				return nullptr;
			}

		protected:
			FVertexName DataReferenceName;

			FDataWriteReference InputValue;
			FDataWriteReference OutputValue;

	};

	/** TPassThroughOperator supplies a readable input and a readable output. 
	 *
	 * It does *not* invoke executable data types (see `TExecutableDataType<>`).
	 */
	template<typename DataType>
	class TPassThroughOperator : public TInputOperator<DataType>
	{
	public:
		using FDataReadReference = TDataReadReference<DataType>;
		using Super = TInputOperator<DataType>;

		TPassThroughOperator(const FVertexName& InDataReferenceName, FDataReadReference InDataReference)
		: TInputOperator<DataType>(InDataReferenceName, WriteCast(InDataReference)) // Write cast is safe because `GetExecuteFunction() and GetInputs() are overridden, ensuring that data is not written.
		, DataReferenceName(InDataReferenceName)
		{
		}

		virtual ~TPassThroughOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs;
			ensure(Inputs.AddDataReadReferenceFrom(DataReferenceName, Super::GetInputs(), DataReferenceName, GetMetasoundDataTypeName<DataType>()));
			return Inputs;
		}

		static void ExecuteFunction(IOperator* InOperator)
		{
			static_cast<TPassThroughOperator<DataType>*>(InOperator)->ExecutPassThrough();
		}

		virtual IOperator::FExecuteFunction GetExecuteFunction() override
		{
			// TODO: this is a hack until we can remove TExecutableOperator<>.
			//
			// The primary contention is that we would like to allow developers 
			// to specialize `TInputNode<>` as in `TInputNode<FStereoAudioFormat>`.
			// `TExecutableOperator<>` adds in a level of complexity that makes it 
			// difficult to allow specialization of TInputNode and to derive from
			// TInputNode to create the TPassThroughOperator. Particularly because
			// TExecutableOperator<> alters which output data reference is used. 
			// Specializations of TInputNode also tend to alter the output data
			// references. Supporting both is likely to cause issues. 
			//
			// We may need to ensure that input nodes do not provide execution
			// functions. Or we may need a more explicit way of only allowing
			// outputs to be modified. Likely a mix of the `final` keyword
			// and disabling template specialization of a base class. 
			//
			// namespace Private
			// {
			//     class TInputNodePrivate<>
			//     {
			//         GetInputs() final
			//         GetExecutionFunction() final
			//         GetOutputs()
			//     }
			// }
			// 
			// template<DataType>
			// using TInputNodeBase<DataType> = TInputNodePrivate<DataType>; // Do not allow specialization of TInputNodePrivate<> or TInputNodeBase<> (this works because you can't specialize a template alias)
			//
			// // DO ALLOW specialization of TInputNode
			// template<DataType>
			// class TInputNode<DataType> : public TInputNodeBase<DataType>
			// {
			// };
			// 
			// template<>
			// class TInputNode<MyType> : public TInputNodeBase<MyType>
			// {
			// 	 GetOutputs() <-- OK to override
			// }
			//
			if (TExecutableDataType<DataType>::bIsExecutable)
			{
				return &TPassThroughOperator<DataType>::ExecuteFunction;
			}
			return nullptr;
		}

	private:
		FVertexName DataReferenceName;
	};


	/** Data type creation policy to create by copy construction. */
	template<typename DataType>
	struct FCreateDataReferenceWithCopy
	{
		template<typename... ArgTypes>
		FCreateDataReferenceWithCopy(ArgTypes&&... Args)
		:	Data(Forward<ArgTypes>(Args)...)
		{
		}

		TDataWriteReference<DataType> CreateDataReference(const FOperatorSettings& InOperatorSettings) const
		{
			return TDataWriteReferenceFactory<DataType>::CreateExplicitArgs(InOperatorSettings, Data);
		}

	private:
		DataType Data;
	};

	/** Data type creation policy to create by literal construction. */
	template<typename DataType>
	struct FCreateDataReferenceWithLiteral
	{
		// If the data type is parsable from a literal type, then the data type 
		// can be registered as an input type with the frontend.  To make a 
		// DataType registrable, either create a constructor for the data type
		// which accepts the one of the supported literal types with an optional 
		// FOperatorSettings argument, or create a default constructor, or specialize
		// this factory with an implementation for that specific data type.
		static constexpr bool bCanCreateWithLiteral = TLiteralTraits<DataType>::bIsParsableFromAnyLiteralType;

		FCreateDataReferenceWithLiteral(FLiteral&& InLiteral)
		:	Literal(MoveTemp(InLiteral))
		{
		}

		TDataWriteReference<DataType> CreateDataReference(const FOperatorSettings& InOperatorSettings) const
		{
			return TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InOperatorSettings, Literal);
		}

	private:

		FLiteral Literal;
	};

	

	/** TInputOperatorFactory initializes the DataType at construction. It uses
	 * the ReferenceCreatorType to create a data reference if one is not passed in.
	 */
	template<typename DataType, typename ReferenceCreatorType>
	class TInputOperatorFactory : public IOperatorFactory
	{
		public:

			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataReadReference = TDataReadReference<DataType>;

			TInputOperatorFactory(ReferenceCreatorType&& InReferenceCreator)
			:	ReferenceCreator(MoveTemp(InReferenceCreator))
			{
			}

			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

		private:
			ReferenceCreatorType ReferenceCreator;
	};

	/** TInputNode represents an input to a metasound graph. */
	template<typename DataType>
	class TInputNode : public FNode
	{

		public:
			// If true, this node can be instantiated by the FrontEnd.
			static constexpr bool bCanRegister = FCreateDataReferenceWithLiteral<DataType>::bCanCreateWithLiteral;

			static FVertexInterface DeclareVertexInterface(const FVertexName& InVertexName)
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

			static FNodeClassMetadata GetNodeInfo(const FVertexName& InVertexName)
			{
				FNodeClassMetadata Info;

				Info.ClassName = { "Input", GetMetasoundDataTypeName<DataType>(), FName() };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.Description = METASOUND_LOCTEXT("Metasound_InputNodeDescription", "Input into the parent Metasound graph.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface(InVertexName);

				return Info;
			}

			template<typename... ArgTypes>
			static FOperatorFactorySharedRef CreateOperatorFactoryWithArgs(ArgTypes&&... Args)
			{
				using FCreatorType = FCreateDataReferenceWithCopy<DataType>;
				using FFactoryType = TInputOperatorFactory<DataType, FCreatorType>;

				return MakeOperatorFactoryRef<FFactoryType>(FCreatorType(Forward<ArgTypes>(Args)...));
			}

			static FOperatorFactorySharedRef CreateOperatorFactoryWithLiteral(FLiteral&& InLiteral)
			{
				using FCreatorType = FCreateDataReferenceWithLiteral<DataType>;
				using FFactoryType = TInputOperatorFactory<DataType, FCreatorType>;

				return MakeOperatorFactoryRef<FFactoryType>(FCreatorType(MoveTemp(InLiteral)));
			}


			/* Construct a TInputNode using the TInputOperatorFactory<> and forwarding 
			 * Args to the TInputOperatorFactory constructor.*/
			template<typename... ArgTypes>
			TInputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName, ArgTypes&&... Args)
			:	FNode(InInstanceName, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			,	Factory(CreateOperatorFactoryWithArgs(Forward<ArgTypes>(Args)...))
			{
			}

			/* Construct a TInputNode using the TInputOperatorLiteralFactory<> and moving
			 * InParam to the TInputOperatorLiteralFactory constructor.*/
			explicit TInputNode(const FVertexName& InNodeName, const FGuid& InInstanceID, const FVertexName& InVertexName, FLiteral&& InParam)
			:	FNode(InNodeName, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			, 	Factory(CreateOperatorFactoryWithLiteral(MoveTemp(InParam)))
			{
			}

			const FVertexName& GetVertexName() const
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
			FVertexName VertexName;

			FVertexInterface Interface;
			FOperatorFactorySharedRef Factory;
	};


	template<typename DataType, typename ReferenceCreatorType>
	TUniquePtr<IOperator> TInputOperatorFactory<DataType, ReferenceCreatorType>::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using FInputNodeType = TInputNode<DataType>;

		const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);
		const FVertexName& VertexKey = InputNode.GetVertexName();

		if (InParams.InputDataReferences.ContainsDataWriteReference<DataType>(VertexKey))
		{
			// Data is externally owned. Use pass through operator
			FDataWriteReference DataRef = InParams.InputDataReferences.GetDataWriteReference<DataType>(VertexKey);
			return MakeUnique<TPassThroughOperator<DataType>>(InputNode.GetVertexName(), DataRef);
		}
		else if (InParams.InputDataReferences.ContainsDataReadReference<DataType>(VertexKey))
		{
			// Data is externally owned. Use pass through operator
			FDataReadReference DataRef = InParams.InputDataReferences.GetDataReadReference<DataType>(VertexKey);
			return MakeUnique<TPassThroughOperator<DataType>>(InputNode.GetVertexName(), DataRef);
		}
		else
		{
			// Create write reference by calling compatible constructor with literal.
			FDataWriteReference DataRef = ReferenceCreator.CreateDataReference(InParams.OperatorSettings);
			return MakeUnique<TInputOperator<DataType>>(InputNode.GetVertexName(), DataRef);
		}
	}
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundFrontend
