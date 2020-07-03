// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Algo/Sort.h"

UMovieScenePropertyTrack::UMovieScenePropertyTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EvalOptions.bEvaluateNearestSection_DEPRECATED = EvalOptions.bCanEvaluateNearestSection = true;
}


void UMovieScenePropertyTrack::SetPropertyNameAndPath(FName InPropertyName, const FString& InPropertyPath)
{
	check((InPropertyName != NAME_None) && !InPropertyPath.IsEmpty());

	PropertyBinding = FMovieScenePropertyBinding(InPropertyName, InPropertyPath);

#if WITH_EDITORONLY_DATA
	if (UniqueTrackName == NAME_None)
	{
		UniqueTrackName = *InPropertyPath;
	}
#endif
}


const TArray<UMovieSceneSection*>& UMovieScenePropertyTrack::GetAllSections() const
{
	return Sections;
}


void UMovieScenePropertyTrack::PostLoad()
{
#if WITH_EDITORONLY_DATA

	if (UniqueTrackName.IsNone())
	{
		UniqueTrackName = PropertyBinding.PropertyPath;
	}

#endif

	Super::PostLoad();
}

void UMovieScenePropertyTrack::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FMovieSceneEvaluationCustomVersion::GUID);

	Super::Serialize(Ar);
#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FMovieSceneEvaluationCustomVersion::GUID) < FMovieSceneEvaluationCustomVersion::EntityManager)
		{
			if (PropertyName_DEPRECATED != NAME_None && !PropertyPath_DEPRECATED.IsEmpty())
			{
				PropertyBinding = FMovieScenePropertyBinding(PropertyName_DEPRECATED, PropertyPath_DEPRECATED);
			}
		}
	}
#endif
}

#if WITH_EDITORONLY_DATA
FText UMovieScenePropertyTrack::GetDefaultDisplayName() const
{
	return FText::FromName(PropertyBinding.PropertyName);
}

FName UMovieScenePropertyTrack::GetTrackName() const
{
	return UniqueTrackName;
}
#endif

void UMovieScenePropertyTrack::RemoveAllAnimationData()
{
	Sections.Empty();
	SectionToKey = nullptr;
}


bool UMovieScenePropertyTrack::HasSection(const UMovieSceneSection& Section) const 
{
	return Sections.Contains(&Section);
}


void UMovieScenePropertyTrack::AddSection(UMovieSceneSection& Section) 
{
	Sections.Add(&Section);
}


void UMovieScenePropertyTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
	if (SectionToKey == &Section)
	{
		if (Sections.Num() > 0)
		{
			SectionToKey = Sections[0];
		}
		else
		{
			SectionToKey = nullptr;
		}
	}
}

void UMovieScenePropertyTrack::RemoveSectionAt(int32 SectionIndex)
{
	bool bResetSectionToKey = (SectionToKey == Sections[SectionIndex]);

	Sections.RemoveAt(SectionIndex);

	if (bResetSectionToKey)
	{
		SectionToKey = Sections.Num() > 0 ? Sections[0] : nullptr;
	}
}


bool UMovieScenePropertyTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}


TArray<UMovieSceneSection*, TInlineAllocator<4>> UMovieScenePropertyTrack::FindAllSections(FFrameNumber Time)
{
	TArray<UMovieSceneSection*, TInlineAllocator<4>> OverlappingSections;

	for (UMovieSceneSection* Section : Sections)
	{
		if (Section->GetRange().Contains(Time))
		{
			OverlappingSections.Add(Section);
		}
	}

	Algo::Sort(OverlappingSections, MovieSceneHelpers::SortOverlappingSections);

	return OverlappingSections;
}


UMovieSceneSection* UMovieScenePropertyTrack::FindSection(FFrameNumber Time)
{
	TArray<UMovieSceneSection*, TInlineAllocator<4>> OverlappingSections = FindAllSections(Time);

	if (OverlappingSections.Num())
	{
		if (SectionToKey && OverlappingSections.Contains(SectionToKey))
		{
			return SectionToKey;
		}
		else
		{
			return OverlappingSections[0];
		}
	}

	return nullptr;
}


