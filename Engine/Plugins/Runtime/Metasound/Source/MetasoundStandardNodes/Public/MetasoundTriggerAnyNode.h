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
	namespace MetasoundTriggerAnyNodePrivate
	{
		METASOUNDSTANDARDNODES_API FNodeClassMetadata CreateNodeClassMetadata(const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface);
	}

	namespace TriggerAnyVertexNames
	{
		METASOUNDSTANDARDNODES_API const FString GetInputTriggerName(uint32 InIndex);
		METASOUNDSTANDARDNODES_API const FText GetInputTriggerDescription(uint32 InIndex);

		METASOUNDSTANDARDNODES_API const FString& GetOutputTriggerName();
		METASOUNDSTANDARDNODES_API const FText& GetOutputTriggerDescription();
	}

	template<uint32 NumInputs>
	class TTriggerAnyOperator : public TExecutableOperator<TTriggerAnyOperator<NumInputs>>
	{
	public:
		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace TriggerAnyVertexNames;

			auto CreateDefaultInterface = []() -> FVertexInterface
			{
				FInputVertexInterface InputInterface;

				for (uint32 i = 0; i < NumInputs; ++i)
				{
					InputInterface.Add(TInputDataVertexModel<FTrigger>(GetInputTriggerName(i), GetInputTriggerDescription(i)));
				}

				FOutputVertexInterface OutputInterface;
				OutputInterface.Add(TOutputDataVertexModel<FTrigger>(GetOutputTriggerName(), GetOutputTriggerDescription()));

				return FVertexInterface(InputInterface, OutputInterface);
			};

			static const FVertexInterface DefaultInterface = CreateDefaultInterface();
			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName OperatorName = *FString::Printf(TEXT("Trigger Any (%d)") , NumInputs);
				FText NodeDisplayName = FText::Format(LOCTEXT("TriggerAnyDisplayNamePattern", "Trigger Any ({0})"), NumInputs);
				FText NodeDescription = LOCTEXT("TriggerAnyDescription", "Will trigger output on any of the input triggers.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return MetasoundTriggerAnyNodePrivate::CreateNodeClassMetadata(OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace TriggerAnyVertexNames;

			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

			TArray<FTriggerReadRef> InputTriggers;

			for (uint32 i = 0; i < NumInputs; ++i)
			{
				InputTriggers.Add(InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(GetInputTriggerName(i), InParams.OperatorSettings));
			}

			return MakeUnique<TTriggerAnyOperator<NumInputs>>(InParams.OperatorSettings, MoveTemp(InputTriggers));
		}

		TTriggerAnyOperator(const FOperatorSettings& InSettings, const TArray<FTriggerReadRef>&& InInputTriggers)
			: InputTriggers(InInputTriggers)
			, OutputTrigger(FTriggerWriteRef::CreateNew(InSettings))
		{
		}

		virtual ~TTriggerAnyOperator() = default;


		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace TriggerAnyVertexNames;

			FDataReferenceCollection Inputs;
			for (uint32 i = 0; i < NumInputs; ++i)
			{
				Inputs.AddDataReadReference(GetInputTriggerName(i), InputTriggers[i]);
			}

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace TriggerAnyVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputTriggerName(), OutputTrigger);

			return Outputs;
		}

		void Execute()
		{
			OutputTrigger->AdvanceBlock();

			for (uint32 i = 0; i < NumInputs; ++i)
			{
				InputTriggers[i]->ExecuteBlock(
					[&](int32 StartFrame, int32 EndFrame)
					{
					},
					[this, i](int32 StartFrame, int32 EndFrame)
					{
						OutputTrigger->TriggerFrame(StartFrame);
					}
				);
			}
		}

	private:

		TArray<FTriggerReadRef> InputTriggers;
		FTriggerWriteRef OutputTrigger;
	};

	/** TTriggerAnyNode
	*
	*  Will output a trigger whenever any of its input triggers are set.
	*/
	template<uint32 NumInputs>
	class METASOUNDSTANDARDNODES_API TTriggerAnyNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TTriggerAnyNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TTriggerAnyOperator<NumInputs>>())
		{}

		virtual ~TTriggerAnyNode() = default;
	};

} // namespace Metasound
