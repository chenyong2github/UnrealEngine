// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FNetworkFileServerOptions;

namespace UE { namespace Cook
{

class ICookOnTheFlyServer;
class ICookOnTheFlyRequestManager;
class ICookOnTheFlyNetworkServer;

TUniquePtr<ICookOnTheFlyRequestManager> MakeNetworkFileCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, TSharedRef<ICookOnTheFlyNetworkServer> NetworkServer);

}} // namespace UE::Cook
