// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesNull.h"
#include "Online/OnlineServicesNullTypes.h"

#include "Online/AuthNull.h"

namespace UE::Online {

struct FNullPlatformConfig
{
public:
	FString TestId;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FNullPlatformConfig)
// example config setup:
//	ONLINE_STRUCT_FIELD(FNullPlatformConfig, TestId)
END_ONLINE_STRUCT_META()

 /*Meta*/ }

FOnlineServicesNull::FOnlineServicesNull()
	: FOnlineServicesCommon(TEXT("Null"))
{
}

void FOnlineServicesNull::RegisterComponents()
{
	Components.Register<FAuthNull>(*this);
	FOnlineServicesCommon::RegisterComponents();
}

void FOnlineServicesNull::Initialize()
{
	FNullPlatformConfig NullPlatformConfig;
	LoadConfig(NullPlatformConfig);

// example config loading:
//	FTCHARToUTF8 TestId(*NullPlatformConfig.TestId);

	FOnlineServicesCommon::Initialize();
}


/* UE::Online */ }
