// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Profile/MediaProfileManager.h"
#include "MediaFrameworkUtilitiesModule.h"

#include "Modules/ModuleManager.h"
#include "MediaAssets/ProxyMediaOutput.h"
#include "MediaAssets/ProxyMediaSource.h"
#include "Profile/MediaProfile.h"
#include "Profile/MediaProfileSettings.h"


IMediaProfileManager& IMediaProfileManager::Get()
{
	static FName NAME_MediaFrameworkUtilities = TEXT("MediaFrameworkUtilities");
	return FModuleManager::GetModuleChecked<IMediaFrameworkUtilitiesModule>(NAME_MediaFrameworkUtilities).GetProfileManager();
}


FMediaProfileManager::FMediaProfileManager()
	: CurrentMediaProfile(nullptr)
{
}


UMediaProfile* FMediaProfileManager::GetCurrentMediaProfile() const
{
	return CurrentMediaProfile.Get();
}


void FMediaProfileManager::SetCurrentMediaProfile(UMediaProfile* InMediaProfile)
{
	bool bRemoveDelegate = false;
	bool bAddDelegate = false;
	UMediaProfile* Previous = CurrentMediaProfile.Get();
	if (InMediaProfile != Previous)
	{
		if (Previous)
		{
			Previous->Reset();
			bRemoveDelegate = true;
		}

		if (InMediaProfile)
		{
			InMediaProfile->Apply();
			bAddDelegate = true;
		}

		CurrentMediaProfile.Reset(InMediaProfile);
		MediaProfileChangedDelegate.Broadcast(Previous, InMediaProfile);
	}

#if WITH_EDITOR
	if (UObjectInitialized())
	{
		if (bAddDelegate && !bRemoveDelegate)
		{
			GetMutableDefault<UMediaProfileSettings>()->OnMediaProxiesChanged.AddRaw(this, &FMediaProfileManager::OnMediaProxiesChanged);
		}
		else if (bRemoveDelegate && !bAddDelegate)
		{
			GetMutableDefault<UMediaProfileSettings>()->OnMediaProxiesChanged.RemoveAll(this);
		}
	}
#endif
}


FMediaProfileManager::FOnMediaProfileChanged& FMediaProfileManager::OnMediaProfileChanged()
{
	return MediaProfileChangedDelegate;
}

#if WITH_EDITOR
void FMediaProfileManager::OnMediaProxiesChanged()
{
	UMediaProfile* CurrentMediaProfilePtr = CurrentMediaProfile.Get();
	if (CurrentMediaProfilePtr)
	{
		CurrentMediaProfilePtr->Reset();
		CurrentMediaProfilePtr->Apply();
	}
}
#endif
