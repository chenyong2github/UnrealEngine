// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IAssetRegistry;

namespace UE { namespace Cook
{

class ICookOnTheFlyServer;
class ICookOnTheFlyRequestManager;
class ICookOnTheFlyNetworkServer;

TUniquePtr<ICookOnTheFlyRequestManager> MakeIoStoreCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, const IAssetRegistry* AssetRegistry, TSharedRef<ICookOnTheFlyNetworkServer> ConnectionServer);

}} // namespace UE::Cook
