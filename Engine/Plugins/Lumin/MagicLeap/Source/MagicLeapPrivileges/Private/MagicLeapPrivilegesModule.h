// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IMagicLeapPrivilegesModule.h"
#include "Lumin/CAPIShims/LuminAPI.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapPrivileges, Verbose, All);

class FMagicLeapPrivilegesModule : public IMagicLeapPrivilegesModule
{
public:
	FMagicLeapPrivilegesModule();

	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);

	bool RequestPrivilege(EMagicLeapPrivilege Privilege);
	bool CheckPrivilege(EMagicLeapPrivilege Privilege);
	bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestStaticDelegate& ResultDelegate) override;
	bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestDelegate& ResultDelegate) override;

private:
#if WITH_MLSDK
	struct FPendingAsyncRequest
	{
		EMagicLeapPrivilege Privilege;
		struct MLPrivilegesAsyncRequest* Request;
		FMagicLeapPrivilegeRequestStaticDelegate StaticDelegate;
		FMagicLeapPrivilegeRequestDelegate DynamicDelegate;
	};
	TArray<FPendingAsyncRequest> PendingAsyncRequests;
#endif //WITH_MLSDK
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;

	bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestStaticDelegate& StaticResultDelegate, const FMagicLeapPrivilegeRequestDelegate& DynamicResultDelegate);
};

inline FMagicLeapPrivilegesModule& GetMagicLeapPrivilegesModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapPrivilegesModule>("MagicLeapPrivileges");
}
