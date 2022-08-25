// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once



#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

namespace UE { namespace Cook
{

class ICookOnTheFlyNetworkServer;
class ICookOnTheFlyRequestManager;
class ICookOnTheFlyServer;

TUniquePtr<ICookOnTheFlyRequestManager> MakeNetworkFileCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, TSharedRef<ICookOnTheFlyNetworkServer> NetworkServer);

}} // namespace UE::Cook
