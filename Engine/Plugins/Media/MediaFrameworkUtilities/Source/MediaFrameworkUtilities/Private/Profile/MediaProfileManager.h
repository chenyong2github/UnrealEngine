// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Profile/IMediaProfileManager.h"
#include "UObject/StrongObjectPtr.h"

class UMediaProfile;
class UProxyMediaSource;
class UProxyMediaOutput;

class FMediaProfileManager : public IMediaProfileManager
{
public:
	FMediaProfileManager();

	virtual UMediaProfile* GetCurrentMediaProfile() const override;
	virtual void SetCurrentMediaProfile(UMediaProfile* InMediaProfile) override;
	virtual FOnMediaProfileChanged& OnMediaProfileChanged() override;

private:
#if WITH_EDITOR
	void OnMediaProxiesChanged();
#endif

	TStrongObjectPtr<UMediaProfile> CurrentMediaProfile;
	FOnMediaProfileChanged MediaProfileChangedDelegate;
};
