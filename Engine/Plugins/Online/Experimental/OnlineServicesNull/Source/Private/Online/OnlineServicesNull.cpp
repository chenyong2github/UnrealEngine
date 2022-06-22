// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesNull.h"
#include "Online/OnlineServicesNullTypes.h"

#include "Online/AchievementsNull.h"
#include "Online/AuthNull.h"
#include "Online/StatsNull.h"
#include "Online/LobbiesNull.h"
#include "Online/TitleFileNull.h"

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

FOnlineServicesNull::FOnlineServicesNull(FName InInstanceName)
	: FOnlineServicesCommon(TEXT("Null"), InInstanceName)
{
}

void FOnlineServicesNull::RegisterComponents()
{
	Components.Register<FAchievementsNull>(*this);
	Components.Register<FAuthNull>(*this);
	Components.Register<FStatsNull>(*this);
#if WITH_ENGINE
	Components.Register<FLobbiesNull>(*this);
#endif
	Components.Register<FTitleFileNull>(*this);
	FOnlineServicesCommon::RegisterComponents();
}

TOnlineResult<FGetResolvedConnectString> FOnlineServicesNull::GetResolvedConnectString(FGetResolvedConnectString::Params&& Params)
{
#if WITH_ENGINE
	ILobbiesPtr LobbiesPtr = GetLobbiesInterface();
	check(LobbiesPtr);

	TOnlineResult<FGetJoinedLobbies> JoinedLobbies = LobbiesPtr->GetJoinedLobbies({ Params.LocalUserId });
	if (JoinedLobbies.IsOk())
	{
		for (TSharedRef<const FLobby>& Lobby : JoinedLobbies.GetOkValue().Lobbies)
		{
			if (Lobby->LobbyId == Params.LobbyId)
			{
				FName Tag = FName(TEXT("ConnectAddress")); // todo: global tag
				if(Lobby->Attributes.Contains(Tag))
				{
					FGetResolvedConnectString::Result Result;
					Result.ResolvedConnectString = Lobby->Attributes[Tag].GetString();
 					return TOnlineResult<FGetResolvedConnectString>(Result);
				}
				else
				{
					continue;
				}
			}
		}
		// No matching lobby
		return TOnlineResult<FGetResolvedConnectString>(Errors::InvalidParams());
	}
	else
	{
		return TOnlineResult<FGetResolvedConnectString>(JoinedLobbies.GetErrorValue());
	}
#else
	return Super::GetResolvedConnectString(MoveTemp(Params));
#endif
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

