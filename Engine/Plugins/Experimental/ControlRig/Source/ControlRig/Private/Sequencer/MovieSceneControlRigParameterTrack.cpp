// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sequencer/MovieSceneControlRigParameterTemplate.h"
#include "MovieScene.h"
#include "MovieSceneTimeHelpers.h"
#include "Channels/MovieSceneChannelProxy.h"
#define LOCTEXT_NAMESPACE "MovieSceneParameterControlRigTrack"


UMovieSceneControlRigParameterTrack::UMovieSceneControlRigParameterTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ControlRig(nullptr)
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
	NewSection->SetControlRig(ControlRig);
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
	if (ControlRig)
	{
		NewSection->RecreateWithThisControlRig(ControlRig,bSetDefault);
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
	if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(&Section))
	{
		if (CRSection->GetControlRig() != ControlRig)
		{
			CRSection->SetControlRig(ControlRig);
		}
		CRSection->ReconstructChannelProxy(true);
	}

	if (Sections.Num() > 1)
	{
		SetSectionToKey(&Section);
	}
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


UMovieSceneSection* UMovieSceneControlRigParameterTrack::CreateControlRigSection(FFrameNumber StartTime, UControlRig* InControlRig, bool bInOwnsControlRig)
{
	if (!bInOwnsControlRig)
	{
		InControlRig->Rename(nullptr, this);
	}
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

void UMovieSceneControlRigParameterTrack::ReconstructControlRig()
{
	if (ControlRig && !ControlRig->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedInitialization))
	{
		ControlRig->ConditionalPostLoad();
		ControlRig->Initialize();
		for (UMovieSceneSection* Section : Sections)
		{
			if (Section)
			{
				UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(Section);
				if (CRSection)
				{
					CRSection->RecreateWithThisControlRig(ControlRig, false);
				}
			}
		}
	}
}

void UMovieSceneControlRigParameterTrack::PostLoad()
{
	Super::PostLoad();

	ReconstructControlRig();
}

void UMovieSceneControlRigParameterTrack::PostEditImport()
{
	Super::PostEditImport();

	ReconstructControlRig();
}

CONTROLRIG_API void UMovieSceneControlRigParameterTrack::ReplaceControlRig(UControlRig* NewControlRig, bool RecreateChannels)
{
	ControlRig = NewControlRig;
	if (ControlRig->GetOuter() != this)
	{
		ControlRig->Rename(nullptr, this);
	}
	for (UMovieSceneSection* Section : Sections)
	{
		if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(Section))
		{
			if (RecreateChannels)
			{
				CRSection->RecreateWithThisControlRig(NewControlRig, CRSection->GetBlendType() == EMovieSceneBlendType::Absolute);
			}
			else
			{
				CRSection->SetControlRig(NewControlRig);
			}
		}	
	}
}

void UMovieSceneControlRigParameterTrack::GetSelectedNodes(TArray<FName>& SelectedControlNames)
{
	if (GetControlRig())
	{
		SelectedControlNames = GetControlRig()->CurrentControlSelection();
	}
}

TArray<FFBXNodeAndChannels>* UMovieSceneControlRigParameterTrack::GetNodeAndChannelMappings()
{
#if WITH_EDITOR
	if (GetControlRig() == nullptr)
	{
		return nullptr;
	}
	bool bSectionAdded;

	UMovieSceneControlRigParameterSection* CurrentSectionToKey = Cast<UMovieSceneControlRigParameterSection>(FindOrAddSection(0, bSectionAdded));
	if (!CurrentSectionToKey)
	{
		return nullptr;
	}

	const FName FloatChannelTypeName = FMovieSceneFloatChannel::StaticStruct()->GetFName();
	const FName BoolChannelTypeName = FMovieSceneBoolChannel::StaticStruct()->GetFName();
	const FName EnumChannelTypeName = FMovieSceneByteChannel::StaticStruct()->GetFName();
	const FName IntegerChannelTypeName = FMovieSceneIntegerChannel::StaticStruct()->GetFName();


	FMovieSceneChannelProxy& ChannelProxy = CurrentSectionToKey->GetChannelProxy();
	TArray<FFBXNodeAndChannels>* NodeAndChannels = new TArray<FFBXNodeAndChannels>();
	TArray<FString> StringArray;

	for (const FMovieSceneChannelEntry& Entry : CurrentSectionToKey->GetChannelProxy().GetAllEntries())
	{
		const FName ChannelTypeName = Entry.GetChannelTypeName();
		if (ChannelTypeName != FloatChannelTypeName && ChannelTypeName != BoolChannelTypeName
			&& ChannelTypeName != EnumChannelTypeName && ChannelTypeName != IntegerChannelTypeName)
		{
			continue;
		}

		TArrayView<FMovieSceneChannel* const>        Channels = Entry.GetChannels();
		TArrayView<const FMovieSceneChannelMetaData> AllMetaData = Entry.GetMetaData();

		for (int32 Index = 0; Index < Channels.Num(); ++Index)
		{
			FMovieSceneChannelHandle Channel = ChannelProxy.MakeHandle(ChannelTypeName, Index);

			const FMovieSceneChannelMetaData& MetaData = AllMetaData[Index];
			StringArray.SetNum(0);
			FString String = MetaData.Name.ToString();
			String.ParseIntoArray(StringArray, TEXT("."));
			if (StringArray.Num() > 0)
			{
				FString NodeName = StringArray[0];
				FRigControl* Control = GetControlRig() ? GetControlRig()->FindControl(FName(*StringArray[0])) : nullptr;
				if (Control)
				{
					NodeName = NodeName.ToUpper();
					if (NodeAndChannels->Num() == 0 || (*NodeAndChannels)[NodeAndChannels->Num() - 1].NodeName != NodeName)
					{
						FFBXNodeAndChannels NodeAndChannel;
						NodeAndChannel.MovieSceneTrack = this;
						NodeAndChannel.ControlType = (FFBXControlRigTypeProxyEnum)(uint8)Control->ControlType;
						NodeAndChannel.NodeName = NodeName;
						NodeAndChannels->Add(NodeAndChannel);
					}
					if (ChannelTypeName == FloatChannelTypeName)
					{
						FMovieSceneFloatChannel* FloatChannel = Channel.Cast<FMovieSceneFloatChannel>().Get();
						(*NodeAndChannels)[NodeAndChannels->Num() - 1].Channels.Add(FloatChannel);
					}
					else if (ChannelTypeName == BoolChannelTypeName)
					{
						FMovieSceneBoolChannel* BoolChannel = Channel.Cast<FMovieSceneBoolChannel>().Get();
						(*NodeAndChannels)[NodeAndChannels->Num() - 1].BoolChannels.Add(BoolChannel);
					}
					else if (ChannelTypeName == EnumChannelTypeName)
					{
						FMovieSceneByteChannel* EnumChannel = Channel.Cast<FMovieSceneByteChannel>().Get();
						(*NodeAndChannels)[NodeAndChannels->Num() - 1].EnumChannels.Add(EnumChannel);
					}
					else if (ChannelTypeName == IntegerChannelTypeName)
					{
						FMovieSceneIntegerChannel* IntegerChannel = Channel.Cast<FMovieSceneIntegerChannel>().Get();
						(*NodeAndChannels)[NodeAndChannels->Num() - 1].IntegerChannels.Add(IntegerChannel);
					}
				}
			}
		}
	}

	return NodeAndChannels;
#else
	return nullptr;
#endif
}


#undef LOCTEXT_NAMESPACE
