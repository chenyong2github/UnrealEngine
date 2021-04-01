// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace StandardNodes
	{
		const FText Generators = { LOCTEXT("Metasound_GeneratorsCategory", "Generators") };
		const FText Filters = { LOCTEXT("Metasound_FiltersCategory", "Filters") };
		const FText TriggerUtils = { LOCTEXT("Metasound_TriggerUtilsCategory", "Trigger Utilities") };
		const FText Music = { LOCTEXT("Metasound_MusicCategory", "Music") };
		const FText RandomUtils = { LOCTEXT("Metasound_RandomUtilsCategory", "Random Utilities") };
		const FText Conversions  = { LOCTEXT("Metasound_ConversionsCategory", "Conversions") };
		const FText DebugUtils = { LOCTEXT("Metasound_DebugUtilsCategory", "Debug Utilities") };
		const FText Io = { LOCTEXT("Metasound_IoCategory", "External IO") };
	}
}

#undef LOCTEXT_NAMESPACE