// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace NodeCategories
	{
		const FText Audio = { LOCTEXT("Metasound_AudioCategory", "Audio") };
		const FText Debug = { LOCTEXT("Metasound_DebugCategory", "Debug") };
		const FText Delays = { LOCTEXT("Metasound_EffectsCategory", "Delays") };
		const FText Dynamics = { LOCTEXT("Metasound_DynamicsCategory", "Dynamics") };
		const FText Envelopes = { LOCTEXT("Metasound_EnvelopesCategory", "Envelopes") };
		const FText Filters = { LOCTEXT("Metasound_FiltersCategory", "Filters") };
		const FText Generators = { LOCTEXT("Metasound_GeneratorsCategory", "Generators") };
		const FText Io = { LOCTEXT("Metasound_IoCategory", "External IO") };
		const FText Math = { LOCTEXT("Metasound_MathCategory", "Math") };
		const FText Music = { LOCTEXT("Metasound_MusicCategory", "Music") };
		const FText RandomUtils = { LOCTEXT("Metasound_RandomCategory", "Random") };
		const FText Spatialization = { LOCTEXT("Metasound_SpatializationCategory", "Spatialization") };
		const FText Trigger = { LOCTEXT("Metasound_TriggerCategory", "Triggers") };
	}
}

#undef LOCTEXT_NAMESPACE