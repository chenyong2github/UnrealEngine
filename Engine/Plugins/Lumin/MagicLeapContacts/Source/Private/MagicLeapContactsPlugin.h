// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Regex.h"
#include "IMagicLeapContactsPlugin.h"
#include "MagicLeapContactsTypes.h"
#include "AppEventHandler.h"
#include "Lumin/CAPIShims/LuminAPIContacts.h"
#include "MagicLeapPluginUtil.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapContacts, Verbose, All);

class FMagicLeapContactsPlugin : public IMagicLeapContactsPlugin
{
public:
	FMagicLeapContactsPlugin();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime) override;

	bool Startup();
	bool Shutdown();
	FGuid AddContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegateMulti& ResultDelegate);
	FGuid EditContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegateMulti& ResultDelegate);
	FGuid DeleteContactAsync(const FMagicLeapContact& Contact, const FSingleContactResultDelegateMulti& ResultDelegate);
	FGuid RequestContactsAsync(const FMultipleContactsResultDelegateMulti& ResultDelegate);
	FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMultipleContactsResultDelegateMulti& ResultDelegate);
	bool CancelRequest(const FGuid& RequestHandle);
	bool SetLogDelegate(const FContactsLogMessageMulti& LogDelegate);

private:
	// JMC TODO: Validation needs to come from the api.
	const FRegexPattern PhoneNumberPattern;
	const FRegexPattern EmailPattern;
	const int32 MAX_NAME_LENGTH;
	const int32 MAX_PHONE_LENGTH;
	const int32 MAX_EMAIL_LENGTH;
	const int32 MAX_TAG_LENGTH;
	const int32 MAX_PHONE_COUNT;
	const int32 MAX_EMAIL_COUNT;

	MagicLeap::IAppEventHandler PrivilegesManager;
	FMagicLeapAPISetup APISetup;
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	struct FContactRequest
	{
		enum class EType
		{
			Add,
			Edit,
			Delete,
			GetAll,
			Search,
			Cancel
		};

		EType Type;
		FMagicLeapContact Contact;
		EMagicLeapPrivilege RequiredPrivilege;
		FSingleContactResultDelegateMulti SingleContactResultDelegate;
		FMultipleContactsResultDelegateMulti MultipleContactsResultDelegate;
#if WITH_MLSDK
		MLHandle Handle;
#endif // WITH_MLSDK
	};
	TArray<FContactRequest> PendingRequests;
	TArray<FContactRequest> ActiveRequests;
	FContactsLogMessageMulti LogDelegate;

#if WITH_MLSDK
	EMagicLeapContactsOperationStatus MLOpStatusToUEOpStatus(MLContactsOperationStatus InMLOpStatus);
	MLContactsSearchField UESearchFieldToMLSearchField(EMagicLeapContactsSearchField InSearchField);
	void UEToMLContact(const FMagicLeapContact& InUEContact, MLContactsContact& OutMLContact);
	void MLToUEContact(const MLContactsContact* InMLContact, FMagicLeapContact& OutUEContact);
	void DestroyMLContact(MLContactsContact& OutMLContact);
	MLHandle FGuidToMLHandle(const FGuid& InGuid);
	FGuid MLHandleToFGuid(const MLHandle& InHandle);
#endif // WITH_MLSDK
	bool ValidateUEContact(const FMagicLeapContact& InUEContact);
	void ForceEmailAddressessToLower(FMagicLeapContact& InUEContact);
	bool TryAddPendingTask(EMagicLeapPrivilege InRequiredPrivilege, FContactRequest::EType InRequestType, const FMagicLeapContact& InContact, const FSingleContactResultDelegateMulti& InResultDelegate);
	bool TryAddPendingTask(EMagicLeapPrivilege InRequiredPrivilege, FContactRequest::EType InRequestType, const FMultipleContactsResultDelegateMulti& InResultDelegate);
	void ProcessPendingRequests();
	void Log(EMagicLeapContactsOperationStatus OpStatus, const FString& LogString);
};

#define GET_MAGIC_LEAP_CONTACTS_PLUGIN() static_cast<FMagicLeapContactsPlugin*>(&IMagicLeapContactsPlugin::Get())
