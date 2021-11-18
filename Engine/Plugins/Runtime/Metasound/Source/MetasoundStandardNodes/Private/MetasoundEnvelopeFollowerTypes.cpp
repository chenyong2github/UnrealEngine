// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEnvelopeFollowerTypes.h"
#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_EnvelopeFollower"

namespace Metasound
{
	DEFINE_METASOUND_ENUM_BEGIN(EEnvelopePeakMode, FEnumEnvelopePeakMode, "EnvelopePeakMode")
		DEFINE_METASOUND_ENUM_ENTRY(EEnvelopePeakMode::MeanSquared, LOCTEXT("EnvelopePeakModeMSDescription", "MS"), LOCTEXT("EnvelopePeakModeMSDescriptionTT", "Envelope follows a running Mean Squared of the audio signal.")),
		DEFINE_METASOUND_ENUM_ENTRY(EEnvelopePeakMode::RootMeanSquared, LOCTEXT("EnvelopePeakModeRMSDescription", "RMS"), LOCTEXT("EnvelopePeakModeRMSDescriptionTT", "Envelope follows a running Root Mean Squared of the audio signal.")),
		DEFINE_METASOUND_ENUM_ENTRY(EEnvelopePeakMode::Peak, LOCTEXT("EnvelopePeakModePeakDescription", "Peak"), LOCTEXT("EnvelopePeakModePeakDescriptionTT", "Envelope follows the peaks in the audio signal.")),
		DEFINE_METASOUND_ENUM_END()
}

#undef LOCTEXT_NAMESPACE