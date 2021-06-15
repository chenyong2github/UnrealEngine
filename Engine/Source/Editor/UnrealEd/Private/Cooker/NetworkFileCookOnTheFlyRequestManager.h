// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FNetworkFileServerOptions;

namespace UE { namespace Cook
{

class ICookOnTheFlyServer;
class ICookOnTheFlyRequestManager;

TUniquePtr<ICookOnTheFlyRequestManager> MakeNetworkFileCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, const FNetworkFileServerOptions& FileServerOptions);

}} // namespace UE::Cook
