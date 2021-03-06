// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerAccumulatorNode.h"

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundVertex.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FTriggerAccumulatorNode)

	class FTriggerAccumulatorOperator : public TExecutableOperator<FTriggerAccumulatorOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			~FTriggerAccumulatorOperator() = default;
			
			FTriggerAccumulatorOperator(
				const FOperatorSettings& InSettings,
				const FTriggerReadRef& InInputTriggerToAccumulate,
				const FTriggerReadRef& InInputTriggerResetCount,
				const FInt32ReadRef& InInputTriggerAtCount);

			virtual FDataReferenceCollection GetInputs() const override;
			virtual FDataReferenceCollection GetOutputs() const override;
			
			void Execute();

		private:
			void Reset(int32 InCurretFrameIndex);
			void Accumulate(int32 CurrentFrameIndex);

			// Input. (triggers)
			FTriggerReadRef TriggerToAccumulate;
			FTriggerReadRef ResetTrigger;
			
			// Input (values)
			FInt32ReadRef TriggerCount;

			// Outputs (triggers)
			FTriggerWriteRef CountReached;

			// Output (values).
			FInt32WriteRef CurrentCount;

			static constexpr const TCHAR* AccumulatePinName = TEXT("In");
			static constexpr const TCHAR* TriggerCountPinName = TEXT("TriggerCount");
			static constexpr const TCHAR* ResetPinName = TEXT("Reset");
			static constexpr const TCHAR* CountReachedPinName = TEXT("Out");
			static constexpr const TCHAR* CurrentCountPinName = TEXT("Count");
	};

	FTriggerAccumulatorOperator::FTriggerAccumulatorOperator(
		const FOperatorSettings& InSettings,
		const FTriggerReadRef& InInputTriggerToAccumulate,
		const FTriggerReadRef& InInputTriggerResetCount,
		const FInt32ReadRef& InInputTriggerAtCount)
		: TriggerToAccumulate(InInputTriggerToAccumulate)
		, ResetTrigger(InInputTriggerResetCount)
		, TriggerCount(InInputTriggerAtCount)
		, CountReached(FTriggerWriteRef::CreateNew(InSettings))
		, CurrentCount(FInt32WriteRef::CreateNew(0))
	{}

	FDataReferenceCollection FTriggerAccumulatorOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(AccumulatePinName, FTriggerReadRef(TriggerToAccumulate));
		InputDataReferences.AddDataReadReference(ResetPinName,	FTriggerReadRef(ResetTrigger));
		InputDataReferences.AddDataReadReference(TriggerCountPinName, FInt32ReadRef(TriggerCount));
		return InputDataReferences;
	}

	FDataReferenceCollection FTriggerAccumulatorOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(CountReachedPinName, FTriggerWriteRef(CountReached));
		OutputDataReferences.AddDataReadReference(CurrentCountPinName, FInt32WriteRef(CurrentCount));
		return OutputDataReferences;
	}

	void FTriggerAccumulatorOperator::Reset(int32 InCurrentFrameIndex)
	{
		*CurrentCount = 0;
	}
	void FTriggerAccumulatorOperator::Accumulate(int32 InCurrentFrameIndex)
	{
		++(*CurrentCount);
		if (*CurrentCount >= *TriggerCount)
		{
			Reset(InCurrentFrameIndex);
			CountReached->TriggerFrame(InCurrentFrameIndex);
		}
	}

	void FTriggerAccumulatorOperator::Execute()
	{		
		// We need to advance our own output triggers.
		CountReached->AdvanceBlock();

		// Query the number of triggers in the current block.
		int32 NumResets = ResetTrigger->NumTriggeredInBlock();
		int32 NumAccumulates = TriggerToAccumulate->NumTriggeredInBlock();
		int32 MaxTriggers = FMath::Max(NumResets, NumAccumulates);

		// Indices into each trigger array
		int32 ResetIndex = 0;
		int32 AccumulateIndex = 0;
		
		// Move forwards through the trigger array and return next trigger frame index, or INDEX_NONE if there's none remaining.
		static const auto GetNextTriggerFrame = [](const FTriggerReadRef& InTrigger, int32& InOutIndex, const int32 InNumTriggers) 
			-> int32 { return InOutIndex < InNumTriggers ? (*InTrigger)[InOutIndex++] : INDEX_NONE; };
		
		// Convenience lambdas for getting the next Reset/Accumulate frame index from either the Trigger or Reset.
		auto GetNextResetFrame = [this, &ResetIndex, NumResets]() -> int32 { return GetNextTriggerFrame(ResetTrigger, ResetIndex, NumResets); };
		auto GetNextAccumulateFrame = [this, &AccumulateIndex, NumAccumulates]() -> int32 { return GetNextTriggerFrame(TriggerToAccumulate, AccumulateIndex, NumAccumulates); };

		int32 NextResetFrame = GetNextResetFrame();
		int32 NextAccumulateFrame = GetNextAccumulateFrame();

		// Walk through each trigger in order.
		for (int32 i = 0; i < MaxTriggers; ++i)
		{
			// If there's no Reset, we can just process all Accumulates in order.
			if (NextResetFrame == INDEX_NONE)
			{
				Accumulate(NextAccumulateFrame);
				NextAccumulateFrame = GetNextAccumulateFrame();
			}
			// ... likewise if there's no Accumulate, we can just do the resets.
			else if (NextAccumulateFrame == INDEX_NONE)
			{
				Reset(NextResetFrame);
				NextResetFrame = GetNextResetFrame();
			}
			// ... both resets and accumulates remaining.
			else
			{
				if (NextAccumulateFrame < NextResetFrame)
				{
					Accumulate(NextAccumulateFrame);
					NextAccumulateFrame = GetNextAccumulateFrame();
				}
				else if(NextResetFrame < NextAccumulateFrame)
				{
					Reset(NextResetFrame);
					NextResetFrame = GetNextResetFrame();
				}
				else // Both on same frame, make sure Reset happens first.
				{
					Reset(NextResetFrame);
					Accumulate(NextAccumulateFrame);
					NextAccumulateFrame = GetNextAccumulateFrame();
					NextResetFrame = GetNextResetFrame();
				}
			}
		}
	}

	FVertexInterface FTriggerAccumulatorOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTrigger>(AccumulatePinName, LOCTEXT("TriggerToAccumulateTooltip", "Trigger to accumulate")),
				TInputDataVertexModel<FTrigger>(ResetPinName, LOCTEXT("ResetAccumulatorTooltip", "Reset the accumulator")),
				TInputDataVertexModel<int32>(TriggerCountPinName, LOCTEXT("CountToTriggerAtTooltip", "Count to trigger at"), 1)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(CountReachedPinName, LOCTEXT("CountReachedTooltop", "Triggered when the accumulated count is reached")),
				TOutputDataVertexModel<int32>(CurrentCountPinName, LOCTEXT("CountTooltop", "Current accumulator count"))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FTriggerAccumulatorOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("TriggerAccumulator"), TEXT("")};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_TriggerAccumulatorNodeDisplayName", "Trigger Accumulator");
			Info.Description = LOCTEXT("Metasound_TriggerAccumulatorNodeDescription", "Accumulates Triggers until a Count is hit and then it Triggers");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::TriggerUtils);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FTriggerAccumulatorOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FTriggerAccumulatorNode& TaNode = static_cast<const FTriggerAccumulatorNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
				
		return MakeUnique<FTriggerAccumulatorOperator>(
			InParams.OperatorSettings, 
			InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(AccumulatePinName, InParams.OperatorSettings),
			InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(ResetPinName, InParams.OperatorSettings),
			InputCollection.GetDataReadReferenceOrConstruct<int32>(TriggerCountPinName, TaNode.GetDefaultTriggerAt() )
		);
	}

	// Node.
	FTriggerAccumulatorNode::FTriggerAccumulatorNode(const FString& InName, const FGuid& InInstanceID, int32 InDefaultTriggerAtCount)
		: FNodeFacade(InName, InInstanceID, TFacadeOperatorClass<FTriggerAccumulatorOperator>())
		, DefaultTriggerAtCount(InDefaultTriggerAtCount)
	{}

	FTriggerAccumulatorNode::FTriggerAccumulatorNode(const FNodeInitData& InitData)
		: FTriggerAccumulatorNode(InitData.InstanceName, InitData.InstanceID, 1)
	{}
}

#undef LOCTEXT_NAMESPACE
