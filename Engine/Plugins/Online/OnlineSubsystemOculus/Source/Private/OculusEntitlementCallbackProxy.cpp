// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "OculusEntitlementCallbackProxy.h"
#include "OnlineSubsystemOculusPrivate.h"
#include "Online.h"

UOculusEntitlementCallbackProxy::UOculusEntitlementCallbackProxy(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

UOculusEntitlementCallbackProxy* UOculusEntitlementCallbackProxy::VerifyEntitlement()
{
	UOculusEntitlementCallbackProxy* Proxy = NewObject<UOculusEntitlementCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	return Proxy;
}

void UOculusEntitlementCallbackProxy::Activate()
{
	auto OculusIdentityInterface = Online::GetIdentityInterface(OCULUS_SUBSYSTEM);

	if (OculusIdentityInterface.IsValid())
	{
		auto Unused = new FUniqueNetIdString("UNUSED", OCULUS_SUBSYSTEM);
		OculusIdentityInterface->GetUserPrivilege(
			*Unused,
			EUserPrivileges::CanPlay,
			IOnlineIdentity::FOnGetUserPrivilegeCompleteDelegate::CreateUObject(this, &UOculusEntitlementCallbackProxy::OnUserPrivilegeCompleteDelegate)
		);
	}
	else
	{
		UE_LOG_ONLINE_ENTITLEMENT(Warning, TEXT("Oculus platform service not available. Skipping entitlement check."));
		OnFailure.Broadcast();
	}
}

void UOculusEntitlementCallbackProxy::OnUserPrivilegeCompleteDelegate(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, uint32 Result)
{
    if (Result == static_cast<uint32>(IOnlineIdentity::EPrivilegeResults::NoFailures))
    {
		OnSuccess.Broadcast();
    }
    else
    {
		OnFailure.Broadcast();
    }
}

