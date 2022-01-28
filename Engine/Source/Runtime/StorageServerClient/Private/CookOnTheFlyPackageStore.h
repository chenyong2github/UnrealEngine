// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_COTF

#include "IO/PackageStore.h"

class FIoDispatcher;

namespace UE { namespace Cook
{
	class ICookOnTheFlyServerConnection;
}}

TSharedPtr<IPackageStore> MakeCookOnTheFlyPackageStore(UE::Cook::ICookOnTheFlyServerConnection& CookOnTheFlyServerConnection);

#endif // WITH_COTF
