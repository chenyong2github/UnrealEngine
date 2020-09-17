// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IAdvertisingProvider.h"

class FIOSAdvertisingProvider : public IAdvertisingProvider
{
public:

	//empty functions for now, this is Android-only support until iAd is replaced by AdMob
	virtual void LoadInterstitialAd(int32 adID) override {}
	virtual bool IsInterstitialAdAvailable() override
	{
		return false;
	}
	virtual bool IsInterstitialAdRequested() override
	{
		return false;
	}
	virtual void ShowInterstitialAd() override {}
};
