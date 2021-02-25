// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRandomNode.h"

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "Math/RandomStream.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	static const TCHAR* InParamNameSeed = TEXT("Seed");
	static const TCHAR* InParamNameMin = TEXT("Min");
	static const TCHAR* InParamNameMax = TEXT("Max");
	static const TCHAR* InParamNameNextTrigger = TEXT("Next");
	static const TCHAR* InParamNameResetTrigger = TEXT("Reset");
	static const TCHAR* OutParamNameValue = TEXT("Value");

	//////////////////////////////////////////////////////////////////////////
	// Random Int

	class FRandomIntOperator : public TExecutableOperator<FRandomIntOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FRandomIntOperator(const FOperatorSettings& InSettings,
			const FInt32ReadRef& InSeed, 
			const FInt32ReadRef& InMin, 
			const FInt32ReadRef& InMax, 
			const FTriggerReadRef& InTriggerNext, 
			const FTriggerReadRef& InTriggerReset);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		void EvaluateSeedChanges();

		FInt32ReadRef Seed;
		FInt32ReadRef Min;
		FInt32ReadRef Max;
		FTriggerReadRef TriggerNext;
		FTriggerReadRef TriggerReset;

		FInt32WriteRef ValueOutput;

		FRandomStream RandomStream;

		// Used to track if the random node starts non-zero and then becomes 0 (i.e. we want a new random seed)
		bool bIsZeroSeeded = false;
		bool bIsRandomStreamInitialized = false;
	};

	FRandomIntOperator::FRandomIntOperator(const FOperatorSettings& InSettings,
		const FInt32ReadRef& InSeed,
		const FInt32ReadRef& InMin,
		const FInt32ReadRef& InMax,
		const FTriggerReadRef& InTriggerNext,
		const FTriggerReadRef& InTriggerReset)
		: Seed(InSeed)
		, Min(InMin)
		, Max(InMax)
		, TriggerNext(InTriggerNext)
		, TriggerReset(InTriggerReset)
		, ValueOutput(FInt32WriteRef::CreateNew(0))
		, bIsZeroSeeded(*Seed == 0)
		, bIsRandomStreamInitialized(false)
	{
		*ValueOutput = *Min;
	}

	FDataReferenceCollection FRandomIntOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(InParamNameSeed, FInt32ReadRef(Seed));
		InputDataReferences.AddDataReadReference(InParamNameMin, FInt32ReadRef(Min));
		InputDataReferences.AddDataReadReference(InParamNameMax, FInt32ReadRef(Max));
		InputDataReferences.AddDataReadReference(InParamNameNextTrigger, FTriggerReadRef(TriggerNext));
		InputDataReferences.AddDataReadReference(InParamNameResetTrigger, FTriggerReadRef(TriggerReset));

		return InputDataReferences;
	}

	FDataReferenceCollection FRandomIntOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(OutParamNameValue, FInt32ReadRef(ValueOutput));
		return OutputDataReferences;
	}

	void FRandomIntOperator::EvaluateSeedChanges()
	{
		// if we have a non-zero seed
		if (*Seed != 0)
		{
			// If we were previously zero-seeded OR our seed has changed
			if (bIsZeroSeeded || !bIsRandomStreamInitialized || *Seed != RandomStream.GetInitialSeed())
			{
				bIsRandomStreamInitialized = true;
				bIsZeroSeeded = false;
				RandomStream.Initialize(*Seed);
			}
		}
		// If we are zero-seeded now BUT were previously not, we need to randomize our seed
		else if (!bIsZeroSeeded || !bIsRandomStreamInitialized)
		{
			bIsRandomStreamInitialized = true;
			bIsZeroSeeded = true;
			RandomStream.Initialize(FPlatformTime::Cycles());
		}
	}

	void FRandomIntOperator::Execute()
	{
		if (TriggerReset->IsTriggeredInBlock())
		{
			EvaluateSeedChanges();
			RandomStream.Reset();
		}

		if (TriggerNext->IsTriggeredInBlock())
		{
			EvaluateSeedChanges();
			*ValueOutput = RandomStream.RandRange(*Min, *Max);
		}

// 		TriggerReset->ExecuteBlock(
// 			// OnPreTrigger
// 			[&](int32 StartFrame, int32 EndFrame)
// 			{
// 			},
// 			// OnTrigger
// 				[&](int32 StartFrame, int32 EndFrame)
// 			{
// 				EvaluateSeedChanges();
// 				RandomStream.Reset();
// 
// 				UE_LOG(LogTemp, Log, TEXT("RESET"));
// 			});
// 
// 		TriggerNext->ExecuteBlock(
// 			// OnPreTrigger
// 			[&](int32 StartFrame, int32 EndFrame)
// 			{
// 			},
// 			// OnTrigger
// 				[&](int32 StartFrame, int32 EndFrame)
// 			{
// 				EvaluateSeedChanges();
// 				*ValueOutput = RandomStream.RandRange(*Min, *Max);
// 
// 				UE_LOG(LogTemp, Log, TEXT("%d"), *ValueOutput);
// 			})
	}

	const FVertexInterface& FRandomIntOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTrigger>(InParamNameNextTrigger, LOCTEXT("NextTooltip", "Trigger to generate the next random integer.")),
				TInputDataVertexModel<FTrigger>(InParamNameResetTrigger, LOCTEXT("ResetTooltip", "Trigger to reset the random sequence with the supplied seed. Useful to get randomized repetition.")),
				TInputDataVertexModel<int32>(InParamNameSeed, LOCTEXT("SeedTooltip", "The seed value to use for the random node. Set to 0 to use a random seed."), 0),
				TInputDataVertexModel<int32>(InParamNameMin, LOCTEXT("MinTooltip", "Min random integer."), 0),
				TInputDataVertexModel<int32>(InParamNameMax, LOCTEXT("MaxTooltip", "Max random integer."), 100)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<int32>(OutParamNameValue, LOCTEXT("ValueTooltip", "The current randomly generated random integer value."))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FRandomIntOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Random"), TEXT("Int32") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_RandomIntDisplayName", "Random Int");
			Info.Description = LOCTEXT("Metasound_RandomIntNodeDescription", "Generates a seedable random integer in the given value range.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FRandomIntOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FRandomIntNode& RandomIntNode = static_cast<const FRandomIntNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FInt32ReadRef SeedValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<int32>(InputInterface, InParamNameSeed);
		FInt32ReadRef MinValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<int32>(InputInterface, InParamNameMin);
		FInt32ReadRef MaxValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<int32>(InputInterface, InParamNameMax);
		FTriggerReadRef TriggerNext = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(InParamNameNextTrigger, InParams.OperatorSettings);
		FTriggerReadRef TriggerReset = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(InParamNameResetTrigger, InParams.OperatorSettings);

		return MakeUnique<FRandomIntOperator>(InParams.OperatorSettings, SeedValue, MinValue, MaxValue, TriggerNext, TriggerReset);
	}

	FRandomIntNode::FRandomIntNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FRandomIntOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FRandomIntNode)

	//////////////////////////////////////////////////////////////////////////
	// Random Float

	class FRandomFloatOperator : public TExecutableOperator<FRandomFloatOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FRandomFloatOperator(const FOperatorSettings& InSettings,
			const FInt32ReadRef& InSeed,
			const FFloatReadRef& InMin,
			const FFloatReadRef& InMax,
			const FTriggerReadRef& InTriggerNext,
			const FTriggerReadRef& InTriggerReset);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		void EvaluateSeedChanges();

		FInt32ReadRef Seed;
		FFloatReadRef Min;
		FFloatReadRef Max;
		FTriggerReadRef TriggerNext;
		FTriggerReadRef TriggerReset;

		FFloatWriteRef ValueOutput;

		FRandomStream RandomStream;

		// Used to track if the random node starts non-zero and then becomes 0 (i.e. we want a new random seed)
		bool bIsZeroSeeded = false;
		bool bIsRandomStreamInitialized = false;
	};

	FRandomFloatOperator::FRandomFloatOperator(const FOperatorSettings& InSettings,
		const FInt32ReadRef& InSeed,
		const FFloatReadRef& InMin,
		const FFloatReadRef& InMax,
		const FTriggerReadRef& InTriggerNext,
		const FTriggerReadRef& InTriggerReset)
		: Seed(InSeed)
		, Min(InMin)
		, Max(InMax)
		, TriggerNext(InTriggerNext)
		, TriggerReset(InTriggerReset)
		, ValueOutput(FFloatWriteRef::CreateNew(0.0f))
		, bIsZeroSeeded(*Seed == 0)
		, bIsRandomStreamInitialized(false)
	{
		*ValueOutput = *Min;
	}

	FDataReferenceCollection FRandomFloatOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(InParamNameSeed, FInt32ReadRef(Seed));
		InputDataReferences.AddDataReadReference(InParamNameMin, FFloatReadRef(Min));
		InputDataReferences.AddDataReadReference(InParamNameMax, FFloatReadRef(Max));
		InputDataReferences.AddDataReadReference(InParamNameNextTrigger, FTriggerReadRef(TriggerNext));
		InputDataReferences.AddDataReadReference(InParamNameResetTrigger, FTriggerReadRef(TriggerReset));

		return InputDataReferences;
	}

	FDataReferenceCollection FRandomFloatOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(OutParamNameValue, FFloatReadRef(ValueOutput));
		return OutputDataReferences;
	}

	void FRandomFloatOperator::EvaluateSeedChanges()
	{
		// if we have a non-zero seed
		if (*Seed != 0)
		{
			// If we were previously zero-seeded OR our seed has changed
			if (bIsZeroSeeded || !bIsRandomStreamInitialized || *Seed != RandomStream.GetInitialSeed())
			{
				bIsRandomStreamInitialized = true;
				bIsZeroSeeded = false;
				RandomStream.Initialize(*Seed);
			}
		}
		// If we are zero-seeded now BUT were previously not, we need to randomize our seed
		else if (!bIsZeroSeeded || !bIsRandomStreamInitialized)
		{
			bIsRandomStreamInitialized = true;
			bIsZeroSeeded = true;
			RandomStream.Initialize(FPlatformTime::Cycles());
		}
	}

	void FRandomFloatOperator::Execute()
	{
		if (TriggerReset->IsTriggeredInBlock())
		{
			EvaluateSeedChanges();
			RandomStream.Reset();
		}

		if (TriggerNext->IsTriggeredInBlock())
		{
			EvaluateSeedChanges();
			*ValueOutput = RandomStream.FRandRange(*Min, *Max);
		}
	}

	const FVertexInterface& FRandomFloatOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTrigger>(InParamNameNextTrigger, LOCTEXT("NextTooltip", "Trigger to generate the next random float.")),
				TInputDataVertexModel<FTrigger>(InParamNameResetTrigger, LOCTEXT("ResetTooltip", "Trigger to reset the random sequence with the supplied seed. Useful to get randomized repetition.")),
				TInputDataVertexModel<int32>(InParamNameSeed, LOCTEXT("SeedTooltip", "The seed value to use for the random node. Set to 0 to use a random seed."), 0),
				TInputDataVertexModel<float>(InParamNameMin, LOCTEXT("MinTooltip", "Min random float."), 0.0f),
				TInputDataVertexModel<float>(InParamNameMax, LOCTEXT("MaxTooltip", "Max random float."), 1.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<float>(OutParamNameValue, LOCTEXT("ValueTooltip", "The current randomly generated float value."))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FRandomFloatOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Random"), TEXT("Float") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_RandomFloatDisplayName", "Random Float");
			Info.Description = LOCTEXT("Metasound_RandomFloatNodeDescription", "Generates a seedable random float in the given value range.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FRandomFloatOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FRandomFloatNode& RandomIntNode = static_cast<const FRandomFloatNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FInt32ReadRef SeedValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<int32>(InputInterface, InParamNameSeed);
		FFloatReadRef MinValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, InParamNameMin);
		FFloatReadRef MaxValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, InParamNameMax);
		FTriggerReadRef TriggerNext = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(InParamNameNextTrigger, InParams.OperatorSettings);
		FTriggerReadRef TriggerReset = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(InParamNameResetTrigger, InParams.OperatorSettings);

		return MakeUnique<FRandomFloatOperator>(InParams.OperatorSettings, SeedValue, MinValue, MaxValue, TriggerNext, TriggerReset);
	}

	FRandomFloatNode::FRandomFloatNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FRandomFloatOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FRandomFloatNode)
}

#undef LOCTEXT_NAMESPACE
