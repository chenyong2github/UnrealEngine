// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXR.h"
#include "LiveLinkXRSource.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkXRSourceSettings.h"

#define LOCTEXT_NAMESPACE "LiveLinkXRModule"

DEFINE_LOG_CATEGORY(LogLiveLinkXR);

void FLiveLinkXRModule::StartupModule()
{
}

void FLiveLinkXRModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveLinkXRModule, LiveLinkXR)