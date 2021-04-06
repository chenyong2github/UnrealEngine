// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundEnum.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "DSP/MidiNoteQuantizer.h"
#include "Internationalization/Text.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{
#define LOCTEXT_NAMESPACE "MidiScaleDefinitions"
	// Any desired additions to this Enum/list need to first be added to the EMusicalScale enum in MidiNoteQuantizer.h
	// and defined in MidiNoteQuantizer.cpp in the TMap<EMusicalScale::Scale, ScaleDegreeSet> ScaleDegreeSetMap static init

	// Metasound enum
	DECLARE_METASOUND_ENUM(Audio::EMusicalScale::Scale, Audio::EMusicalScale::Scale::Major,
	METASOUNDSTANDARDNODES_API, FEnumEMusicalScale, FEnumMusicalScaleTypeInfo, FEnumMusicalScaleReadRef, FEnumMusicalScaleWriteRef);
	DEFINE_METASOUND_ENUM_BEGIN(Audio::EMusicalScale::Scale, FEnumEMusicalScale, "MusicalScale")

	// modes
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Major, LOCTEXT("MajorDescription", "Major Scale"), LOCTEXT("MajorDescriptionTT", "Major (Ionian)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Minor_Dorian, LOCTEXT("Minor_DorianDescription", "Minor (Dorian)"), LOCTEXT("Minor_DorianDescriptionTT", "Dorian Minor")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Phrygian, LOCTEXT("PhrygianDescription", "Phrygian "), LOCTEXT("PhrygianDescriptionTT", "Phrygian")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Lydian, LOCTEXT("LydianDescription", "Lydian"), LOCTEXT("LydianDescriptionTT", "Lydian (sharp-4)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Dominant7th_Mixolydian, LOCTEXT("Dominant7th_MixolydianDescription", "Dominant 7th (Mixolydian)"), LOCTEXT("Dominant7th_MixolydianDescriptionTT", "Mioxlydian (Dominant 7)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::NaturalMinor_Aeolian, LOCTEXT("NaturalMinor_AeolianDescription", "Natural Minor (Aeolian)"), LOCTEXT("NaturalMinor_AeolianDescriptionTT", "Natural Minor (Aeolian)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::HalfDiminished_Locrian, LOCTEXT("HalfDiminished_LocrianDescription", "Half Diminished (Locrian)"), LOCTEXT("HalfDiminished_LocrianDescriptionTT", "Half-Diminished (Locrian)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Diminished, LOCTEXT("DiminishedDescription", "Diminished "), LOCTEXT("DiminishedDescriptionTT", "Diminished")),
	// non-diatonic
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Chromatic, LOCTEXT("ChromaticDescription", "Chromatic"), LOCTEXT("ChromaticDescriptionTT", "Chromatic")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::WholeTone, LOCTEXT("WholeToneDescription", "Whole-Tone"), LOCTEXT("WholeToneDescriptionTT", "Whole Tone")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::DiminishedWholeTone, LOCTEXT("DiminishedWholeToneDescription", "Diminished Whole-Tone"), LOCTEXT("DiminishedWholeToneDescriptionTT", "Diminished Whole Tone")),
	// petantonic
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::MajorPentatonic, LOCTEXT("MajorPentatonicDescription", "Major Pentatonic "), LOCTEXT("MajorPentatonicDescriptionTT", "Major Pentatonic")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::MinorPentatonic, LOCTEXT("MinorPentatonicDescription", "Minor Pentatonic "), LOCTEXT("MinorPentatonicDescriptionTT", "Minor Pentatonic")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Blues, LOCTEXT("BluesDescription", "Blues "), LOCTEXT("BluesDescriptionTT", "Blues")),
	// bebop
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Bebop_Major, LOCTEXT("Bebop_MajorDescription", "Bebop (Major)"), LOCTEXT("Bebop_MajorDescriptionTT", "Bebop Major")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Bebop_Minor, LOCTEXT("Bebop_MinorDescription", "Bebop (Minor)"), LOCTEXT("Bebop_MinorDescriptionTT", "Bebop Minor")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Bebop_MinorNumber2, LOCTEXT("Bebop_MinorNumber2Description", "Bebop (Minor) #2"), LOCTEXT("Bebop_MinorNumber2DescriptionTT", "Bebop Minor #2")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Bebop_Dominant, LOCTEXT("Bebop_DominantDescription", "Bebop (Dominant)"), LOCTEXT("Bebop_DominantDescriptionTT", "Bebop Dominant")),
	// common major/minors
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::HarmonicMajor, LOCTEXT("HarmonicMajorDescription", "Harmonic Major"), LOCTEXT("HarmonicMajorDescriptionTT", "Harmonic Major")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::HarmonicMinor, LOCTEXT("HarmonicMinorDescription", "Harmonic Minor "), LOCTEXT("HarmonicMinorDescriptionTT", "Harmonic Minor")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::MelodicMinor, LOCTEXT("MelodicMinorDescription", "Melodic Minor "), LOCTEXT("MelodicMinorDescriptionTT", "Melodic Minor")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::SixthModeOfHarmonicMinor, LOCTEXT("SixthModeOfHarmonicMinorDescription", "Sixth Mode of Harmonic Minor"), LOCTEXT("SixthModeOfHarmonicMinorDescriptionTT", "Sixth Mode of Harmonic Minor")),
	// lydian/augmented
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::LydianAugmented, LOCTEXT("LydianAugmentedDescription", "Lydian Augmented"), LOCTEXT("LydianAugmentedDescriptionTT", "Lydian Augmented")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::LydianDominant, LOCTEXT("LydianDominantDescription", "Lydian Dominant "), LOCTEXT("LydianDominantDescriptionTT", "Lydian Dominant")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Augmented, LOCTEXT("AugmentedDescription", "Augmented"), LOCTEXT("AugmentedDescriptionTT", "Augmented")),
	// diminished
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Diminished_BeginWithHalfStep, LOCTEXT("Diminished_BeginWithHalfStepDescription", "Diminished (Begin With Half-Step)"), LOCTEXT("Diminished_BeginWithHalfStepDescriptionTT", "Diminished (begins with Half Step)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Diminished_BeginWithWholeStep, LOCTEXT("Diminished_BeginWithWholeStepDescription", "Diminished (Begin With Whole-Step"), LOCTEXT("Diminished_BeginWithWholeStepDescriptionTT", "Diminished (begins with Whole Step)")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::HalfDiminished_LocrianNumber2, LOCTEXT("HalfDiminished_LocrianNumber2Description", "Half-Diminished (Locrian #2)"), LOCTEXT("HalfDiminished_LocrianNumber2DescriptionTT", "Half Diminished Locrian (#2)")),
	// other
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Spanish_or_Jewish, LOCTEXT("Spanish_or_JewishDescription", "Spanish or Jewish Scale"), LOCTEXT("Spanish_or_JewishDescriptionTT", "Spanish/Jewish")),
	DEFINE_METASOUND_ENUM_ENTRY(Audio::EMusicalScale::Scale::Hindu, LOCTEXT("HinduDescription", "Hindu "), LOCTEXT("HinduDescriptionTT", "Hindu"))

	DEFINE_METASOUND_ENUM_END()

#undef LOCTEXT_NAMESPACE
} // namespace Metasound
