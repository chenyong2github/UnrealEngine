// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationServicesModule.h"
#include "MagicLeapLocationServicesImpl.h"
#include "IMagicLeapPlugin.h"

DEFINE_LOG_CATEGORY(LogMagicLeapLocationServices);

using namespace MagicLeap;

void FMagicLeapLocationServicesModule::StartupModule()
{
	ImplInstance = NewObject<UMagicLeapLocationServicesImpl>();
	ULocationServices::SetLocationServicesImpl(ImplInstance);
}

void FMagicLeapLocationServicesModule::ShutdownModule()
{
	ULocationServices::ClearLocationServicesImpl();
	ImplInstance = NULL;
}

IMPLEMENT_MODULE(FMagicLeapLocationServicesModule, MagicLeapLocationServices);
