// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFreeD.h"
#include "LiveLinkFreeDSource.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkFreeDSourceSettings.h"

#define LOCTEXT_NAMESPACE "LiveLinkFreeDModule"

DEFINE_LOG_CATEGORY(LogLiveLinkFreeD);

void FLiveLinkFreeDModule::StartupModule()
{
}

void FLiveLinkFreeDModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveLinkFreeDModule, LiveLinkFreeD)