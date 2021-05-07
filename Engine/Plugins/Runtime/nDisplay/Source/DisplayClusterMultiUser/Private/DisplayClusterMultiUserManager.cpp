// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMultiUserManager.h"
#include "DisplayClusterMultiUserLog.h"

#include "DisplayClusterConfigurationTypes_Base.h"

#include "IConcertSyncClient.h"
#include "IConcertSyncClientModule.h"

#define NDISPLAY_MULTIUSER_TRANSACTION_FILTER TEXT("DisplayClusterMultiUser")

FDisplayClusterMultiUserManager::FDisplayClusterMultiUserManager()
{
	const TSharedPtr<IConcertSyncClient> ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser"));
	if (ConcertSyncClient.IsValid())
	{
		IConcertClientTransactionBridge* Bridge = ConcertSyncClient->GetTransactionBridge();
		check(Bridge != nullptr);

		Bridge->RegisterTransactionFilter(NDISPLAY_MULTIUSER_TRANSACTION_FILTER,
			FTransactionFilterDelegate::CreateRaw(this, &FDisplayClusterMultiUserManager::ShouldObjectBeTransacted));
	}
}

FDisplayClusterMultiUserManager::~FDisplayClusterMultiUserManager()
{
	const TSharedPtr<IConcertSyncClient> ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser"));
	if (ConcertSyncClient.IsValid())
	{
		IConcertClientTransactionBridge* Bridge = ConcertSyncClient->GetTransactionBridge();
		check(Bridge != nullptr);

		Bridge->UnregisterTransactionFilter(NDISPLAY_MULTIUSER_TRANSACTION_FILTER);
	}
}

ETransactionFilterResult FDisplayClusterMultiUserManager::ShouldObjectBeTransacted(UObject* InObject, UPackage* InPackage)
{
	if (InObject && InObject->IsA<UDisplayClusterConfigurationData_Base>())
	{
		UE_LOG(LogDisplayClusterMultiUser, Log, TEXT("FDisplayClusterMultiUser transaction for object: %s"), *InObject->GetName());
		return ETransactionFilterResult::IncludeObject;
	}

	return ETransactionFilterResult::UseDefault;
}
