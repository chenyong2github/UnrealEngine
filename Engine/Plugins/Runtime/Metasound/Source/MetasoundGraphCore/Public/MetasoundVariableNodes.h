// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundLog.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundVariable.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"

namespace Metasound
{
	namespace VariableNodeVertexNames
	{
		/** Input vertex name for data */
		METASOUNDGRAPHCORE_API const FString& GetInputDataName();

		/** Output vertex name for data */
		METASOUNDGRAPHCORE_API const FString& GetOutputDataName();

		/** Input vertex name for variables */
		METASOUNDGRAPHCORE_API const FString& GetInputVariableName();
		
		/** Output vertex name for variables */
		METASOUNDGRAPHCORE_API const FString& GetOutputVariableName();
	}

	/** Init Variable nodes initialize variable values. The output of a InitVariableNode
	 * is a TDelayedVariable. This provides access to a variable's value before it is set
	 * by using the TGetDelayedVariableNode.
	 */
	template<typename DataType>
	class TInitVariableNode : public FNode
	{
		using FDelayedVariable = TDelayedVariable<DataType>;

		class FOperator : public TExecutableOperator<FOperator>
		{
			using Super = TExecutableOperator<FOperator>;

		public:
			FOperator(TDataWriteReference<FDelayedVariable> InVariable)
			: Variable(InVariable)
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
				Collection.AddDataWriteReference(VariableNodeVertexNames::GetOutputVariableName(), Variable);
				return Collection;
			}

			void Execute()
			{
				Variable->CopyReferencedData();
			}

		private:

