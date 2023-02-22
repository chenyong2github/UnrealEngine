// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundBuildError.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFrontendDataTypeTraits.h"
#include "MetasoundNodeConstructorParams.h"
#include "MetasoundLiteral.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundRouter.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertexData.h"
#include "UObject/NameTypes.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	namespace MetasoundInputNodePrivate
	{
		class METASOUNDFRONTEND_API FInputOperatorBase : public IOperator
		{
		public:
			virtual FDataReferenceCollection GetInputs() const override;
			virtual FDataReferenceCollection GetOutputs() const override;
		};
		
		class METASOUNDFRONTEND_API FNonExecutableInputOperatorBase : public FInputOperatorBase
		{	
		public:
			virtual void Bind(FVertexInterfaceData& InVertexData) const override;
			virtual IOperator::FExecuteFunction GetExecuteFunction() override;

		protected:
			FNonExecutableInputOperatorBase(const FVertexName& InVertexName, FAnyDataReference&& InDataRef);

		private:
			FVertexName VertexName;
			FAnyDataReference DataRef;
		};


		class METASOUNDFRONTEND_API FNonExecutableInputPassThroughOperator : public FNonExecutableInputOperatorBase
		{
		public:
			template<typename DataType>
			FNonExecutableInputPassThroughOperator(const FVertexName& InVertexName, const TDataReadReference<DataType>& InDataRef)
			: FNonExecutableInputOperatorBase(InVertexName, FAnyDataReference{InDataRef})
			{
			}
			virtual IOperator::FResetFunction GetResetFunction() override;
		};


		/** TInputValueOperator provides an input for value references. */
		template<typename DataType>
		class TInputValueOperator : public FNonExecutableInputOperatorBase
		{
		public:
			/** Construct an TInputValueOperator with the name of the vertex and the 
			 * value reference associated with input. 
			 */
			explicit TInputValueOperator(const FName& InVertexName, const TDataValueReference<DataType>& InValueRef)
			: FNonExecutableInputOperatorBase(InVertexName, FAnyDataReference{InValueRef})
			{
			}

			TInputValueOperator(const FVertexName& InVertexName, const FOperatorSettings& InSettings, const FLiteral& InLiteral)
			: FNonExecutableInputOperatorBase(InVertexName, FAnyDataReference{TDataValueReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, InLiteral)})
			{
			}

			virtual IOperator::FResetFunction GetResetFunction() { return nullptr; }
		};


		template<typename DataType> 
		class TNonExecutableInputOperator : public FInputOperatorBase
		{
		public:
			TNonExecutableInputOperator(const FVertexName& InVertexName, const FOperatorSettings& InSettings, const FLiteral& InLiteral)
			: VertexName(InVertexName)
			, DataRef(TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, InLiteral))
			, Literal(InLiteral)
			{
			}

			virtual void Bind(FVertexInterfaceData& InVertexData) const override
			{
				InVertexData.GetInputs().BindWriteVertex(VertexName, DataRef);
				InVertexData.GetOutputs().BindReadVertex(VertexName, DataRef);
			}

			virtual IOperator::FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

			virtual IOperator::FResetFunction GetResetFunction() override
			{
				return &Reset;
			}

		private:
			static void Reset(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				using FOperator = TNonExecutableInputOperator<DataType>;

				FOperator* Operator = static_cast<FOperator*>(InOperator);
				check(nullptr != Operator);

				*Operator->DataRef = TDataTypeLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Operator->Literal);
			}

			FVertexName VertexName;
			TDataWriteReference<DataType> DataRef;
			FLiteral Literal;
		};

		/** A writable input and a readable output. */
		template<typename DataType>
		class TExecutableInputOperator : public FInputOperatorBase
		{
			static_assert(TExecutableDataType<DataType>::bIsExecutable, "TExecutableInputOperatorBase should only be used with executable data types");
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataWriteReferenceFactory = TDataWriteReferenceLiteralFactory<DataType>;

			TExecutableInputOperator(const FVertexName& InDataReferenceName, const FOperatorSettings& InSettings, const FLiteral& InLiteral)
				: DataReferenceName(InDataReferenceName)
				// Executable DataTypes require a copy of the output to operate on whereas non-executable
				// types do not. Avoid copy by assigning to reference for non-executable types.
				, InputValue(FDataWriteReferenceFactory::CreateExplicitArgs(InSettings, InLiteral))
				, OutputValue(FDataWriteReferenceFactory::CreateExplicitArgs(InSettings, InLiteral))
				, Literal(InLiteral)
			{
			}

			virtual void Bind(FVertexInterfaceData& InOutVertexData) const override
			{
				InOutVertexData.GetInputs().BindWriteVertex(DataReferenceName, InputValue);
				InOutVertexData.GetOutputs().BindReadVertex(DataReferenceName, OutputValue);
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return &Execute;
			}

			virtual FResetFunction GetResetFunction() override
			{
				return &Reset;
			}

		private:

			static void Reset(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				using FExecutableInputOperator = TExecutableInputOperator<DataType>;

				FExecutableInputOperator* Operator = static_cast<FExecutableInputOperator*>(InOperator);
				check(nullptr != Operator);

				*Operator->InputValue = TDataTypeLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Operator->Literal);
				*Operator->OutputValue = *Operator->InputValue;
			}

			static void Execute(IOperator* InOperator)
			{
				using FExecutableInputOperator = TExecutableInputOperator<DataType>;

				FExecutableInputOperator* DerivedOperator = static_cast<FExecutableInputOperator*>(InOperator);
				check(nullptr != DerivedOperator);

				TExecutableDataType<DataType>::Execute(*(DerivedOperator->InputValue), *(DerivedOperator->OutputValue));
			}

			FVertexName DataReferenceName;

			FDataWriteReference InputValue;
			FDataWriteReference OutputValue;
			FLiteral Literal;
		};


		/** TInputReceiverOperator provides support for transmittable inputs. */
		template<typename DataType>
		class TInputReceiverOperator : public FInputOperatorBase
		{
		public:
			using FDataReadReference = TDataReadReference<DataType>;
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FInputReceiverOperator = TInputReceiverOperator<DataType>;

			/** Construct an TInputReceiverOperator with the name of the vertex, value reference associated with input, SendAddress, & Receiver
			 */
			TInputReceiverOperator(const FVertexName& InDataReferenceName, FDataReadReference InDataReference, FSendAddress&& InSendAddress, TReceiverPtr<DataType>&& InReceiver)
				: DataReferenceName(InDataReferenceName) 
				, InputValue(InDataReference)
				, OutputValue(FDataWriteReference::CreateNew(*InDataReference))
				, SendAddress(MoveTemp(InSendAddress))
				, Receiver(MoveTemp(InReceiver))
			{
			}

			virtual ~TInputReceiverOperator()
			{
				Receiver.Reset();
				FDataTransmissionCenter::Get().UnregisterDataChannelIfUnconnected(SendAddress);
			}

			virtual void Bind(FVertexInterfaceData& InOutVertexData) const override
			{
				InOutVertexData.GetInputs().BindReadVertex(DataReferenceName, InputValue);
				InOutVertexData.GetOutputs().BindReadVertex(DataReferenceName, OutputValue);
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return &StaticExecute;
			}

			virtual FResetFunction GetResetFunction() override
			{
				return &StaticReset;
			}

		private:
			void Execute()
			{
				DataType& OutputData = *OutputValue;

				bool bHasNewData = Receiver->CanPop();
				if (bHasNewData)
				{
					Receiver->Pop(OutputData);
					bHasNotReceivedData = false;
				}

				if (bHasNotReceivedData)
				{
					OutputData = *InputValue;
					bHasNewData = true;
				}

				if constexpr (TExecutableDataType<DataType>::bIsExecutable)
				{
					TExecutableDataType<DataType>::ExecuteInline(OutputData, bHasNewData);
				}
			}

			static void StaticExecute(IOperator* InOperator)
			{
				using FOperator = TInputReceiverOperator<DataType>;

				FOperator* Operator = static_cast<FOperator*>(InOperator);
				check(nullptr != Operator);
				Operator->Execute();
			}

			void Reset(const IOperator::FResetParams& InParams)
			{
				*OutputValue = *InputValue;
				bHasNotReceivedData = true;
			}

			static void StaticReset(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				using FOperator = TInputReceiverOperator<DataType>;

				FOperator* Operator = static_cast<FOperator*>(InOperator);
				check(nullptr != Operator);
				Operator->Reset(InParams);
			}

			FVertexName DataReferenceName;
			FDataReadReference InputValue;
			FDataWriteReference OutputValue;
			FSendAddress SendAddress;
			TReceiverPtr<DataType> Receiver;
			bool bHasNotReceivedData = true;
		};
	}

	/** Choose input operator based upon data type and access type */
	template<typename DataType, EVertexAccessType VertexAccess=EVertexAccessType::Reference>
	using TInputOperator = std::conditional_t<
		VertexAccess == EVertexAccessType::Value,
		MetasoundInputNodePrivate::TInputValueOperator<DataType>,
		std::conditional_t<
			TExecutableDataType<DataType>::bIsExecutable,
			MetasoundInputNodePrivate::TExecutableInputOperator<DataType>,
			MetasoundInputNodePrivate::TNonExecutableInputOperator<DataType>
		>
	>;

	/** Choose pass through operator based upon data type and access type */
	template<typename DataType, EVertexAccessType VertexAccess=EVertexAccessType::Reference>
	using TPassThroughOperator = std::conditional_t<
		VertexAccess == EVertexAccessType::Value,
		MetasoundInputNodePrivate::TInputValueOperator<DataType>,
		MetasoundInputNodePrivate::FNonExecutableInputPassThroughOperator
	>;


	/** TInputNode represents an input to a metasound graph. */
	template<typename DataType, EVertexAccessType VertexAccess=EVertexAccessType::Reference>
	class TInputNode : public FNode
	{
		static constexpr bool bIsConstructorInput = VertexAccess == EVertexAccessType::Value;
		static constexpr bool bIsSupportedConstructorInput = TIsConstructorVertexSupported<DataType>::Value && bIsConstructorInput;
		static constexpr bool bIsReferenceInput = VertexAccess == EVertexAccessType::Reference;
		static constexpr bool bIsSupportedReferenceInput = TLiteralTraits<DataType>::bIsParsableFromAnyLiteralType && bIsReferenceInput;

		static constexpr bool bIsSupportedInput = bIsSupportedConstructorInput || bIsSupportedReferenceInput;

		// Use Variant names to differentiate between normal input nodes and constructor 
		// input nodes.
		static FName GetVariantName()
		{
			if constexpr (EVertexAccessType::Value == VertexAccess)
			{
				return FName("Constructor");
			}
			else
			{
				return FName();
			}
		}

		// Factory for creating input operators. 
		class FInputNodeOperatorFactory : public IOperatorFactory
		{
			static constexpr bool bIsReferenceVertexAccess = VertexAccess == EVertexAccessType::Reference;
			static constexpr bool bIsValueVertexAccess = VertexAccess == EVertexAccessType::Value;

			static_assert(bIsValueVertexAccess || bIsReferenceVertexAccess, "Unsupported EVertexAccessType");

			// Choose which data reference type is created based on template parameters
			using FDataReference = std::conditional_t<bIsReferenceVertexAccess, TDataReadReference<DataType>, TDataValueReference<DataType>>;
			using FDataReferenceFactory = std::conditional_t<bIsReferenceVertexAccess, TDataReadReferenceLiteralFactory<DataType>, TDataValueReferenceLiteralFactory<DataType>>;
			using FPassThroughDataReference = std::conditional_t<bIsReferenceVertexAccess, TDataReadReference<DataType>, TDataValueReference<DataType>>;

			// Return correct data reference type based on vertex access type for pass through scenario.
			FPassThroughDataReference CreatePassThroughDataReference(const FAnyDataReference& InRef)
			{
				if constexpr (bIsReferenceVertexAccess)
				{
					return InRef.GetDataReadReference<DataType>();
				}
				else if constexpr (bIsValueVertexAccess)
				{
					return InRef.GetDataValueReference<DataType>();
				}
				else
				{
					static_assert("Unsupported EVertexAccessType");
				}
			}

		public:
			explicit FInputNodeOperatorFactory(FLiteral&& InLiteral, bool bInEnableTransmission)
			: Literal(MoveTemp(InLiteral))
			, bEnableTransmission(bInEnableTransmission)
			{
			}

			virtual TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults) override
			{
				using namespace MetasoundInputNodePrivate;

				using FInputNodeType = TInputNode<DataType, VertexAccess>;

				checkf(!(bEnableTransmission && bIsValueVertexAccess), TEXT("Input cannot enable transmission for vertex with access 'EVertexAccessType::Reference'"));

				const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);
				const FVertexName& VertexKey = InputNode.GetVertexName();

				if (bEnableTransmission)
				{
					const FName DataTypeName = GetMetasoundDataTypeName<DataType>();
					FSendAddress SendAddress = FMetaSoundParameterTransmitter::CreateSendAddressFromEnvironment(InParams.Environment, VertexKey, DataTypeName);
					TReceiverPtr<DataType> Receiver = FDataTransmissionCenter::Get().RegisterNewReceiver<DataType>(SendAddress, FReceiverInitParams{ InParams.OperatorSettings });
					if (Receiver.IsValid())
					{
						// Transmittable input value
						if (const FAnyDataReference* Ref = InParams.InputData.FindDataReference(VertexKey))
						{
							return MakeUnique<TInputReceiverOperator<DataType>>(VertexKey, CreatePassThroughDataReference(*Ref), MoveTemp(SendAddress), MoveTemp(Receiver));
						}
						else
						{
							FDataReference DataRef = FDataReferenceFactory::CreateExplicitArgs(InParams.OperatorSettings, Literal);
							return MakeUnique<TInputReceiverOperator<DataType>>(VertexKey, DataRef, MoveTemp(SendAddress), MoveTemp(Receiver));
						}
					}

					AddBuildError<FInputReceiverInitializationError>(OutResults.Errors, InParams.Node, VertexKey, DataTypeName);
					return nullptr;
				}

				if (const FAnyDataReference* Ref = InParams.InputData.FindDataReference(VertexKey))
				{
					// Pass through input value
					return MakeUnique<TPassThroughOperator<DataType, VertexAccess>>(VertexKey, CreatePassThroughDataReference(*Ref));
				}

				// Owned input value
				return MakeUnique<TInputOperator<DataType, VertexAccess>>(VertexKey, InParams.OperatorSettings, Literal);
			}

		private:
			FLiteral Literal;
			bool bEnableTransmission = false;
		};


	public:
		// If true, this node can be instantiated by the Frontend.
		static constexpr bool bCanRegister = bIsSupportedInput;

		static FVertexInterface DeclareVertexInterface(const FVertexName& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					FInputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), FDataVertexMetadata{ FText::GetEmpty() }, VertexAccess)
				),
				FOutputVertexInterface(
					FOutputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), FDataVertexMetadata{ FText::GetEmpty() }, VertexAccess)
				)
			);
		}

		static FNodeClassMetadata GetNodeInfo(const FVertexName& InVertexName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Input", GetMetasoundDataTypeName<DataType>(), GetVariantName() };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = METASOUND_LOCTEXT("Metasound_InputNodeDescription", "Input into the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface(InVertexName);

			return Info;
		}

		/* Construct a TInputNode using the TInputOperatorLiteralFactory<> and moving
		 * InParam to the TInputOperatorLiteralFactory constructor.*/
		explicit TInputNode(FInputNodeConstructorParams&& InParams)
		:	FNode(InParams.NodeName, InParams.InstanceID, GetNodeInfo(InParams.VertexName))
		,	VertexName(InParams.VertexName)
		,	Interface(DeclareVertexInterface(InParams.VertexName))
		,	Factory(MakeShared<FInputNodeOperatorFactory>(MoveTemp(InParams.InitParam), InParams.bEnableTransmission))
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
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundFrontend
