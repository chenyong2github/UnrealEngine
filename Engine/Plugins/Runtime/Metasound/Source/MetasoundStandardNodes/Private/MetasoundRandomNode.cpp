// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRandomNode.h"

#include "Internationalization/Text.h"
#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace RandomNodeNames
	{
		const FString& GetInputNextTriggerName()
		{
			static const FString Name = TEXT("Next");
			return Name;
		}

		const FString& GetInputResetTriggerName()
		{
			static const FString Name = TEXT("Reset");
			return Name;
		}

		const FString& GetInputSeedName()
		{
			static const FString Name = TEXT("Seed");
			return Name;
		}

		const FString& GetInputMinName()
		{
			static const FString Name = TEXT("Min");
			return Name;
		}

		const FString& GetInputMaxName()
		{
			static const FString Name = TEXT("Max");
			return Name;
		}

		const FString& GetOutputOnNextTriggerName()
		{
			static const FString Name = TEXT("On Next");
			return Name;
		}

		const FString& GetOutputOnResetTriggerName()
		{
			static const FString Name = TEXT("On Reset");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		static FText GetNextTriggerDescription()
		{
			return LOCTEXT("RandomNodeNextTT", "Trigger to generate the next random integer.");
		}

		static FText GetResetDescription()
		{
			return LOCTEXT("RandomNodeResetTT", "Trigger to reset the random sequence with the supplied seed. Useful to get randomized repetition.");
		}

		static FText GetSeedDescription()
		{
			return LOCTEXT("RandomNodeSeedTT", "The seed value to use for the random node. Set to -1 to use a random seed.");
		}

		static FText GetMinDescription()
		{
			return LOCTEXT("RandomNodeMinTT", "Min random value.");
		}

		static FText GetMaxDescription()
		{
			return LOCTEXT("RandNodeMaxTT", "Max random value.");
		}

		static FText GetOutputDescription()
		{
			return LOCTEXT("RandomNodeOutputTT", "The randomly generated value.");
		}

		static FText GetOutputOnNextDescription()
		{
			return LOCTEXT("RandomNodeOutputNextTT", "Triggers when next is triggered.");
		}

		static FText GetOutputOnResetDescription()
		{
			return LOCTEXT("RandomNodeOutputNextTT", "Triggers when reset is triggered.");
		}
	}

 	using FRandomNodeInt32 = TRandomNode<int32>;
 	METASOUND_REGISTER_NODE(FRandomNodeInt32)
 
 	using FRandomNodeFloat = TRandomNode<float>;
 	METASOUND_REGISTER_NODE(FRandomNodeFloat)
 
	using FRandomNodeBool = TRandomNode<bool>;
	METASOUND_REGISTER_NODE(FRandomNodeBool)
}

#undef LOCTEXT_NAMESPACE