			TDataWriteReference<FDelayedVariable> Variable;
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
				return MakeUnique<FOperator>(TDataWriteReference<FDelayedVariable>::CreateNew(Data));
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
					TOutputDataVertexModel<FDelayedVariable>(VariableNodeVertexNames::GetOutputVariableName(), FText::GetEmpty())
				)
			);
		}

		static FNodeClassMetadata GetNodeMetadata()
		{
			FNodeClassMetadata Info;

			Info.ClassName = {"InitVariable", GetMetasoundDataTypeName<DataType>(), ""};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_InitVariableNodeDisplayNameFormat", "Init {0} Variable"), FText::FromName(GetMetasoundDataTypeName<DataType>()));
			Info.Description = LOCTEXT("Metasound_InitVariableNodeDescription", "Initialize a variable of a MetaSound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };

			return Info;
		}

	public:

		TInitVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID, FLiteral&& InLiteral)
		: FNode(InNodeDescription, InInstanceID, GetNodeMetadata())
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

	/** TSetVariableNode allows variable values to be set. It converts TDelayedVariables
	 * into TVariables and assigns TDelayedVariable references.
	 */
	template<typename DataType>
	class TSetVariableNode : public FNodeFacade
	{
		using FVariable = TVariable<DataType>;
		using FDelayedVariable = TDelayedVariable<DataType>;

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
				Collection.AddDataReadReference<FVariable>(VariableNodeVertexNames::GetOutputVariableName(), Variable);
				return Collection;
			}

		private:
			TDataReadReference<FVariable> Variable;
		};

	public:
		TSetVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID)
		: FNodeFacade(InNodeDescription, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TSetVariableNode(const FNodeInitData& InInitData)
		: TSetVariableNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TSetVariableNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNodeVertexNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		// Update delayed variable.
		if (Inputs.ContainsDataWriteReference<FDelayedVariable>(GetInputVariableName()))
		{
			TDataWriteReference<FDelayedVariable> Delayed = Inputs.GetDataWriteReference<FDelayedVariable>(GetInputVariableName());
			if (Inputs.ContainsDataReadReference<DataType>(GetInputDataName()))
			{
				// Update the delayed variable with the data reference to copy from.
				TDataReadReference InputData = Inputs.GetDataReadReference<DataType>(GetInputDataName());
				Delayed->SetDataReference(InputData);
			}

			// Create variable with matching reference to delayed variable.
			TDataReadReference<FVariable> Variable = TDataReadReference<FVariable>::CreateNew(Delayed->GetDataReference());
			return MakeUnique<FOperator>(Variable);
		}
		else if (Inputs.ContainsDataReadReference<DataType>(GetInputDataName()))
		{
			// Create Variable without DelayedVariable
			TDataReadReference InputData = Inputs.GetDataReadReference<DataType>(GetInputDataName());
			TDataReadReference<FVariable> Variable = TDataReadReference<FVariable>::CreateNew(InputData);
			return MakeUnique<FOperator>(Variable);
		}
		else 
		{
			// Nothing to do if there's no input data.
			return MakeUnique<FNoOpOperator>();
		}
	}

	template<typename DataType>
	FVertexInterface TSetVariableNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<DataType>(VariableNodeVertexNames::GetInputDataName(), FText::GetEmpty()), 
				TInputDataVertexModel<FDelayedVariable>(VariableNodeVertexNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FVariable>(VariableNodeVertexNames::GetOutputVariableName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TSetVariableNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = {"SetVariable", GetMetasoundDataTypeName<DataType>(), ""};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_SetVariableNodeDisplayNameFormat", "Set {0}"), GetMetasoundDataTypeDisplayText<DataType>());
			Info.Description = LOCTEXT("Metasound_SetVariableNodeDescription", "Set variable on MetaSound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };

			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}

	/** TGetDelayedVariableNode provides access to the prior executions variable value.
	 * TGetDelayedVariableNodes must always be before TSetVariableNodes in the dependency
	 * order.
	 */
	template<typename DataType>
	class TGetDelayedVariableNode : public FNodeFacade
	{
		using FDelayedVariable = TDelayedVariable<DataType>;

		class FOperator : public FNoOpOperator 
		{
		public:
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static FVertexInterface DeclareVertexInterface();
			static const FNodeClassMetadata& GetNodeInfo();

			FOperator(TDataWriteReference<FDelayedVariable> InDelayedVariable)
			: DelayedVariable(InDelayedVariable)
			, DelayedData(DelayedVariable->GetDelayedDataReference())
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataWriteReference<FDelayedVariable>(VariableNodeVertexNames::GetOutputVariableName(), DelayedVariable);
				Collection.AddDataReadReference<DataType>(VariableNodeVertexNames::GetOutputDataName(), DelayedVariable->GetDelayedDataReference());
				return Collection;
			}

		private:
			TDataWriteReference<FDelayedVariable> DelayedVariable;
			TDataReadReference<DataType> DelayedData;
		};

	public:

		TGetDelayedVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID)
		: FNodeFacade(InNodeDescription, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TGetDelayedVariableNode(const FNodeInitData& InInitData)
		: TGetDelayedVariableNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TGetDelayedVariableNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNodeVertexNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		if (ensure(Inputs.ContainsDataWriteReference<FDelayedVariable>(GetInputVariableName())))
		{
			TDataWriteReference<FDelayedVariable> DelayedVariable = Inputs.GetDataWriteReference<FDelayedVariable>(GetInputVariableName());

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
	FVertexInterface TGetDelayedVariableNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FDelayedVariable>(VariableNodeVertexNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FDelayedVariable>(VariableNodeVertexNames::GetOutputVariableName(), FText::GetEmpty()),
				TOutputDataVertexModel<DataType>(VariableNodeVertexNames::GetOutputDataName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TGetDelayedVariableNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = {"GetDelayedVariable", GetMetasoundDataTypeName<DataType>(), ""};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_GetDelayedVariableNodeDisplayNameFormat", "Get Delayed {0}"), GetMetasoundDataTypeDisplayText<DataType>());
			Info.Description = LOCTEXT("Metasound_GetDelayedVariableNodeDescription", "Get a delayed variable on MetaSound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };

			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}

	/** FGetVariable node provides delay free, cpu free access to a set variable. */
	template<typename DataType>
	class TGetVariableNode : public FNodeFacade
	{
		using FVariable = TVariable<DataType>;

		class FOperator : public FNoOpOperator 
		{
		public:
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static FVertexInterface DeclareVertexInterface();
			static const FNodeClassMetadata& GetNodeInfo();

			FOperator(TDataReadReference<DataType> InData)
			: Data(InData)
			{
			}

			virtual ~FOperator() = default;

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Collection;
				Collection.AddDataReadReference<DataType>(VariableNodeVertexNames::GetOutputDataName(), Data);
				return Collection;
			}

		private:
			TDataReadReference<DataType> Data;
		};
	public:

		TGetVariableNode(const FString& InNodeDescription, const FGuid& InInstanceID)
		: FNodeFacade(InNodeDescription, InInstanceID, TFacadeOperatorClass<FOperator>())
		{
		}

		TGetVariableNode(const FNodeInitData& InInitData)
		: TGetVariableNode(InInitData.InstanceName, InInitData.InstanceID)
		{}
	};

	template<typename DataType>
	TUniquePtr<IOperator> TGetVariableNode<DataType>::FOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		using namespace VariableNodeVertexNames;

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		// Update delayed variable.
		if (Inputs.ContainsDataReadReference<FVariable>(GetInputVariableName()))
		{
			TDataReadReference<FVariable> Variable = Inputs.GetDataReadReference<FVariable>(GetInputVariableName());
			return MakeUnique<FOperator>(Variable->GetDataReference());
		}
		else 
		{
			// Nothing to do if there's no input data.
			UE_LOG(LogMetaSound, Warning, TEXT("Missing internal variable connection. Failed to create valid \"GetVariable\" operator"));
			return MakeUnique<FNoOpOperator>();
		}
	}

	template<typename DataType>
	FVertexInterface TGetVariableNode<DataType>::FOperator::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FVariable>(VariableNodeVertexNames::GetInputVariableName(), FText::GetEmpty())
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<DataType>(VariableNodeVertexNames::GetOutputDataName(), FText::GetEmpty())
			)
		);
	}

	template<typename DataType>
	const FNodeClassMetadata& TGetVariableNode<DataType>::FOperator::GetNodeInfo()
	{
		auto CreateNodeClassMetadata = []()
		{
			FNodeClassMetadata Info;

			Info.ClassName = {"GetVariable", GetMetasoundDataTypeName<DataType>(), ""};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_GetVariableNodeDisplayNameFormat", "Get {0}"), GetMetasoundDataTypeDisplayText<DataType>());
			Info.Description = LOCTEXT("Metasound_GetVariableNodeDescription", "Get variable on MetaSound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			Info.CategoryHierarchy = { LOCTEXT("Metasound_VariableCategory", "Variable") };
			return Info;
		};

		static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
		return Metadata;
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundGraphCore
