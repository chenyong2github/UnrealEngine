// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiUserClientStatics.h"
#include "Logging/LogMacros.h"

#if WITH_CONCERT
#include "IMultiUserClientModule.h"
#include "IConcertSyncClient.h"
#include "IConcertClient.h"
#include "IConcertSession.h"
#include "IConcertClientPresenceManager.h"
#include "ConcertMessageData.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMultiUserClient, Log, All);

#define LOCTEXT_NAMESPACE "MultiUserClientStatics"

#if WITH_CONCERT
namespace MultiUserClientUtil
{

FMultiUserClientInfo ConvertClientInfo(const FGuid& ClientEndpointId, const FConcertClientInfo& ClientInfo)
{
	FMultiUserClientInfo Result;
	Result.ClientEndpointId = ClientEndpointId;
	Result.DisplayName = ClientInfo.DisplayName;
	Result.AvatarColor = ClientInfo.AvatarColor;
	Result.Tags = ClientInfo.Tags;
	return Result;
}

} // namespace MultiUserClientUtil
#endif

UMultiUserClientStatics::UMultiUserClientStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMultiUserClientStatics::SetMultiUserPresenceEnabled(const bool IsEnabled)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient();
		if (ConcertSyncClient && ConcertSyncClient->GetPresenceManager())
		{
			ConcertSyncClient->GetPresenceManager()->SetPresenceEnabled(IsEnabled);
		}
	}
#endif
}

void UMultiUserClientStatics::SetMultiUserPresenceVisibility(const FString& Name, bool Visibility, bool PropagateToAll)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient();
		if (ConcertSyncClient && ConcertSyncClient->GetPresenceManager())
		{
			ConcertSyncClient->GetPresenceManager()->SetPresenceVisibility(Name, Visibility, PropagateToAll);
		}
	}
#endif
}

void UMultiUserClientStatics::SetMultiUserPresenceVisibilityById(const FGuid& ClientEndpointId, bool Visibility, bool PropagateToAll /*= false*/)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient();
		if (ConcertSyncClient && ConcertSyncClient->GetPresenceManager())
		{
			ConcertSyncClient->GetPresenceManager()->SetPresenceVisibility(ClientEndpointId, Visibility, PropagateToAll);
		}
	}
#endif
}

FTransform UMultiUserClientStatics::GetMultiUserPresenceTransform(const FGuid& ClientEndpointId)
{
	FTransform PresenceTransform;
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient();
		IConcertClientPresenceManager* PresenceManager = ConcertSyncClient ? ConcertSyncClient->GetPresenceManager() : nullptr;
		if (PresenceManager)
		{
			PresenceTransform = PresenceManager->GetPresenceTransform(ClientEndpointId);
		}
	}
#endif
	return PresenceTransform;
}

void UMultiUserClientStatics::JumpToMultiUserPresence(const FString& OtherUserName, FTransform TransformOffset)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();

			const TSharedPtr<IConcertClientSession> ClientSession = ConcertClient->GetCurrentSession();
			FGuid OtherClientId;

			if (ClientSession.IsValid())
			{
				const TArray<FConcertSessionClientInfo> SessionClients = ClientSession->GetSessionClients();
				for (const FConcertSessionClientInfo SessionClient : SessionClients)
				{
					if (SessionClient.ClientInfo.DisplayName == OtherUserName)
					{
						OtherClientId = SessionClient.ClientEndpointId;
					}
				}
			}

			if (OtherClientId.IsValid() && ConcertSyncClient->GetPresenceManager())
			{
				ConcertSyncClient->GetPresenceManager()->InitiateJumpToPresence(OtherClientId, TransformOffset);
			}
		}
	}
#endif
}

void UMultiUserClientStatics::UpdateWorkspaceModifiedPackages()
{
	PersistMultiUserSessionChanges();
}

void UMultiUserClientStatics::PersistMultiUserSessionChanges()
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			ConcertSyncClient->PersistAllSessionChanges();
		}
	}
#endif
}

FMultiUserClientInfo UMultiUserClientStatics::GetLocalMultiUserClientInfo()
{
	FMultiUserClientInfo ClientInfo;
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
			TSharedPtr<IConcertClientSession> ClientSession = ConcertClient->GetCurrentSession();
		
			FGuid LocalClientEndpointId = ClientSession ? ClientSession->GetSessionClientEndpointId() : FGuid();
			const FConcertClientInfo& LocalClientInfo = ClientSession ? ClientSession->GetLocalClientInfo() : ConcertClient->GetClientInfo();
			ClientInfo = MultiUserClientUtil::ConvertClientInfo(LocalClientEndpointId, LocalClientInfo);
		}
	}
#endif
	return ClientInfo;
}

bool UMultiUserClientStatics::GetMultiUserClientInfoByName(const FString& ClientName, FMultiUserClientInfo& ClientInfo)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
			const TSharedPtr<IConcertClientSession> ClientSession = ConcertClient->GetCurrentSession();

			// We return the first match by name that we find. We expect the user to avoid name collisions in the user names. 
			// @todo: We can change this behavior once Concert has unique client IDs that persist across sessions.
			const FConcertClientInfo& LocalClientInfo = ClientSession ? ClientSession->GetLocalClientInfo() : ConcertClient->GetClientInfo();
			if (ClientName == LocalClientInfo.DisplayName)
			{
				ClientInfo = MultiUserClientUtil::ConvertClientInfo(ClientSession ? ClientSession->GetSessionClientEndpointId() : FGuid(), LocalClientInfo);
				return true;
			}

			if (ClientSession.IsValid())
			{
				const TArray<FConcertSessionClientInfo> SessionClients = ClientSession->GetSessionClients();
				for (const FConcertSessionClientInfo& SessionClient : SessionClients)
				{
					if (SessionClient.ClientInfo.DisplayName == ClientName)
					{
						ClientInfo = MultiUserClientUtil::ConvertClientInfo(SessionClient.ClientEndpointId, SessionClient.ClientInfo);
						return true;
					}
				}
			}
		}
	}
#endif
	return false;
}

bool UMultiUserClientStatics::GetRemoteMultiUserClientInfos(TArray<FMultiUserClientInfo>& ClientInfos)
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();

			const TSharedPtr<IConcertClientSession> ClientSession = ConcertClient->GetCurrentSession();
			if (ClientSession.IsValid())
			{
				const TArray<FConcertSessionClientInfo> SessionClients = ClientSession->GetSessionClients();
				for (const FConcertSessionClientInfo SessionClient : SessionClients)
				{
					ClientInfos.Add(MultiUserClientUtil::ConvertClientInfo(SessionClient.ClientEndpointId, SessionClient.ClientInfo));
				}

				return ClientInfos.Num() > 0;
			}
		}
	}
#endif
	return false;
}

bool UMultiUserClientStatics::GetMultiUserConnectionStatus()
{
#if WITH_CONCERT
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();

			const TSharedPtr<IConcertClientSession> ClientSession = ConcertClient->GetCurrentSession();
			if (ClientSession.IsValid())
			{
				return ClientSession->GetConnectionStatus() == EConcertConnectionStatus::Connected ? true : false;
			}
		}
	}
#endif
	return false;
}


#undef LOCTEXT_NAMESPACE
