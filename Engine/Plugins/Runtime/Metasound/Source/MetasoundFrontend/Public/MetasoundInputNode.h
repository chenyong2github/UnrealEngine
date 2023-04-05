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
			virtual void Bind(FVertexInterfaceData& InOutVertexData) const override;

			virtual IOperator::FExecuteFunction GetExecuteFunction() override;
			virtual IOperator::FPostExecuteFunction GetPostExecuteFunction() override;
			virtual IOperator::FResetFunction GetResetFunction() override;

		protected:
			FNonExecutableInputOperatorBase(const FVertexName& InVertexName, FAnyDataReference&& InDataRef);

		private:
			void BindInputs(FInputVertexInterfaceData& InOutVertexData) const;
		protected:
			void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) const;


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

			template<typename DataType>
			FNonExecutableInputPassThroughOperator(const FVertexName& InVertexName, const TDataWriteReference<DataType>& InDataRef)
			: FNonExecutableInputPassThroughOperator(InVertexName, TDataReadReference<DataType>(InDataRef))
			{
			}
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
		};

		template<typename DataType>
		class TExecutableInputOperator : public FInputOperatorBase
		{
			static_assert(TExecutableDataType<DataType>::bIsExecutable, "TExecutableInputOperatorBase should only be used with executable data types");
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataWriteReferenceFactory = TDataWriteReferenceLiteralFactory<DataType>;

			TExecutableInputOperator(const FVertexName& InDataReferenceName, TDataWriteReference<DataType> InValue)
				: DataReferenceName(InDataReferenceName)
				, InputValue(InValue)
				, OutputValue(FDataWriteReferenceFactory::CreateNew(*InValue))
			{
			}

			virtual void Bind(FVertexInterfaceData& InOutVertexData) const override
			{
				BindInputs(InOutVertexData.GetInputs());
				BindOutputs(InOutVertexData.GetOutputs());
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return &Execute;
			}

			virtual FResetFunction GetResetFunction() override
			{
				return nullptr;
			}

		private:
			void BindInputs(FInputVertexInterfaceData& InOutVertexData) const
			{
				InOutVertexData.BindWriteVertex(DataReferenceName, InputValue);
			}

		protected:

			void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) const
			{
				InOutVertexData.BindReadVertex(DataReferenceName, OutputValue);
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

		};

		template<typename DataType>
		class TResetableExecutableInputOperator : public TExecutableInputOperator<DataType>
		{
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataWriteReferenceFactory = TDataWriteReferenceLiteralFactory<DataType>;

			TResetableExecutableInputOperator(const FVertexName& InDataReferenceName, const FOperatorSettings& InSettings, const FLiteral& InLiteral)
			: TExecutableInputOperator<DataType>(InDataReferenceName, FDataWriteReferenceFactory::CreateExplicitArgs(InSettings, InLiteral))
			, Literal(InLiteral)
			{
			}

			virtual IOperator::FResetFunction GetResetFunction() override
			{
				return &Reset;
			}

		private:

			static void Reset(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				using FResetableExecutableInputOperator = TResetableExecutableInputOperator<DataType>;

				FResetableExecutableInputOperator* Operator = static_cast<FResetableExecutableInputOperator*>(InOperator);
				check(nullptr != Operator);

				*Operator->InputValue = TDataTypeLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Operator->Literal);
				*Operator->OutputValue = *Operator->InputValue;
			}

			FLiteral Literal;
		};

		template<typename DataType>
		class TPostExecutableInputOperator : public FInputOperatorBase
		{
			static_assert(TPostExecutableDataType<DataType>::bIsPostExecutable, "TPostExecutableInputOperator should only be used with post executable data types");
			static_assert(!TExecutableDataType<DataType>::bIsExecutable, "A data type cannot be Executable and PostExecutable");

		public:
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataWriteReferenceFactory = TDataWriteReferenceLiteralFactory<DataType>;

			TPostExecutableInputOperator(const FVertexName& InDataReferenceName, TDataWriteReference<DataType> InValue)
				: DataReferenceName(InDataReferenceName)
				, Value(InValue)
			{
			}

			virtual void Bind(FVertexInterfaceData& InOutVertexData) const override
			{
				BindInputs(InOutVertexData.GetInputs());
				BindOutputs(InOutVertexData.GetOutputs());
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

			virtual FPostExecuteFunction GetPostExecuteFunction() override
			{
				return &PostExecute;
			}

			virtual FResetFunction GetResetFunction() override
			{
				return nullptr;
			}

		private:
			void BindInputs(FInputVertexInterfaceData& InOutVertexData) const
			{
				InOutVertexData.BindWriteVertex(DataReferenceName, Value);
			}

		protected:

			void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) const
			{
				InOutVertexData.BindReadVertex(DataReferenceName, Value);
			}

			static void PostExecute(IOperator* InOperator)
			{
				using FPostExecutableInputOperator = TPostExecutableInputOperator<DataType>;

				FPostExecutableInputOperator* DerivedOperator = static_cast<FPostExecutableInputOperator*>(InOperator);
				check(nullptr != DerivedOperator);

				TPostExecutableDataType<DataType>::PostExecute(*(DerivedOperator->Value));
			}

			FVertexName DataReferenceName;
			FDataWriteReference Value;

		};

		template<typename DataType>
		class TResetablePostExecutableInputOperator : public TPostExecutableInputOperator<DataType>
		{
		public:
			using FDataWriteReference = TDataWriteReference<DataType>;
			using FDataWriteReferenceFactory = TDataWriteReferenceLiteralFactory<DataType>;

			TResetablePostExecutableInputOperator(const FVertexName& InDataReferenceName, const FOperatorSettings& InSettings, const FLiteral& InLiteral)
			: TPostExecutableInputOperator<DataType>(InDataReferenceName, FDataWriteReferenceFactory::CreateExplicitArgs(InSettings, InLiteral))
			, Literal(InLiteral)
			{
			}

			virtual IOperator::FResetFunction GetResetFunction() override
			{
				return &Reset;
			}

		private:

			static void Reset(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				using FResetablePostExecutableInputOperator = TResetablePostExecutableInputOperator<DataType>;

				FResetablePostExecutableInputOperator* Operator = static_cast<FResetablePostExecutableInputOperator*>(InOperator);
				check(nullptr != Operator);

				*Operator->Value = TDataTypeLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Operator->Literal);
			}

			FLiteral Literal;
		};

		/** Non owning input operator that may need execution. */
		template<typename DataType, EVertexAccessType VertexAccess=EVertexAccessType::Reference>
		using TNonOwningInputOperator = std::conditional_t<
			TExecutableDataType<DataType>::bIsExecutable,
			TExecutableInputOperator<DataType>, // Use this input operator if the data type is not owned by the input node but needs execution.
			std::conditional_t<
				TPostExecutableDataType<DataType>::bIsPostExecutable,
				TPostExecutableInputOperator<DataType>, // Use this input operator if the data type is not owned by the input node but needs post execution.
				MetasoundInputNodePrivate::FNonExecutableInputPassThroughOperator // Use this input operator if the data type is not owned by the input node and is not executable, nor post executable.
			>
		>;
	}

	/** Owning input operator that may need execution. */
	template<typename DataType, EVertexAccessType VertexAccess=EVertexAccessType::Reference>
	using TInputOperator = std::conditional_t<
		VertexAccess == EVertexAccessType::Value || (!TExecutableDataType<DataType>::bIsExecutable && !TPostExecutableDataType<DataType>::bIsPostExecutable),
		MetasoundInputNodePrivate::TInputValueOperator<DataType>, // Use this input operator if the data type is owned by the input node and is not executable, nor post executable.
		std::conditional_t<
			TExecutableDataType<DataType>::bIsExecutable,
			MetasoundInputNodePrivate::TResetableExecutableInputOperator<DataType>, // Use this input operator if the data type is owned by the input node and is executable.
			MetasoundInputNodePrivate::TResetablePostExecutableInputOperator<DataType> // Use this input operator if the data type is owned by the input node and is post executable.

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
			explicit FInputNodeOperatorFactory()
			{
			}

			virtual TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults) override
			{
				using namespace MetasoundInputNodePrivate;

				using FInputNodeType = TInputNode<DataType, VertexAccess>;

				const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);
				const FVertexName& VertexKey = InputNode.GetVertexName();

				if (const FAnyDataReference* Ref = InParams.InputData.FindDataReference(VertexKey))
				{
					if constexpr (bIsReferenceVertexAccess)
					{
						if (EDataReferenceAccessType::Write == Ref->GetAccessType())
						{
							return MakeUnique<TNonOwningInputOperator<DataType, VertexAccess>>(VertexKey, Ref->GetDataWriteReference<DataType>());
						}
					}
					// Pass through input value
					return MakeUnique<TPassThroughOperator<DataType, VertexAccess>>(VertexKey, CreatePassThroughDataReference(*Ref));
				}
				else
				{
					const FLiteral& Literal = InputNode.GetVertexInterface().GetInputInterface()[VertexKey].GetDefaultLiteral();
					// Owned input value
					return MakeUnique<TInputOperator<DataType, VertexAccess>>(VertexKey, InParams.OperatorSettings, Literal);
				}
			}

		private:
		};


	public:
		// If true, this node can be instantiated by the Frontend.
		static constexpr bool bCanRegister = bIsSupportedInput;

		static FVertexInterface CreateVertexInterface(const FVertexName& InVertexName, const FLiteral& InLiteral)
		{
			return FVertexInterface(
				FInputVertexInterface(
					FInputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), FDataVertexMetadata{ FText::GetEmpty() }, VertexAccess, InLiteral)
				),
				FOutputVertexInterface(
					FOutputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), FDataVertexMetadata{ FText::GetEmpty() }, VertexAccess)
				)
			);
		}

		static FVertexInterface CreateDefaultVertexInterface(const FVertexName& InVertexName)
		{
			return CreateVertexInterface(InVertexName, FLiteral());
		}


		UE_DEPRECATED(5.3, "Use TInputNode<>::CreateDefaultVertexInterface or TInputNode<>::CreateVertexInterface instead.")
		static FVertexInterface DeclareVertexInterface(const FVertexName& InVertexName)
		{
			return CreateDefaultVertexInterface(InVertexName);
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
			Info.DefaultInterface = CreateDefaultVertexInterface(InVertexName);

			return Info;
		}

		/* Construct a TInputNode using the TInputOperatorLiteralFactory<> and moving
		 * InParam to the TInputOperatorLiteralFactory constructor.*/
		explicit TInputNode(FInputNodeConstructorParams&& InParams)
		:	FNode(InParams.NodeName, InParams.InstanceID, GetNodeInfo(InParams.VertexName))
		,	VertexName(InParams.VertexName)
		,	Interface(CreateVertexInterface(InParams.VertexName, InParams.InitParam))
		,	Factory(MakeShared<FInputNodeOperatorFactory>())
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
