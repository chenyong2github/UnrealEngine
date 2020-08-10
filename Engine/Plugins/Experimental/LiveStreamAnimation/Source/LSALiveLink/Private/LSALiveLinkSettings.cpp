// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSALiveLinkSettings.h"
#include "LSALiveLinkFrameTranslator.h"
#include "LiveStreamAnimationSettings.h"

class ULSALiveLinkFrameTranslator* ULSALiveLinkSettings::GetFrameTranslator()
{
	return GetMutableDefault<ULSALiveLinkSettings>()->FrameTranslator.LoadSynchronous();
}

FDelegateHandle ULSALiveLinkSettings::AddFrameTranslatorChangedCallback(FSimpleMulticastDelegate::FDelegate&& InDelegate)
{
	return GetMutableDefault<ULSALiveLinkSettings>()->OnFrameTranslatorChanged.Add(MoveTemp(InDelegate));
}

FDelegateHandle ULSALiveLinkSettings::AddFrameTranslatorChangedCallback(const FSimpleMulticastDelegate::FDelegate& InDelegate)
{
	return GetMutableDefault<ULSALiveLinkSettings>()->OnFrameTranslatorChanged.Add(InDelegate);
}

void ULSALiveLinkSettings::RemoveFrameTranslatorChangedCallback(FDelegateHandle DelegateHandle)
{
	GetMutableDefault<ULSALiveLinkSettings>()->OnFrameTranslatorChanged.Remove(DelegateHandle);
}

#if WITH_EDITOR
void ULSALiveLinkSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULSALiveLinkSettings, FrameTranslator))
	{
		OnFrameTranslatorChanged.Broadcast();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULSALiveLinkSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULSALiveLinkSettings, FrameTranslator))
	{
		OnFrameTranslatorChanged.Broadcast();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif

FName ULSALiveLinkSettings::GetCategoryName() const
{
	static const FName PluginCategory(TEXT("Plugins"));

	return PluginCategory;
}
