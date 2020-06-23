// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXR.h"
#include "LiveLinkXRSource.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkXRSourceSettings.h"

#define LOCTEXT_NAMESPACE "LiveLinkXRModule"

bool FLiveLinkXRModule::bWasShutdown = false;

void FLiveLinkXRModule::StartupModule()
{
	bWasShutdown = false;
}

void FLiveLinkXRModule::ShutdownModule()
{
	bWasShutdown = true;
}

ULiveLinkXRSettingsObject const* const FLiveLinkXRModule::GetSettings() const
{
	return ULiveLinkXRSettingsObject::StaticClass()->GetDefaultObject<ULiveLinkXRSettingsObject>();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveLinkXRModule, LiveLinkXR)