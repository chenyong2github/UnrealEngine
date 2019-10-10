// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sequencer/MovieSceneControlRigParameterTemplate.h"
#include "MovieScene.h"
#include "MovieSceneTimeHelpers.h"

#define LOCTEXT_NAMESPACE "MovieSceneParameterControlRigTrack"


UMovieSceneControlRigParameterTrack::UMovieSceneControlRigParameterTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(65, 89, 194, 65);
#endif

	SupportedBlendTypes = FMovieSceneBlendTypeField::None();
	SupportedBlendTypes.Add(EMovieSceneBlendType::Additive);
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

}
FMovieSceneEvalTemplatePtr UMovieSceneControlRigParameterTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneControlRigParameterTemplate(*CastChecked<UMovieSceneControlRigParameterSection>(&InSection), *this);
}

bool UMovieSceneControlRigParameterTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneControlRigParameterSection::StaticClass();
}

UMovieSceneSection* UMovieSceneControlRigParameterTrack::CreateNewSection()
{
	UMovieSceneControlRigParameterSection* NewSection = NewObject<UMovieSceneControlRigParameterSection>(this, NAME_None, RF_Transactional);
	NewSection->ControlRig = ControlRig;
	bool bSetDefault = false;
	if (Sections.Num() == 0)
	{
		NewSection->SetBlendType(EMovieSceneBlendType::Absolute);
		bSetDefault = true;
	}
	else
	{
		NewSection->SetBlendType(EMovieSceneBlendType::Additive);
	}
	UMovieScene* OuterMovieScene = GetTypedOuter<UMovieScene>();
	IControlRigManipulatable* Manip = GetManipulatableFromBinding(OuterMovieScene);
	if (Manip)
	{
		/*
		UENUM()
		enum class ERigControlType : uint8
		{
			Bool,
			Float,
			Vector2D,
			Position,
			Scale,
			Quat,
			Rotator,
			Transform
		};
		*/

		const TArray<FRigControl>& Controls = Manip->AvailableControls();
		TArray<bool> OnArray;
		OnArray.Init(true,Controls.Num());
		NewSection->SetControlsMask(OnArray);


		for (const FRigControl& RigControl : Controls)
		{
			switch (RigControl.ControlType)
			{
			case ERigControlType::Float:
			{
				TOptional<float> DefaultValue;
				if (bSetDefault)
				{
					//or use IntialValue?
					DefaultValue = RigControl.Value.Get<float>();
				}
				NewSection->AddScalarParameter(RigControl.Name,DefaultValue);
				break;
			}
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			{
				TOptional<FVector> DefaultValue;
				if (bSetDefault)
				{
					//or use IntialValue?
					DefaultValue = RigControl.Value.Get<FVector>();
				}
				NewSection->AddVectorParameter(RigControl.Name,DefaultValue);
				//mz todo specify rotator special so we can do quat interps or no we have a quat type so...
				break;
			}
			case ERigControlType::Transform:
			{
				TOptional<FTransform> DefaultValue;
				if (bSetDefault)
				{
					//or use IntialValue?
					DefaultValue = RigControl.Value.Get<FTransform>();
				}
				NewSection->AddTransformParameter(RigControl.Name,DefaultValue);
				break;
			}
				//mz todo the other types
			}
		}



	}
	return  NewSection;
}

void UMovieSceneControlRigParameterTrack::RemoveAllAnimationData()
{
	Sections.Empty();
	SectionToKey = nullptr;
}

bool UMovieSceneControlRigParameterTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

void UMovieSceneControlRigParameterTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}

void UMovieSceneControlRigParameterTrack::RemoveSection(UMovieSceneSection& Section)
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

void UMovieSceneControlRigParameterTrack::RemoveSectionAt(int32 SectionIndex)
{
	bool bResetSectionToKey = (SectionToKey == Sections[SectionIndex]);

	Sections.RemoveAt(SectionIndex);

	if (bResetSectionToKey)
	{
		SectionToKey = Sections.Num() > 0 ? Sections[0] : nullptr;
	}
}

bool UMovieSceneControlRigParameterTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

const TArray<UMovieSceneSection*>& UMovieSceneControlRigParameterTrack::GetAllSections() const
{
	return Sections;
}


#if WITH_EDITORONLY_DATA
FText UMovieSceneControlRigParameterTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("DisplayName", "Control Rig Parameter");
}
#endif




IControlRigManipulatable* UMovieSceneControlRigParameterTrack::GetManipulatableFromBinding(UMovieScene* MovieScene)
{
	if (ControlRig)
	{
		return ControlRig;
	}
	return nullptr;
}


UMovieSceneSection* UMovieSceneControlRigParameterTrack::CreateControlRigSection(FFrameNumber StartTime, UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	UMovieSceneControlRigParameterSection*  NewSection = Cast<UMovieSceneControlRigParameterSection>(CreateNewSection());

	UMovieScene* OuterMovieScene = GetTypedOuter<UMovieScene>();
	NewSection->SetRange(TRange<FFrameNumber>::All());

	//mz todo tbd maybe just set it to animated range? TRange<FFrameNumber> Range = OuterMovieScene->GetPlaybackRange();
	//Range.SetLowerBoundValue(StartTime);
	//NewSection->SetRange(Range);

	AddSection(*NewSection);

	return NewSection;
}

TArray<UMovieSceneSection*, TInlineAllocator<4>> UMovieSceneControlRigParameterTrack::FindAllSections(FFrameNumber Time)
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


UMovieSceneSection* UMovieSceneControlRigParameterTrack::FindSection(FFrameNumber Time)
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


UMovieSceneSection* UMovieSceneControlRigParameterTrack::FindOrExtendSection(FFrameNumber Time, float& Weight)
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
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* Section = Sections[SectionIndex];

		// Check if there are no more sections that would overlap the time 
		if (!Sections.IsValidIndex(SectionIndex + 1) || (Sections[SectionIndex + 1]->HasEndFrame() && Sections[SectionIndex + 1]->GetExclusiveEndFrame() > Time))
		{
			// No sections overlap the time

			if (SectionIndex > 0)
			{
				// Append and grow the previous section
				UMovieSceneSection* PreviousSection = Sections[SectionIndex ? SectionIndex - 1 : 0];

				PreviousSection->SetEndFrame(Time);
				return PreviousSection;
			}
			else if (Sections.IsValidIndex(SectionIndex + 1))
			{
				// Prepend and grow the next section because there are no sections before this one
				UMovieSceneSection* NextSection = Sections[SectionIndex + 1];
				NextSection->SetStartFrame(Time);
				return NextSection;
			}
			else
			{
				// SectionIndex == 0 
				UMovieSceneSection* PreviousSection = Sections[0];
				if (PreviousSection->HasEndFrame() && PreviousSection->GetExclusiveEndFrame() <= Time)
				{
					// Append and grow the section
					if (PreviousSection->GetExclusiveEndFrame() != Time)
					{
						PreviousSection->SetEndFrame(Time);
					}
				}
				else
				{
					// Prepend and grow the section
					PreviousSection->SetStartFrame(Time);
				}
				return PreviousSection;
			}
		}
	}

	return nullptr;
}

UMovieSceneSection* UMovieSceneControlRigParameterTrack::FindOrAddSection(FFrameNumber Time, bool& bSectionAdded)
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

void UMovieSceneControlRigParameterTrack::SetSectionToKey(UMovieSceneSection* InSection)
{
	SectionToKey = InSection;
}

UMovieSceneSection* UMovieSceneControlRigParameterTrack::GetSectionToKey() const
{
	return SectionToKey;
}

#undef LOCTEXT_NAMESPACE
