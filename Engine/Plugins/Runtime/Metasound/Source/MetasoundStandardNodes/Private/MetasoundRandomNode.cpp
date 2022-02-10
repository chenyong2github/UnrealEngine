// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRandomNode.h"

#include "Internationalization/Text.h"
#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace RandomNodeNames
	{
		const FVertexName& GetInputNextTriggerName()
		{
			static const FVertexName Name = TEXT("Next");
			return Name;
		}

		const FVertexName& GetInputResetTriggerName()
		{
			static const FVertexName Name = TEXT("Reset");
			return Name;
		}

		const FVertexName& GetInputSeedName()
		{
			static const FVertexName Name = TEXT("Seed");
			return Name;
		}

		const FVertexName& GetInputMinName()
		{
			static const FVertexName Name = TEXT("Min");
			return Name;
		}

		const FVertexName& GetInputMaxName()
		{
			static const FVertexName Name = TEXT("Max");
			return Name;
		}

		const FVertexName& GetOutputOnNextTriggerName()
		{
			static const FVertexName Name = TEXT("On Next");
			return Name;
		}

		const FVertexName& GetOutputOnResetTriggerName()
		{
			static const FVertexName Name = TEXT("On Reset");
			return Name;
		}

		const FVertexName& GetOutputValueName()
		{
			static const FVertexName Name = TEXT("Value");
			return Name;
		}

		static FText GetNextTriggerDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_NextTT", "Trigger to generate the next random integer.");
		}

		static FText GetResetDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_ResetTT", "Trigger to reset the random sequence with the supplied seed. Useful to get randomized repetition.");
		}

		static FText GetSeedDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_SeedTT", "The seed value to use for the random node. Set to -1 to use a random seed.");
		}

		static FText GetMinDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_MinTT", "Min random value.");
		}

		static FText GetMaxDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_MaxTT", "Max random value.");
		}

		static FText GetOutputDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_OutputTT", "The randomly generated value.");
		}

		static FText GetOutputOnNextDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_OutputNextTT", "Triggers when next is triggered.");
		}

		static FText GetOutputOnResetDescription()
		{
			return METASOUND_LOCTEXT("RandomNode_OutputResetTT", "Triggers when reset is triggered.");
		}
	}

	// Mac Clang require linkage for constexpr
	template<typename ValueType>
	constexpr int32 Metasound::TRandomNodeOperator<ValueType>::DefaultSeed;

 	using FRandomNodeInt32 = TRandomNode<int32>;
 	METASOUND_REGISTER_NODE(FRandomNodeInt32)
 
 	using FRandomNodeFloat = TRandomNode<float>;
 	METASOUND_REGISTER_NODE(FRandomNodeFloat)
 
	using FRandomNodeBool = TRandomNode<bool>;
	METASOUND_REGISTER_NODE(FRandomNodeBool)

	using FRandomNodeTime = TRandomNode<FTime>;
	METASOUND_REGISTER_NODE(FRandomNodeTime)
}

#undef LOCTEXT_NAMESPACE
