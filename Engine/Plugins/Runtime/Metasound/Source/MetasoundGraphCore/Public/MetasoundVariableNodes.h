// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundBuilderInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundLog.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundVariable.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	namespace VariableNames
	{
		/** Input vertex name for data */
		METASOUNDGRAPHCORE_API const FVertexName& GetInputDataName();

		/** Output vertex name for data */
		METASOUNDGRAPHCORE_API const FVertexName& GetOutputDataName();

		/** Input vertex name for variables */
		METASOUNDGRAPHCORE_API const FVertexName& GetInputVariableName();
		
		/** Output vertex name for variables */
		METASOUNDGRAPHCORE_API const FVertexName& GetOutputVariableName();

		/** Class name for variable node. */
		METASOUNDGRAPHCORE_API FNodeClassName GetVariableNodeClassName(const FName& InDataTypeName);

		/** Class name for variable mutator node. */
		METASOUNDGRAPHCORE_API FNodeClassName GetVariableMutatorNodeClassName(const FName& InDataTypeName);

		/** Class name for variable accessor node */
		METASOUNDGRAPHCORE_API FNodeClassName GetVariableAccessorNodeClassName(const FName& InDataTypeName);

		/** Class name for variable deferred accessor node. */
		METASOUNDGRAPHCORE_API FNodeClassName GetVariableDeferredAccessorNodeClassName(const FName& InDataTypeName);

		/** Class name for variable node. */
		template<typename DataType>
		const FNodeClassName& GetVariableNodeClassName()
		{
			static const FNodeClassName ClassName = GetVariableNodeClassName(GetMetasoundDataTypeName<DataType>());
			return ClassName;
		}

		/** Class name for variable mutator node. */
		template<typename DataType>
		const FNodeClassName& GetVariableMutatorNodeClassName()
		{
			static const FNodeClassName ClassName = GetVariableMutatorNodeClassName(GetMetasoundDataTypeName<DataType>());
			return ClassName;
		}

		/** Class name for variable accessor node */
		template<typename DataType>
		const FNodeClassName& GetVariableAccessorNodeClassName()
		{
			static const FNodeClassName ClassName = GetVariableAccessorNodeClassName(GetMetasoundDataTypeName<DataType>());
			return ClassName;
		}

		/** Class name for variable deferred accessor node. */
		template<typename DataType>
		const FNodeClassName& GetVariableDeferredAccessorNodeClassName()
		{
			static const FNodeClassName ClassName = GetVariableDeferredAccessorNodeClassName(GetMetasoundDataTypeName<DataType>());
			return ClassName;
		}

	}

	/** Variable nodes initialize variable values. The output of a VariableNode
	 * is a TVariable.  */
	template<typename DataType>
	class TVariableNode : public FNode
	{
		using FVariable = TVariable<DataType>;

		class FOperator : public TExecutableOperator<FOperator>
		{
			using Super = TExecutableOperator<FOperator>;

		public:
			FOperator(TDataWriteReference<FVariable> InVariable)
			: Variable(InVariable)
			, bCopyReferenceDataOnExecute(false)
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Collection;
				return Collection;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataWriteReference(VariableNames::GetOutputVariableName(), Variable);
				return Collection;
			}

			void Execute()
			{
				if (bCopyReferenceDataOnExecute)
				{
					Variable->CopyReferencedData();
				}
				else
				{
					// The first time a variable node is run, it should not copy
					// reference data in the variable, but instead use the original
					// initial value of the variable. 
					//
					// The TVariableNode is currently executed before the TVariableDeferredAccessor
					// nodes. This execution order is managed in the Metasound::Frontend::FGraphController.
					// Because the TVariableNode is executed before the TVariableDeferredAccessor node
					// it can undesireably override the "init" value of the variable with the "init" 
					// value of the data reference set in the TVariableMutatorNode. 
					// This would mean that the first call to execute on TVariableDeferredAccessor
					// would result in reading the "init" value of the data reference
					// set in the TVariableMutatorNode as opposed to the "init" value
					// of the data reference set in the TVariableNode. This boolean
					// protects against that situation so that on first call to execute, the 
					// TVariableDeferredAccessor always reads the "init" value provided
					// by the TVariableNode. 
					bCopyReferenceDataOnExecute = true;
				}
			}

		private:

			TDataWriteReference<FVariable> Variable;
			bool bCopyReferenceDataOnExecute;
		};

		class FFactory : public IOperatorFactory
		{
		public:
			FFactory(FLiteral&& InLiteral)
			: Literal(MoveTemp(InLiteral))
			{
			}

			virtual ~FFactory() = default;

			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
			{
				TDataWriteReference<DataType> Data = TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InParams.OperatorSettings, Literal);
				return MakeUnique<FOperator>(TDataWriteReference<FVariable>::CreateNew(Data));
			}
		private:
			FLiteral Literal;
		};


		static FVertexInterface DeclareVertexInterface()
		{
			return FVertexInterface(
				FInputVertexInterface(
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FVariable>(VariableNames::GetOutputVariableName(), FText::GetEmpty())
				)
			);
		}

		static FNodeClassMetadata GetNodeMetadata()
		{
			FNodeClassMetadata Info;

			Info.ClassName = VariableNames::GetVariableNodeClassName<DataType>();
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;

#if WITH_EDITOR
			Info.DisplayStyle.bShowName = false;
			Info.Description = LOCTEXT("Metasound_InitVariableNodeDescription", "Initialize a variable of a MetaSound graph.");
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };
#endif // WITH_EDITOR
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		}

	public:

		TVariableNode(const FVertexName& InNodeName, const FGuid& InInstanceID, FLiteral&& InLiteral)
		: FNode(InNodeName, InInstanceID, GetNodeMetadata())
		, Interface(DeclareVertexInterface())
		, Factory(MakeShared<FFactory>(MoveTemp(InLiteral)))
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

	/** TVariableMutatorNode allows variable values to be set.  */
	template<typename DataType>
	class TVariableMutatorNode : public FNodeFacade
	{
		using FVariable = TVariable<DataType>;

		class FOperator : public FNoOpOperator 
		{
		public:
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static FVertexInterface DeclareVertexInterface();
			static const FNodeClassMetadata& GetNodeInfo();

			FOperator(TDataReadReference<FVariable> InVariable)
			: Variable(InVariable)
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataReadReference<FVariable>(VariableNames::GetOutputVariableName(), Variable);
				return Collection;
			}

		private:
			TDataReadReference<FVariable> Variable;
		};

	public:
		TVariableMutatorNode(const FVertexName& InNodeName, const FGuid& InInstanceID)
		: FNodeFacade(InNodeName, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TVariableMutatorNode(const FNodeInitData& InInitData)
		: TVariableMutatorNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TVariableMutatorNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		if (Inputs.ContainsDataWriteReference<FVariable>(GetInputVariableName()))
		{
			// If a variable is provided, set the reference to read.
			TDataWriteReference<FVariable> Variable = Inputs.GetDataWriteReference<FVariable>(GetInputVariableName());
			if (Inputs.ContainsDataReadReference<DataType>(GetInputDataName()))
			{
				// Update the input variable with the data reference to copy from.
				TDataReadReference InputData = Inputs.GetDataReadReference<DataType>(GetInputDataName());
				Variable->SetDataReference(InputData);
			}

			return MakeUnique<FOperator>(Variable);
		}
		else if (Inputs.ContainsDataReadReference<DataType>(GetInputDataName()))
		{
			// If no input variable is provided create Variable with input variable
			TDataReadReference<DataType> InputData = Inputs.GetDataReadReference<DataType>(GetInputDataName());
			TDataWriteReference<DataType> InitData = TDataWriteReference<DataType>::CreateNew(*InputData);
			TDataWriteReference<FVariable> Variable = TDataWriteReference<FVariable>::CreateNew(InitData);
			Variable->SetDataReference(InputData);
			return MakeUnique<FOperator>(Variable);
		}
		else 
		{
			// Nothing to do if there's no input data.
			return MakeUnique<FNoOpOperator>();
		}
	}

	template<typename DataType>
	FVertexInterface TVariableMutatorNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<DataType>(VariableNames::GetInputDataName(), FText::GetEmpty()), 
				TInputDataVertexModel<FVariable>(VariableNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FVariable>(VariableNames::GetOutputVariableName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TVariableMutatorNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = VariableNames::GetVariableMutatorNodeClassName<DataType>();
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;

#if WITH_EDITOR
			Info.DisplayStyle.bShowName = false;
			Info.DisplayName = LOCTEXT("Metasound_VariableMutatorNodeDisplayName", "Set");
			Info.Description = LOCTEXT("Metasound_VariableMutatorNodeDescription", "Set variable on MetaSound graph.");
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };
#endif // WITH_EDITOR
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}

	/** TVariableDeferredAccessorNode provides access to the prior executions variable value.
	 * TVariableDeferredAccessorNodes must always be before TVariableMutatorNodes in the dependency
	 * order.
	 */
	template<typename DataType>
	class TVariableDeferredAccessorNode : public FNodeFacade
	{
		using FVariable = TVariable<DataType>;

		class FOperator : public FNoOpOperator 
		{
		public:
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static FVertexInterface DeclareVertexInterface();
			static const FNodeClassMetadata& GetNodeInfo();

			FOperator(TDataWriteReference<FVariable> InDelayedVariable)
			: DelayedVariable(InDelayedVariable)
			, DelayedData(DelayedVariable->GetDelayedDataReference())
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataWriteReference<FVariable>(VariableNames::GetOutputVariableName(), DelayedVariable);
				Collection.AddDataReadReference<DataType>(VariableNames::GetOutputDataName(), DelayedVariable->GetDelayedDataReference());
				return Collection;
			}

		private:
			TDataWriteReference<FVariable> DelayedVariable;
			TDataReadReference<DataType> DelayedData;
		};

	public:

		TVariableDeferredAccessorNode(const FVertexName& InNodeName, const FGuid& InInstanceID)
		: FNodeFacade(InNodeName, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TVariableDeferredAccessorNode(const FNodeInitData& InInitData)
		: TVariableDeferredAccessorNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TVariableDeferredAccessorNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		if (ensure(Inputs.ContainsDataWriteReference<FVariable>(GetInputVariableName())))
		{
			TDataWriteReference<FVariable> DelayedVariable = Inputs.GetDataWriteReference<FVariable>(GetInputVariableName());

			return MakeUnique<FOperator>(DelayedVariable);
		}
		else 
		{
			// Nothing to do if there's no input data.
			UE_LOG(LogMetaSound, Warning, TEXT("Missing internal variable connection. Failed to create valid \"GetDelayedVariable\" operator"));
			return MakeUnique<FNoOpOperator>();
		}
	}

	template<typename DataType>
	FVertexInterface TVariableDeferredAccessorNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FVariable>(VariableNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FVariable>(VariableNames::GetOutputVariableName(), FText::GetEmpty()),
				TOutputDataVertexModel<DataType>(VariableNames::GetOutputDataName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TVariableDeferredAccessorNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = VariableNames::GetVariableDeferredAccessorNodeClassName<DataType>();
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
#if WITH_EDITOR
			Info.DisplayStyle.bShowName = false;
			Info.DisplayName = LOCTEXT("Metasound_VariableDeferredAccessorNodeDisplayName", "Get Delayed");
			Info.Description = LOCTEXT("Metasound_VariableDeferredAccessorNodeDescription", "Get a delayed variable on MetaSound graph.");
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };
#endif // WITH_EDITOR
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}

	/** FGetVariable node provides delay free, cpu free access to a set variable. */
	template<typename DataType>
	class TVariableAccessorNode : public FNodeFacade
	{
		using FVariable = TVariable<DataType>;

		class FOperator : public FNoOpOperator 
		{
		public:
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static FVertexInterface DeclareVertexInterface();
			static const FNodeClassMetadata& GetNodeInfo();

			FOperator(TDataReadReference<FVariable> InVariable)
			: Variable(InVariable)
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataReadReference<DataType>(VariableNames::GetOutputDataName(), Variable->GetDataReference());
				Collection.AddDataReadReference<FVariable>(VariableNames::GetOutputVariableName(), Variable);
				return Collection;
			}

		private:
			TDataReadReference<FVariable> Variable;
		};
	public:

		TVariableAccessorNode(const FVertexName& InNodeName, const FGuid& InInstanceID)
		: FNodeFacade(InNodeName, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TVariableAccessorNode(const FNodeInitData& InInitData)
		: TVariableAccessorNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TVariableAccessorNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		// Update delayed variable.
		if (Inputs.ContainsDataReadReference<FVariable>(GetInputVariableName()))
		{
			TDataReadReference<FVariable> Variable = Inputs.GetDataReadReference<FVariable>(GetInputVariableName());
			return MakeUnique<FOperator>(Variable);
		}
		else if (Inputs.ContainsDataWriteReference<FVariable>(GetInputVariableName()))
		{
			TDataReadReference<FVariable> Variable = Inputs.GetDataWriteReference<FVariable>(GetInputVariableName());
			return MakeUnique<FOperator>(Variable);
		}
		else 
		{
			// Nothing to do if there's no input data.
			UE_LOG(LogMetaSound, Warning, TEXT("Missing internal variable connection. Failed to create valid \"GetVariable\" operator"));
			return MakeUnique<FNoOpOperator>();
		}
	}

	template<typename DataType>
	FVertexInterface TVariableAccessorNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FVariable>(VariableNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<DataType>(VariableNames::GetOutputDataName(), FText::GetEmpty()),
				TOutputDataVertexModel<FVariable>(VariableNames::GetOutputVariableName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TVariableAccessorNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = VariableNames::GetVariableAccessorNodeClassName<DataType>();
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
#if WITH_EDITOR
			Info.DisplayStyle.bShowName = false;
			Info.DisplayName = LOCTEXT("Metasound_VariableAccessorNodeDisplayName", "Get");
			Info.Description = LOCTEXT("Metasound_VariableAccessorNodeDescription", "Get variable on MetaSound graph.");
#endif // WITH_EDITOR

			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundGraphCore
