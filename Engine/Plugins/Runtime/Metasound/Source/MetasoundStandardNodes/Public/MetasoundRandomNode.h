// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "Internationalization/Text.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	template<typename ValueType>
	struct TRandomNodeSpecialization
	{
		bool bSupported = false;
	};

	template<>
	struct TRandomNodeSpecialization<int32>
	{
		static FName GetClassName()
		{
			return FName("RandomInt32");
		}

		static FText GetDisplayName()
		{
			return LOCTEXT("Int32RandomValueDisplayName", "Random Int");
		}

		static FText GetDescription()
		{
			return LOCTEXT("Int32RandomDescription", "Generates a seedable random integer in the given value range.");
		}

		static bool HasRange()
		{
			return true;
		}

		static void GetDefaultRange(int32& OutMin, int32& OutMax)
		{
			OutMin = 0;
			OutMax = 100;
		}

		static int32 GetNextValue(FRandomStream& InStream, int32 InMin, int32 InMax)
		{
			return InStream.RandRange(InMin, InMax);
		}
	};

	template<>
	struct TRandomNodeSpecialization<float>
	{
		static FName GetClassName()
		{
			return FName("RandomFloat");
		}

		static FText GetDisplayName()
		{
			return LOCTEXT("FloatRandomValueDisplayName", "Random Float");
		}

		static FText GetDescription()
		{
			return LOCTEXT("FloatRandomDescription", "Generates a seedable random float in the given value range.");
		}

		static bool HasRange()
		{
			return true;
		}

		static void GetDefaultRange(float& OutMin, float& OutMax)
		{
			OutMin = 0.0f;
			OutMax = 1.0f;
		}

		static float GetNextValue(FRandomStream& InStream, float InMin, float InMax)
		{
			return InStream.FRandRange(InMin, InMax);
		}
	};

	template<>
	struct TRandomNodeSpecialization<bool>
	{
		static FName GetClassName()
		{
			return FName("RandomBool");
		}

		static FText GetDisplayName()
		{
			return LOCTEXT("BoolRandomValueDisplayName", "Random Bool");
		}

		static FText GetDescription()
		{
			return LOCTEXT("BoolRandomDiscription", "Generates a random bool value.");
		}

		static bool HasRange()
		{
			return false;
		}

		static void GetDefaultRange(bool& OutMin, bool& OutMax)
		{
		}

		static bool GetNextValue(FRandomStream& InStream, bool, bool)
		{
			return (bool)InStream.RandRange(0, 1);
		}
	};

	namespace RandomNodeNames
	{
		/** Input vertex names. */
		const FString& GetInputNextTriggerName();
		const FString& GetInputResetTriggerName();
		const FString& GetInputSeedName();
		const FString& GetInputMinName();
		const FString& GetInputMaxName();

		/** Output vertex names. */
		const FString& GetOutputOnNextTriggerName();
		const FString& GetOutputOnResetTriggerName();
		const FString& GetOutputValueName();

		/** Descriptions. */
		static FText GetNextTriggerDescription();
		static FText GetResetDescription();
		static FText GetSeedDescription();
		static FText GetMinDescription();
		static FText GetMaxDescription();
		static FText GetOutputDescription();
		static FText GetOutputOnNextDescription();
		static FText GetOutputOnResetDescription();
	}
	
	template<typename ValueType>
	class TRandomNodeOperator : public TExecutableOperator<TRandomNodeOperator<ValueType>>
	{
	public:
		static constexpr int32 DefaultSeed = -1;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace RandomNodeNames;

			if (TRandomNodeSpecialization<ValueType>::HasRange())
			{
				ValueType MinValue;
				ValueType MaxValue;
				TRandomNodeSpecialization<ValueType>::GetDefaultRange(MinValue, MaxValue);

				static const FVertexInterface DefaultInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FTrigger>(GetInputNextTriggerName(), GetNextTriggerDescription()),
						TInputDataVertexModel<FTrigger>(GetInputResetTriggerName(), GetResetDescription()),
						TInputDataVertexModel<int32>(GetInputSeedName(), GetSeedDescription(), DefaultSeed),
						TInputDataVertexModel<ValueType>(GetInputMinName(), GetMinDescription(), MinValue),
						TInputDataVertexModel<ValueType>(GetInputMaxName(), GetMaxDescription(), MaxValue)
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FTrigger>(GetOutputOnNextTriggerName(), GetOutputOnNextDescription()),
						TOutputDataVertexModel<FTrigger>(GetOutputOnResetTriggerName(), GetOutputOnResetDescription()),
						TOutputDataVertexModel<ValueType>(GetOutputValueName(), GetOutputDescription())
					)
				);

				return DefaultInterface;
			}
			else
			{
				static const FVertexInterface DefaultInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FTrigger>(GetInputNextTriggerName(), GetNextTriggerDescription()),
						TInputDataVertexModel<FTrigger>(GetInputResetTriggerName(), GetResetDescription()),
						TInputDataVertexModel<int32>(GetInputSeedName(), GetSeedDescription(), DefaultSeed)
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FTrigger>(GetOutputOnNextTriggerName(), GetOutputOnNextDescription()),
						TOutputDataVertexModel<FTrigger>(GetOutputOnResetTriggerName(), GetOutputOnResetDescription()),
						TOutputDataVertexModel<ValueType>(GetOutputValueName(), GetOutputDescription())
					)
				);

				return DefaultInterface;
			}
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TRandomNodeSpecialization<ValueType>::GetClassName(), TEXT("") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = TRandomNodeSpecialization<ValueType>::GetDisplayName();
				Info.Description = LOCTEXT("RandomOpDescription", "Generates a Random value.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetDefaultInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::RandomUtils);

				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
 			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
 			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
			using namespace RandomNodeNames;

 			FTriggerReadRef InNextTrigger = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(GetInputNextTriggerName(), InParams.OperatorSettings);
			FTriggerReadRef InResetTrigger = InputCollection.GetDataReadReferenceOrConstruct<FTrigger>(GetInputResetTriggerName(), InParams.OperatorSettings);
			FInt32ReadRef InSeedValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<int32>(InputInterface, GetInputSeedName());

			// note: this is ignored by random bool
			TDataReadReference<ValueType> InMinValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<ValueType>(InputInterface, GetInputMinName());
			TDataReadReference<ValueType> InMaxValue = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<ValueType>(InputInterface, GetInputMaxName());

			return MakeUnique<TRandomNodeOperator<ValueType>>(InParams.OperatorSettings, InNextTrigger, InResetTrigger, InSeedValue, InMinValue, InMaxValue);
		}


		TRandomNodeOperator(const FOperatorSettings& InSettings, 
			TDataReadReference<FTrigger> InNextTrigger, 
			TDataReadReference<FTrigger> InResetTrigger, 
			TDataReadReference<int32> InSeedValue, 
			TDataReadReference<ValueType> InMinValue,
			TDataReadReference<ValueType> InMaxValue)
 			: NextTrigger(InNextTrigger)
			, ResetTrigger(InResetTrigger)
			, SeedValue(InSeedValue)
 			, MinValue(InMinValue)
 			, MaxValue(InMaxValue)
			, TriggerOutOnNext(FTriggerWriteRef::CreateNew(InSettings))
			, TriggerOutOnReset(FTriggerWriteRef::CreateNew(InSettings))
 			, OutputValue(TDataWriteReferenceFactory<ValueType>::CreateAny(InSettings))
			, bIsDefaultSeeded(*SeedValue == DefaultSeed)
			, bIsRandomStreamInitialized(false)
		{
			// We need to initialize the output value to *something*
			*OutputValue = *MinValue;

			EvaluateSeedChanges();
			RandomStream.Reset();
		}

		virtual ~TRandomNodeOperator() = default;


		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace RandomNodeNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetInputNextTriggerName(), NextTrigger);
 			Inputs.AddDataReadReference(GetInputResetTriggerName(), ResetTrigger);
 			Inputs.AddDataReadReference(GetInputSeedName(), SeedValue);

			// If the type doesn't have a range no need for input pins to define it
			if (TRandomNodeSpecialization<ValueType>::HasRange())
			{
				Inputs.AddDataReadReference(GetInputMinName(), MinValue);
				Inputs.AddDataReadReference(GetInputMaxName(), MinValue);
			}
			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace RandomNodeNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputOnNextTriggerName(), TriggerOutOnNext);
			Outputs.AddDataReadReference(GetOutputOnResetTriggerName(), TriggerOutOnReset);
			Outputs.AddDataReadReference(GetOutputValueName(), OutputValue);
			return Outputs;
		}

		void Execute()
		{
			TriggerOutOnReset->AdvanceBlock();
			TriggerOutOnNext->AdvanceBlock();

			ResetTrigger->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					EvaluateSeedChanges();
					RandomStream.Reset();
					TriggerOutOnReset->TriggerFrame(StartFrame);
				}
			);

			NextTrigger->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					*OutputValue = TRandomNodeSpecialization<ValueType>::GetNextValue(RandomStream, *MinValue, *MaxValue);
					TriggerOutOnNext->TriggerFrame(StartFrame);
				}
			);
		}

	private:
		void EvaluateSeedChanges()
		{
			// if we have a non-zero seed
			if (*SeedValue != DefaultSeed)
			{
				// If we were previously zero-seeded OR our seed has changed
				if (bIsDefaultSeeded || !bIsRandomStreamInitialized || *SeedValue != RandomStream.GetInitialSeed())
				{
					bIsRandomStreamInitialized = true;
					bIsDefaultSeeded = false;
					RandomStream.Initialize(*SeedValue);
				}
			}
			// If we are zero-seeded now BUT were previously not, we need to randomize our seed
			else if (!bIsDefaultSeeded || !bIsRandomStreamInitialized)
			{
				bIsRandomStreamInitialized = true;
				bIsDefaultSeeded = true;
				RandomStream.Initialize(FPlatformTime::Cycles());
			}
		}


		FTriggerReadRef NextTrigger;
		FTriggerReadRef ResetTrigger;
		FInt32ReadRef SeedValue;
		TDataReadReference<ValueType> MinValue;
		TDataReadReference<ValueType> MaxValue;

		FTriggerWriteRef TriggerOutOnNext;
		FTriggerWriteRef TriggerOutOnReset;
		TDataWriteReference<ValueType> OutputValue;

		FRandomStream RandomStream;
		bool bIsDefaultSeeded = false;
		bool bIsRandomStreamInitialized = false;
	};

	/** TRandomNode
	 *
	 *  Generates a random float value when triggered.
	 */
	template<typename ValueType>
	class METASOUNDSTANDARDNODES_API TRandomNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		TRandomNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TRandomNodeOperator<ValueType>>())
		{}

		virtual ~TRandomNode() = default;
	};

} // namespace Metasound

