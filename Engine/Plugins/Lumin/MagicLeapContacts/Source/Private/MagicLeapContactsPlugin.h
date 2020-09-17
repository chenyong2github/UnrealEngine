// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Regex.h"
#include "IMagicLeapContactsPlugin.h"
#include "MagicLeapContactsTypes.h"
#include "AppEventHandler.h"
#include "Lumin/CAPIShims/LuminAPIContacts.h"

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
	FGuid AddContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& ResultDelegate);
	FGuid EditContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& ResultDelegate);
	FGuid DeleteContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& ResultDelegate);
	FGuid RequestContactsAsync(const FMagicLeapMultipleContactsResultDelegateMulti& ResultDelegate, int32 MaxNumResults = 250);
	FGuid SelectContactsAsync(const FMagicLeapMultipleContactsResultDelegateMulti& ResultDelegate, int32 MaxNumResults = 250, EMagicLeapContactsSearchField SelectionField = EMagicLeapContactsSearchField::All);
	FGuid SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMagicLeapMultipleContactsResultDelegateMulti& InResultDelegate);
	bool SetLogDelegate(const FMagicLeapContactsLogMessageMulti& LogDelegate);

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
			Search
		};

		EType Type;
		FMagicLeapContact Contact;
		int32 MaxNumResults;
		EMagicLeapPrivilege RequiredPrivilege;
		FString Query;
		EMagicLeapContactsSearchField SearchField;
		FMagicLeapSingleContactResultDelegateMulti SingleContactResultDelegate;
		FMagicLeapMultipleContactsResultDelegateMulti MultipleContactsResultDelegate;
#if WITH_MLSDK
		MLHandle Handle;
#endif // WITH_MLSDK
	};
	TArray<FContactRequest> PendingRequests;
	TArray<FContactRequest> ActiveRequests;
	FMagicLeapContactsLogMessageMulti LogDelegate;

#if WITH_MLSDK
	EMagicLeapContactsOperationStatus MLOpStatusToUEOpStatus(MLContactsOperationStatus InMLOpStatus);
	MLContactsSearchField UESearchFieldToMLSearchField(EMagicLeapContactsSearchField InSearchField);
	MLContactsSelectionField UESearchFieldToMLSelectionField(EMagicLeapContactsSearchField SearchField);
	void UEToMLContact(const FMagicLeapContact& InUEContact, MLContactsContact& OutMLContact);
	void MLToUEContact(const MLContactsContact* InMLContact, FMagicLeapContact& OutUEContact);
	void DestroyMLContact(MLContactsContact& OutMLContact);
	MLHandle FGuidToMLHandle(const FGuid& InGuid);
	FGuid MLHandleToFGuid(const MLHandle& InHandle);
#endif // WITH_MLSDK
	bool ValidateUEContact(const FMagicLeapContact& InUEContact);
	void ForceEmailAddressessToLower(FMagicLeapContact& InUEContact);
	bool TryAddPendingTask(const FContactRequest& ContactRequest);
	void ProcessPendingRequests();
	void Log(EMagicLeapContactsOperationStatus OpStatus, const FString& LogString);
};

inline FMagicLeapContactsPlugin& GetMagicLeapContactsPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapContactsPlugin>("MagicLeapContacts");
}