UMovieSceneSection* UMovieScenePropertyTrack::FindOrExtendSection(FFrameNumber Time, float& Weight)
{
	Weight = 1.0f;
	TArray<UMovieSceneSection*, TInlineAllocator<4>> OverlappingSections = FindAllSections(Time);
	if (SectionToKey)
	{
		bool bCalculateWeight = false;
		if (SectionToKey && !OverlappingSections.Contains(SectionToKey))
		{
			if (SectionToKey->HasEndFrame() && SectionToKey->GetExclusiveEndFrame() <= Time)
			{
				if (SectionToKey->GetExclusiveEndFrame() != Time)
				{
					SectionToKey->SetEndFrame(Time);
				}
			}
			else
			{
				SectionToKey->SetStartFrame(Time);
			}
			if (OverlappingSections.Num() > 0)
			{
				bCalculateWeight = true;
			}
		}
		else
		{
			if (OverlappingSections.Num() > 1)
			{
				bCalculateWeight = true;
			}
		}
		//we need to calculate weight also possibly
		FOptionalMovieSceneBlendType BlendType = SectionToKey->GetBlendType();
		if (bCalculateWeight)
		{
			Weight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, Time);
		}
		return SectionToKey;
	}
	else
	{
		if (OverlappingSections.Num() > 0)
		{
			return OverlappingSections[0];
		}
	}

	// Find a spot for the section so that they are sorted by start time
	TOptional<int32> MinDiff;
	int32 ClosestSectionIndex = -1;
	bool bStartFrame = false;
	for(int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* Section = Sections[SectionIndex];

		if (Section->HasStartFrame())
		{
			int32 Diff = FMath::Abs(Time.Value - Section->GetInclusiveStartFrame().Value);

			if (!MinDiff.IsSet())
			{
				MinDiff = Diff;
				ClosestSectionIndex = SectionIndex;
				bStartFrame = true;
			}
			else if (Diff < MinDiff.GetValue())
			{
				MinDiff = Diff;
				ClosestSectionIndex = SectionIndex;
				bStartFrame = true;
			}
		}

		if (Section->HasEndFrame())
		{
			int32 Diff = FMath::Abs(Time.Value - Section->GetExclusiveEndFrame().Value);

			if (!MinDiff.IsSet())
			{
				MinDiff = Diff;
				ClosestSectionIndex = SectionIndex;
				bStartFrame = false;
			}
			else if (Diff < MinDiff.GetValue())
			{
				MinDiff = Diff;
				ClosestSectionIndex = SectionIndex;
				bStartFrame = false;
			}
		}
	}

	if (ClosestSectionIndex != -1)
	{
		UMovieSceneSection* ClosestSection = Sections[ClosestSectionIndex];
		if (bStartFrame)
		{
			ClosestSection->SetStartFrame(Time);
		}
		else
		{
			ClosestSection->SetEndFrame(Time);
		}

		return ClosestSection;
	}

	return nullptr;
}

UMovieSceneSection* UMovieScenePropertyTrack::FindOrAddSection(FFrameNumber Time, bool& bSectionAdded)
{
	bSectionAdded = false;

	UMovieSceneSection* FoundSection = FindSection(Time);
	if (FoundSection)
	{
		return FoundSection;
	}

	// Add a new section that starts and ends at the same time
	UMovieSceneSection* NewSection = CreateNewSection();
	ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
	NewSection->SetFlags(RF_Transactional);
	NewSection->SetRange(TRange<FFrameNumber>::Inclusive(Time, Time));

	Sections.Add(NewSection);
	
	bSectionAdded = true;

	return NewSection;
}

void UMovieScenePropertyTrack::SetSectionToKey(UMovieSceneSection* InSection)
{
	SectionToKey = InSection;
}

UMovieSceneSection* UMovieScenePropertyTrack::GetSectionToKey() const
{
	return SectionToKey;
}

