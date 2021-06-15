// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE { namespace Cook
{

class ICookOnTheFlyServer;
class ICookOnTheFlyRequestManager;

struct FIoStoreCookOnTheFlyServerOptions
{
	int32 Port = -1; // -1 indicates the default COTF serving port
};

TUniquePtr<ICookOnTheFlyRequestManager> MakeIoStoreCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, FIoStoreCookOnTheFlyServerOptions Options);

}} // namespace UE::Cook
