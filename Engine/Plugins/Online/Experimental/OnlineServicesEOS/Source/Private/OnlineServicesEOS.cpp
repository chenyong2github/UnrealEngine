// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineServicesEOS.h"
#include "AuthEOS.h"

namespace UE::Online {

FOnlineServicesEOS::FOnlineServicesEOS()
{
}

void FOnlineServicesEOS::RegisterComponents()
{
#if WITH_EOS_SDK
	Components.Register<FAuthEOS>(*this);
#endif
}

/* UE::Online */ }