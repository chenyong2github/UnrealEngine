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
	namespace MetasoundTriggerAccumulatorNodePrivate
	{
		METASOUNDSTANDARDNODES_API FNodeClassMetadata CreateNodeClassMetadata(const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface);
	}

	namespace TriggerAccumulatorVertexNames
	{
		METASOUNDSTANDARDNODES_API const FString GetInputAutoResetName();
		METASOUNDSTANDARDNODES_API const FText GetInputAutoResetDescription();

		METASOUNDSTANDARDNODES_API const FString GetInputTriggerName(uint32 InIndex);
		METASOUNDSTANDARDNODES_API const FText GetInputTriggerDescription(uint32 InIndex);

		METASOUNDSTANDARDNODES_API const FString& GetOutputTriggerName();
		METASOUNDSTANDARDNODES_API const FText& GetOutputTriggerDescription();
	}

	template<uint32 NumInputs>
	class TTriggerAccumulatorOperator : public TExecutableOperator<TTriggerAccumulatorOperator<NumInputs>>
	{
	public:
		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace TriggerAccumulatorVertexNames;

			auto CreateDefaultInterface = []() -> FVertexInterface
			{
				FInputVertexInterface InputInterface;

				InputInterface.Add(TInputDataVertexModel<bool>(GetInputAutoResetName(), GetInputAutoResetDescription()));

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
				FName OperatorName = *FString::Printf(TEXT("Trigger Accumulate (%d)") , NumInputs);
				FText NodeDisplayName = FText::Format(LOCTEXT("TriggerAccumulateDisplayNamePattern", "Trigger Accumulate ({0})"), NumInputs);
				FText NodeDescription = LOCTEXT("TriggerAccumulateDescription", "Will trigger output once all input triggers have been hit at some point in the past.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return MetasoundTriggerAccumulatorNodePrivate::CreateNodeClassMetadata(OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace TriggerAccumulatorVertexNames;

			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

			FBoolReadRef bInAutoReset = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, GetInputAutoResetName());
			TArray<FTriggerReadRef> InputTriggers;

			for (uint32 i = 0; i < NumInputs; ++i)
			{
				InputTriggers.Add(InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(GetInputTriggerName(i), InParams.OperatorSettings));
			}

			return MakeUnique<TTriggerAccumulatorOperator<NumInputs>>(InParams.OperatorSettings, bInAutoReset, MoveTemp(InputTriggers));
		}

		TTriggerAccumulatorOperator(const FOperatorSettings& InSettings, 
			const FBoolReadRef& bInAutoReset, 
			const TArray<FTriggerReadRef>&& InInputTriggers)
			: bAutoReset(bInAutoReset)
			, InputTriggers(InInputTriggers)
			, OutputTrigger(FTriggerWriteRef::CreateNew(InSettings))
		{
			ResetTriggerState();
		}

		virtual ~TTriggerAccumulatorOperator() = default;


		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace TriggerAccumulatorVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetInputAutoResetName(), bAutoReset);
			for (uint32 i = 0; i < NumInputs; ++i)
			{
				Inputs.AddDataReadReference(GetInputTriggerName(i), InputTriggers[i]);
			}

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace TriggerAccumulatorVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputTriggerName(), OutputTrigger);

			return Outputs;
		}

		void TriggerOutputIfReady(int32 InStartFrame)
		{
			for (bool WasTriggered : bInputWasTriggered)
			{
				if (!WasTriggered)
				{
					return;
				}
			}

			if (*bAutoReset)
			{
				ResetTriggerState();
			}

			OutputTrigger->TriggerFrame(InStartFrame);
		}

		void ResetTriggerState()
		{
			bInputWasTriggered.Reset();
			bInputWasTriggered.AddDefaulted(InputTriggers.Num());
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
						bInputWasTriggered[i] = true;
						TriggerOutputIfReady(StartFrame);
					}
				);
			}
		}

	private:

		FBoolReadRef bAutoReset;
		TArray<FTriggerReadRef> InputTriggers;
		FTriggerWriteRef OutputTrigger;

		TArray<bool> bInputWasTriggered;
	};

	/** TTriggerAccumulatorNode
	*
	*  Routes values from multiple input pins to a single output pin based on trigger inputs.
	*/
	template<uint32 NumInputs>
	class METASOUNDSTANDARDNODES_API TTriggerAccumulatorNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TTriggerAccumulatorNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TTriggerAccumulatorOperator<NumInputs>>())
		{}

		virtual ~TTriggerAccumulatorNode() = default;
	};

} // namespace Metasound
