// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Profile/MediaProfile.h"

#include "MediaFrameworkUtilitiesModule.h"

#include "Engine/Engine.h"
#include "Engine/EngineCustomTimeStep.h"
#include "Engine/TimecodeProvider.h"
#include "MediaAssets/ProxyMediaOutput.h"
#include "MediaAssets/ProxyMediaSource.h"
#include "MediaOutput.h"
#include "MediaSource.h"
#include "Profile/MediaProfileSettings.h"


UMediaSource* UMediaProfile::GetMediaSource(int32 Index) const
{
	if (MediaSources.IsValidIndex(Index))
	{
		return MediaSources[Index];
	}
	return nullptr;
}


int32 UMediaProfile::NumMediaSources() const
{
	return MediaSources.Num();
}


UMediaOutput* UMediaProfile::GetMediaOutput(int32 Index) const
{
	if (MediaOutputs.IsValidIndex(Index))
	{
		return MediaOutputs[Index];
	}
	return nullptr;
}


int32 UMediaProfile::NumMediaOutputs() const
{
	return MediaOutputs.Num();
}


UTimecodeProvider* UMediaProfile::GetTimecodeProvider() const
{
	return bOverrideTimecodeProvider ? TimecodeProvider : nullptr;
}


UEngineCustomTimeStep* UMediaProfile::GetCustomTimeStep() const
{
	return bOverrideCustomTimeStep ? CustomTimeStep : nullptr;
}


void UMediaProfile::Apply()
{
#if WITH_EDITORONLY_DATA
	bNeedToBeReapplied = false;
#endif

	if (GEngine == nullptr)
	{
		UE_LOG(LogMediaFrameworkUtilities, Error, TEXT("The MediaProfile '%s' could not be applied. The Engine is not initialized."), *GetName());
		return;
	}

	// Make sure we have the same amount of souces and outputs as the number of proxies.
	FixNumSourcesAndOutputs();

	{
		TArray<UProxyMediaSource*> SourceProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaSourceProxy();
		check(SourceProxies.Num() == MediaSources.Num());
		for (int32 Index = 0; Index < MediaSources.Num(); ++Index)
		{
			UProxyMediaSource* Proxy = SourceProxies[Index];
			if (Proxy)
			{
				Proxy->SetDynamicMediaSource(MediaSources[Index]);
			}
		}
	}

	{
		TArray<UProxyMediaOutput*> OutputProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaOutputProxy();
		check(OutputProxies.Num() == MediaOutputs.Num());
		for (int32 Index = 0; Index < MediaOutputs.Num(); ++Index)
		{
			UProxyMediaOutput* Proxy = OutputProxies[Index];
			if (Proxy)
			{
				Proxy->SetDynamicMediaOutput(MediaOutputs[Index]);
			}
		}
	}

	if (bOverrideTimecodeProvider)
	{
		if (TimecodeProvider)
		{
			bool bResult = GEngine->SetTimecodeProvider(TimecodeProvider);
			if (!bResult)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The TimecodeProvider '%s' could not be initialized."), *TimecodeProvider->GetName());
			}
		}
		else
		{
			GEngine->SetTimecodeProvider(nullptr);
		}
	}

	if (bOverrideCustomTimeStep)
	{
		if (CustomTimeStep)
		{
			bool bResult = GEngine->SetCustomTimeStep(CustomTimeStep);
			if (!bResult)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The Custom Time Step '%s' could not be initialized."), *CustomTimeStep->GetName());
			}
		}
		else
		{
			GEngine->SetCustomTimeStep(nullptr);
		}
	}
}


void UMediaProfile::Reset()
{
	if (GEngine == nullptr)
	{
		UE_LOG(LogMediaFrameworkUtilities, Error, TEXT("The MediaProfile '%s' could not be reset. The Engine is not initialized."), *GetName());
		return;
	}

	{
		// Reset the source proxies
		TArray<UProxyMediaSource*> SourceProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaSourceProxy();
		for (UProxyMediaSource* Proxy : SourceProxies)
		{
			if (Proxy)
			{
				Proxy->SetDynamicMediaSource(nullptr);
			}
		}
	}

	{
		// Reset the output proxies
		TArray<UProxyMediaOutput*> OutputProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaOutputProxy();
		for (UProxyMediaOutput* Proxy : OutputProxies)
		{
			if (Proxy)
			{
				Proxy->SetDynamicMediaOutput(nullptr);
			}
		}
	}

	{
		// Reset the timecode provider
		const UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
		if (CurrentTimecodeProvider)
		{
			if (CurrentTimecodeProvider->GetOuter() == this)
			{
				GEngine->SetTimecodeProvider(nullptr);
			}
		}
	}

	{
		// Reset the engine custom time step
		const UEngineCustomTimeStep* CurrentCustomTimeStep = GEngine->GetCustomTimeStep();
		if (CurrentCustomTimeStep)
		{
			if (CurrentCustomTimeStep->GetOuter() == this)
			{
				GEngine->SetCustomTimeStep(GEngine->GetDefaultCustomTimeStep());
			}
		}
	}
}

void UMediaProfile::FixNumSourcesAndOutputs()
{
	const int32 NumSourceProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaSourceProxy().Num();
	if (MediaSources.Num() != NumSourceProxies)
	{
		MediaSources.SetNumZeroed(NumSourceProxies);
		Modify();
	}

	const int32 NumOutputProxies = GetDefault<UMediaProfileSettings>()->GetAllMediaOutputProxy().Num();
	if (MediaOutputs.Num() != NumOutputProxies)
	{
		Modify();
		MediaOutputs.SetNumZeroed(NumOutputProxies);
	}
}