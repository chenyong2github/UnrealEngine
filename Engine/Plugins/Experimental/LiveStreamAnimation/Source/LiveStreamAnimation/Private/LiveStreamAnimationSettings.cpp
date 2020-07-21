// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationSettings.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"

ULiveStreamAnimationSettings::ULiveStreamAnimationSettings()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// These are just default names, but any can be added / removed
		// using GameConfig or through Edit > Plugin Settings > Live Stream Animation
		// This list **must** be consistent between all instances of the project, so do
		// not attempt to customize the list for Servers or Clients.
		HandleNames.Append({
			FName(TEXT("LiveLinkSubject1")),
			FName(TEXT("LiveLinkSubject2")),
			FName(TEXT("LiveLinkSubject3")),
			FName(TEXT("LiveLinkSubject4")),
			FName(TEXT("LiveLinkSubject5")),
			FName(TEXT("LiveStreamAnimationHandle1")),
			FName(TEXT("LiveStreamAnimationHandle2")),
			FName(TEXT("LiveStreamAnimationHandle3")),
			FName(TEXT("LiveStreamAnimationHandle4")),
			FName(TEXT("LiveStreamAnimationHandle5")),
			FName(TEXT("LiveLinkFrameTranslation1")),
			FName(TEXT("LiveLinkFrameTranslation2")),
			FName(TEXT("LiveLinkFrameTranslation3")),
			FName(TEXT("LiveLinkFrameTranslation4")),
			FName(TEXT("LiveLinkFrameTranslation5")),
		});
	}
}


class ULiveStreamAnimationLiveLinkFrameTranslator* ULiveStreamAnimationSettings::GetFrameTranslator()
{
	return GetMutableDefault<ULiveStreamAnimationSettings>()->FrameTranslator.LoadSynchronous();
}

FDelegateHandle ULiveStreamAnimationSettings::AddFrameTranslatorChangedCallback(FSimpleMulticastDelegate::FDelegate&& InDelegate)
{
	return GetMutableDefault<ULiveStreamAnimationSettings>()->OnFrameTranslatorChanged.Add(MoveTemp(InDelegate));
}

FDelegateHandle ULiveStreamAnimationSettings::AddFrameTranslatorChangedCallback(const FSimpleMulticastDelegate::FDelegate& InDelegate)
{
	return GetMutableDefault<ULiveStreamAnimationSettings>()->OnFrameTranslatorChanged.Add(InDelegate);
}

void ULiveStreamAnimationSettings::RemoveFrameTranslatorChangedCallback(FDelegateHandle DelegateHandle)
{
	GetMutableDefault<ULiveStreamAnimationSettings>()->OnFrameTranslatorChanged.Remove(DelegateHandle);
}

const TArrayView<const FName> ULiveStreamAnimationSettings::GetHandleNames()
{
	return GetDefault<ULiveStreamAnimationSettings>()->HandleNames;
}

#if WITH_EDITOR
void ULiveStreamAnimationSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULiveStreamAnimationSettings, FrameTranslator))
	{
		OnFrameTranslatorChanged.Broadcast();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULiveStreamAnimationSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULiveStreamAnimationSettings, FrameTranslator))
	{
		OnFrameTranslatorChanged.Broadcast();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif

FName ULiveStreamAnimationSettings::GetContainerName() const
{
	return Super::GetContainerName();
}

FName ULiveStreamAnimationSettings::GetCategoryName() const
{
	static const FName PluginCategory(TEXT("Plugins"));

	return PluginCategory;
}

FName ULiveStreamAnimationSettings::GetSectionName() const
{
	return Super::GetSectionName();
}

#if WITH_EDITOR
FText ULiveStreamAnimationSettings::GetSectionText() const
{
	return Super::GetSectionText();
}

FText ULiveStreamAnimationSettings::GetSectionDescription() const
{
	return Super::GetSectionDescription();
}
#endif