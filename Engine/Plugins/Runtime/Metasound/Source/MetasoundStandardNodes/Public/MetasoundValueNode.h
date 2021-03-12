// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace MetasoundValueNodePrivate
	{
		METASOUNDSTANDARDNODES_API FNodeClassMetadata CreateNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface);
	}

	namespace ValueVertexNames
	{
		METASOUNDSTANDARDNODES_API const FString& GetInitValueName();
		METASOUNDSTANDARDNODES_API const FString& GetSetValueName();
		METASOUNDSTANDARDNODES_API const FString& GetInputTriggerName();
		METASOUNDSTANDARDNODES_API const FString& GetOutputValueName();
	}

	template<typename ValueType>
	class TValueOperator : public TExecutableOperator<TValueOperator<ValueType>>
	{
	public:
		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ValueVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<ValueType>(GetInitValueName(), LOCTEXT("InitValue", "Value to init the output to.")),
					TInputDataVertexModel<ValueType>(GetSetValueName(), LOCTEXT("ValueInput", "Value to set the output to when triggered.")),
					TInputDataVertexModel<FTrigger>(GetInputTriggerName(), LOCTEXT("ValueTrigger", "Trigger to write the set value to the output."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<ValueType>(GetOutputValueName(), LOCTEXT("ValueOutput", "The output value cached in the node."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ValueType>();
				FName OperatorName = TEXT("Value");
				FText NodeDisplayName = FText::Format(LOCTEXT("ValueDisplayNamePattern", "Value ({0})"), FText::FromString(GetMetasoundDataTypeString<ValueType>()));
				FText NodeDescription = LOCTEXT("ValueDescription", "Allows setting a value to output on trigger.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return MetasoundValueNodePrivate::CreateNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ValueVertexNames;

			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

			FTriggerReadRef Trigger = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(GetInputTriggerName(), InParams.OperatorSettings);
			TDataReadReference<ValueType> InitValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<ValueType>(InputInterface, GetInitValueName());
			TDataReadReference<ValueType> SetValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<ValueType>(InputInterface, GetSetValueName());

			return MakeUnique<TValueOperator<ValueType>>(InParams.OperatorSettings, Trigger, InitValue, SetValue);
		}


		TValueOperator(const FOperatorSettings& InSettings, TDataReadReference<FTrigger> InTrigger, TDataReadReference<ValueType> InInitValue, TDataReadReference<ValueType> InSetValue)
			: Trigger(InTrigger)
			, InitValue(InInitValue)
			, SetValue(InSetValue)
			, OutputValue(TDataWriteReferenceFactory<ValueType>::CreateAny(InSettings))
		{
			*OutputValue = *InitValue;
		}

		virtual ~TValueOperator() = default;


		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ValueVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetInputTriggerName(), Trigger);
			Inputs.AddDataReadReference(GetInitValueName(), InitValue);
			Inputs.AddDataReadReference(GetSetValueName(), SetValue);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ValueVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputValueName(), OutputValue);

			return Outputs;
		}

		void Execute()
		{
			if (*Trigger)
			{
				*OutputValue = *SetValue;
			}
		}

	private:

		TDataReadReference<FTrigger> Trigger;
		TDataReadReference<ValueType> InitValue;
		TDataReadReference<ValueType> SetValue;
		TDataWriteReference<ValueType> OutputValue;
	};

	/** TValueNode
	 *
	 *  Generates a random float value when triggered.
	 */
	template<typename ValueType>
	class METASOUNDSTANDARDNODES_API TValueNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TValueNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TValueOperator<ValueType>>())
		{}

		virtual ~TValueNode() = default;
	};
} // namespace Metasound

