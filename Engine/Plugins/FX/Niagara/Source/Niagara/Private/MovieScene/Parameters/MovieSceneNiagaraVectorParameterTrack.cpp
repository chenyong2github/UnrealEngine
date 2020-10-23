// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieScene/Parameters/MovieSceneNiagaraVectorParameterTrack.h"
#include "MovieScene/Parameters/MovieSceneNiagaraVectorParameterSectionTemplate.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Channels/MovieSceneChannelProxy.h"

bool UMovieSceneNiagaraVectorParameterTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneVectorSection::StaticClass();
}

UMovieSceneSection* UMovieSceneNiagaraVectorParameterTrack::CreateNewSection()
{
	UMovieSceneVectorSection* VectorSection = NewObject<UMovieSceneVectorSection>(this, NAME_None, RF_Transactional);
	VectorSection->SetChannelsUsed(ChannelsUsed);
	return VectorSection;
}

void UMovieSceneNiagaraVectorParameterTrack::SetSectionChannelDefaults(UMovieSceneSection* Section, const TArray<uint8>& DefaultValueData) const
{
	UMovieSceneVectorSection* VectorSection = Cast<UMovieSceneVectorSection>(Section);
	if (ensureMsgf(VectorSection != nullptr, TEXT("Section must be a color section.")) && ensureMsgf(DefaultValueData.Num() == sizeof(float) * VectorSection->GetChannelsUsed(), TEXT("DefaultValueData must be the correct vector type.")))
	{
		FMovieSceneChannelProxy& VectorChannelProxy = VectorSection->GetChannelProxy();

		if (VectorSection->GetChannelsUsed() == 2 && ensureMsgf(DefaultValueData.Num() == sizeof(FVector2D), TEXT("DefaultValueData must be a FVector2D when channels used is 2")))
		{
			FVector2D DefaultValue = *((FVector2D*)DefaultValueData.GetData());
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(0), DefaultValue.X);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(1), DefaultValue.Y);
		}
		else if (VectorSection->GetChannelsUsed() == 3 && ensureMsgf(DefaultValueData.Num() == sizeof(FVector), TEXT("DefaultValueData must be a FVector when channels used is 3")))
		{
			FVector DefaultValue = *((FVector*)DefaultValueData.GetData());
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(0), DefaultValue.X);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(1), DefaultValue.Y);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(2), DefaultValue.Z);
		}
		else if (VectorSection->GetChannelsUsed() == 4 && ensureMsgf(DefaultValueData.Num() == sizeof(FVector4), TEXT("DefaultValueData must be a FVector4 when channels used is 4")))
		{
			FVector4 DefaultValue = *((FVector4*)DefaultValueData.GetData());
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(0), DefaultValue.X);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(1), DefaultValue.Y);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(2), DefaultValue.Z);
			SetChannelDefault(VectorChannelProxy, VectorSection->GetChannel(3), DefaultValue.W);
		}
	}
}

FMovieSceneEvalTemplatePtr UMovieSceneNiagaraVectorParameterTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	const UMovieSceneVectorSection* VectorSection = Cast<UMovieSceneVectorSection>(&InSection);
	if (VectorSection != nullptr)
	{
		TArray<FMovieSceneFloatChannel> ComponentChannels;
		for (int32 i = 0; i < VectorSection->GetChannelsUsed(); i++)
		{
			ComponentChannels.Add(VectorSection->GetChannel(i));
		}
		return FMovieSceneNiagaraVectorParameterSectionTemplate(GetParameter(), MoveTemp(ComponentChannels), VectorSection->GetChannelsUsed());
	}
	return FMovieSceneEvalTemplatePtr();
}

int32 UMovieSceneNiagaraVectorParameterTrack::GetChannelsUsed() const
{
	return ChannelsUsed;
}

void UMovieSceneNiagaraVectorParameterTrack::SetChannelsUsed(int32 InChannelsUsed)
{
	ChannelsUsed = InChannelsUsed;
}