// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapContactsPlugin.h"
#include "Stats/Stats.h"

using namespace MagicLeap;

DEFINE_LOG_CATEGORY(LogMagicLeapContacts);

FMagicLeapContactsPlugin::FMagicLeapContactsPlugin()
: PhoneNumberPattern(TEXT("(^\\s*(?:\\+?(\\d{1,3}))?[-. (]*(\\d{3})[-. )]*(\\d{3})[-. ]*(\\d{4})(?: *x(\\d+))?\\s*$)"))
, EmailPattern(TEXT("((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|""(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*"")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\\]))"))
, MAX_NAME_LENGTH(128)
, MAX_PHONE_LENGTH(20)
, MAX_EMAIL_LENGTH(128)
, MAX_TAG_LENGTH(128)
, MAX_PHONE_COUNT(16)
, MAX_EMAIL_COUNT(32)
, PrivilegesManager({ EMagicLeapPrivilege::AddressBookRead, EMagicLeapPrivilege::AddressBookWrite })
{

}

void FMagicLeapContactsPlugin::StartupModule()
{
	IMagicLeapContactsPlugin::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapContactsPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void FMagicLeapContactsPlugin::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapContactsPlugin::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapContactsPlugin_Tick);

#if WITH_MLSDK
	if (PendingRequests.Num() > 0)
	{
		ProcessPendingRequests();
	}

	if (ActiveRequests.Num() == 0)
	{
		return true;
	}

	MLResult Result = MLResult_Ok;
	for (int32 i = ActiveRequests.Num() - 1; i > -1; --i)
	{
		bool bNeedsRemoving = false;
		const FContactRequest& ContactRequest = ActiveRequests[i];
		if (ContactRequest.SingleContactResultDelegate.IsBound())
		{
			MLContactsOperationResult* OpResult = nullptr;
			Result = MLContactsTryGetOperationResult(ActiveRequests[i].Handle, &OpResult);
			if (Result == MLResult_Pending)
			{
				continue;
			}
			else if (Result == MLContactsResult_Completed)
			{
				ContactRequest.SingleContactResultDelegate.Broadcast(MLOpStatusToUEOpStatus(OpResult->operation_status));
			}
			else
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsTryGetOperationResult failed with error '%s'!"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
			}

			bNeedsRemoving = true;
		}
		else
		{
			MLContactsListResult* ListResult = nullptr;
			Result = MLContactsTryGetListResult(ContactRequest.Handle, &ListResult);
			if (Result == MLResult_Pending)
			{
				continue;
			}
			else if (Result == MLContactsResult_Completed)
			{
				const int32 NumContacts = static_cast<int32>(ListResult->result_list.count);
				TArray<FMagicLeapContact> Contacts;
				Contacts.AddDefaulted(NumContacts);
				for (int32 iContact = 0; iContact < NumContacts; ++iContact)
				{
					FMagicLeapContact& Contact = Contacts[iContact];
					MLToUEContact(ListResult->result_list.contacts[iContact], Contact);
				}

				ContactRequest.MultipleContactsResultDelegate.Broadcast(Contacts, MLOpStatusToUEOpStatus(ListResult->operation_status));
			}
			else
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsTryGetOperationResult failed with error '%s'!"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
			}

			bNeedsRemoving = true;
		}

		if (bNeedsRemoving)
		{
			MLContactsReleaseRequestResources(ContactRequest.Handle);
			ActiveRequests.RemoveAt(i);
		}
	}
#endif // WITH_MLSDK
	return true;
}

bool FMagicLeapContactsPlugin::Startup()
{
#if WITH_MLSDK
	MLResult Result = MLContactsStartup();
	if (MLResult_Ok != Result)
	{
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsStartup failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}

	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapContactsPlugin::Shutdown()
{
#if WITH_MLSDK
	MLResult Result = MLContactsShutdown();
	if (MLResult_Ok != Result)
	{
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsShutdown failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

FGuid FMagicLeapContactsPlugin::AddContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	FContactRequest ContactRequest;
	ContactRequest.Type = FContactRequest::EType::Add;
	ContactRequest.Contact = InContact;
	ContactRequest.RequiredPrivilege = EMagicLeapPrivilege::AddressBookWrite;
	ContactRequest.SingleContactResultDelegate = InResultDelegate;
	if (TryAddPendingTask(ContactRequest))
	{
		return FGuid();
	}

	FMagicLeapContact UEContact = InContact;
	ForceEmailAddressessToLower(UEContact);

	if (!ValidateUEContact(UEContact))
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		return FGuid();
	}

	MLContactsContact MLContact;
	UEToMLContact(UEContact, MLContact);
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestInsert(&MLContact, &RequestHandle);
	DestroyMLContact(MLContact);
	if (Result == MLResult_Ok)
	{
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestInsert failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

FGuid FMagicLeapContactsPlugin::EditContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	FContactRequest ContactRequest;
	ContactRequest.Type = FContactRequest::EType::Edit;
	ContactRequest.Contact = InContact;
	ContactRequest.RequiredPrivilege = EMagicLeapPrivilege::AddressBookWrite;
	ContactRequest.SingleContactResultDelegate = InResultDelegate;
	if (TryAddPendingTask(ContactRequest))
	{
		return FGuid();
	}

	FMagicLeapContact UEContact = InContact;
	ForceEmailAddressessToLower(UEContact);

	if (!ValidateUEContact(UEContact))
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		return FGuid();
	}

	MLContactsContact MLContact;
	UEToMLContact(UEContact, MLContact);
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestUpdate(&MLContact, &RequestHandle);
	DestroyMLContact(MLContact);
	if (Result == MLResult_Ok)
	{
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestUpdate failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

FGuid FMagicLeapContactsPlugin::DeleteContactAsync(const FMagicLeapContact& InContact, const FMagicLeapSingleContactResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	FContactRequest ContactRequest;
	ContactRequest.Type = FContactRequest::EType::Delete;
	ContactRequest.Contact = InContact;
	ContactRequest.RequiredPrivilege = EMagicLeapPrivilege::AddressBookWrite;
	ContactRequest.SingleContactResultDelegate = InResultDelegate;
	if (TryAddPendingTask(ContactRequest))
	{
		return FGuid();
	}

	FMagicLeapContact UEContact = InContact;
	ForceEmailAddressessToLower(UEContact);

	if (!ValidateUEContact(UEContact))
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		return FGuid();
	}

	MLContactsContact MLContact;
	UEToMLContact(UEContact, MLContact);
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestDelete(&MLContact, &RequestHandle);
	DestroyMLContact(MLContact);
	if (Result == MLResult_Ok)
	{
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			InResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestDelete failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

FGuid FMagicLeapContactsPlugin::RequestContactsAsync(const FMagicLeapMultipleContactsResultDelegateMulti& InResultDelegate, int32 MaxNumResults)
{
#if WITH_MLSDK
	FContactRequest ContactRequest;
	ContactRequest.Type = FContactRequest::EType::GetAll;
	ContactRequest.RequiredPrivilege = EMagicLeapPrivilege::AddressBookRead;
	ContactRequest.MultipleContactsResultDelegate = InResultDelegate;
	ContactRequest.MaxNumResults = MaxNumResults <= 0 ? MLContacts_DefaultFetchLimit : MaxNumResults;
	if (TryAddPendingTask(ContactRequest))
	{
		return FGuid();
	}

	MLContactsListArgs Args;
	MLContactsListArgsInit(&Args);
	Args.limit = ContactRequest.MaxNumResults;
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestList(&Args, &RequestHandle);
	if (Result == MLResult_Ok)
	{
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			TArray<FMagicLeapContact> DummyContacts;
			InResultDelegate.Broadcast(DummyContacts, EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestList failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

FGuid FMagicLeapContactsPlugin::SelectContactsAsync(const FMagicLeapMultipleContactsResultDelegateMulti& InResultDelegate, int32 MaxNumResults, EMagicLeapContactsSearchField SelectionField)
{
#if WITH_MLSDK
	MLContactsSelectionArgs Args;
	MLContactsSelectionArgsInit(&Args);
	Args.limit = MaxNumResults <= 0 ? MLContacts_DefaultFetchLimit : MaxNumResults;
	Args.fields = UESearchFieldToMLSelectionField(SelectionField);
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestSelection(&Args, &RequestHandle);
	if (Result == MLResult_Ok)
	{
		FContactRequest ContactRequest;
		ContactRequest.MultipleContactsResultDelegate = InResultDelegate;
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			TArray<FMagicLeapContact> DummyContacts;
			InResultDelegate.Broadcast(DummyContacts, EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestSelection failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

FGuid FMagicLeapContactsPlugin::SearchContactsAsync(const FString& Query, EMagicLeapContactsSearchField SearchField, const FMagicLeapMultipleContactsResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	FContactRequest ContactRequest;
	ContactRequest.Type = FContactRequest::EType::Search;
	ContactRequest.RequiredPrivilege = EMagicLeapPrivilege::AddressBookRead;
	ContactRequest.Query = Query;
	ContactRequest.SearchField = SearchField;
	ContactRequest.MultipleContactsResultDelegate = InResultDelegate;
	if (TryAddPendingTask(ContactRequest))
	{
		return FGuid();
	}

	MLContactsSearchArgs Args;
	MLContactsSearchArgsInit(&Args);
	Args.fields = UESearchFieldToMLSearchField(SearchField);
	Args.query = TCHAR_TO_UTF8(*Query);
	MLHandle RequestHandle;
	MLResult Result = MLContactsRequestSearch(&Args, &RequestHandle);
	if (Result == MLResult_Ok)
	{
		ContactRequest.Handle = RequestHandle;
		ActiveRequests.Add(ContactRequest);
		return MLHandleToFGuid(RequestHandle);
	}
	else
	{
		if (InResultDelegate.IsBound())
		{
			TArray<FMagicLeapContact> DummyContacts;
			InResultDelegate.Broadcast(DummyContacts, EMagicLeapContactsOperationStatus::Fail);
		}
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("MLContactsRequestSearch failed with error '%s'"), UTF8_TO_TCHAR(MLContactsGetResultString(Result))));
	}
#endif // WITH_MLSDK
	return FGuid();
}

bool FMagicLeapContactsPlugin::SetLogDelegate(const FMagicLeapContactsLogMessageMulti& InLogDelegate)
{
	LogDelegate = InLogDelegate;
	return true;
}

#if WITH_MLSDK
EMagicLeapContactsOperationStatus FMagicLeapContactsPlugin::MLOpStatusToUEOpStatus(MLContactsOperationStatus InMLOpStatus)
{
	EMagicLeapContactsOperationStatus Status = EMagicLeapContactsOperationStatus::Fail;

	switch (InMLOpStatus)
	{
	case MLContactsOperationStatus_Success: Status = EMagicLeapContactsOperationStatus::Success; break;
	case MLContactsOperationStatus_Fail: Status = EMagicLeapContactsOperationStatus::Fail; break;
	case MLContactsOperationStatus_Duplicate: Status = EMagicLeapContactsOperationStatus::Duplicate; break;
	case MLContactsOperationStatus_NotFound: Status = EMagicLeapContactsOperationStatus::NotFound; break;
	}

	return Status;
}

MLContactsSearchField FMagicLeapContactsPlugin::UESearchFieldToMLSearchField(EMagicLeapContactsSearchField InSearchField)
{
	MLContactsSearchField SearchField = MLContactsSearchField_All;

	switch (InSearchField)
	{
	case EMagicLeapContactsSearchField::Name: SearchField = MLContactsSearchField_Name; break;
	case EMagicLeapContactsSearchField::Phone: SearchField = MLContactsSearchField_Phone; break;
	case EMagicLeapContactsSearchField::Email: SearchField = MLContactsSearchField_Email; break;
	case EMagicLeapContactsSearchField::All: SearchField = MLContactsSearchField_All; break;
	}

	return SearchField;
}

MLContactsSelectionField FMagicLeapContactsPlugin::UESearchFieldToMLSelectionField(EMagicLeapContactsSearchField SearchField)
{
	MLContactsSelectionField SelectionField = MLContactsSelectionField_All;
	switch (SearchField)
	{
	case EMagicLeapContactsSearchField::Name: SelectionField = MLContactsSelectionField_Name; break;
	case EMagicLeapContactsSearchField::Phone: SelectionField = MLContactsSelectionField_Phone; break;
	case EMagicLeapContactsSearchField::Email: SelectionField = MLContactsSelectionField_Email; break;
	case EMagicLeapContactsSearchField::All: SelectionField = MLContactsSelectionField_All; break;
	}

	return SelectionField;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:6386) /* Buffer overrun while writing to '<array>': the writable size is '<array size>' bytes, but '<write size>' bytes might be written */
							  /* After careful code analysis, we've determine this warning is incorrect in this case, do disabling at function scope. */
#endif

void FMagicLeapContactsPlugin::UEToMLContact(const FMagicLeapContact& InUEContact, MLContactsContact& OutMLContact)
{
	MLContactsContactInit(&OutMLContact);
	OutMLContact.id = static_cast<const char*>(FMemory::Malloc(InUEContact.Id.Len() + 1));
	FMemory::Memzero((void*)OutMLContact.id, InUEContact.Id.Len() + 1);
	FMemory::Memcpy((void*)OutMLContact.id, TCHAR_TO_UTF8(*InUEContact.Id), InUEContact.Id.Len());
	OutMLContact.name = static_cast<const char*>(FMemory::Malloc(InUEContact.Name.Len() + 1));
	FMemory::Memzero((void*)OutMLContact.name, InUEContact.Name.Len() + 1);
	FMemory::Memcpy((void*)OutMLContact.name, TCHAR_TO_UTF8(*InUEContact.Name), InUEContact.Name.Len());

	OutMLContact.phone_number_list.count = static_cast<size_t>(InUEContact.PhoneNumbers.Num());
	MLContactsTaggedAttribute** MLPhoneNumbers = new MLContactsTaggedAttribute*[InUEContact.PhoneNumbers.Num()];
	OutMLContact.phone_number_list.items = MLPhoneNumbers;
	for (int32 iPhoneNumber = 0; iPhoneNumber < InUEContact.PhoneNumbers.Num(); ++iPhoneNumber)
	{
		const FMagicLeapTaggedAttribute& UEPhoneNumber = InUEContact.PhoneNumbers[iPhoneNumber];
		MLContactsTaggedAttribute* MLPhoneNumber = new MLContactsTaggedAttribute;
		MLPhoneNumber->tag = static_cast<const char*>(FMemory::Malloc(UEPhoneNumber.Tag.Len() + 1));
		FMemory::Memzero((void*)MLPhoneNumber->tag, UEPhoneNumber.Tag.Len() + 1);
		FMemory::Memcpy((void*)MLPhoneNumber->tag, TCHAR_TO_UTF8(*UEPhoneNumber.Tag), UEPhoneNumber.Tag.Len());
		MLPhoneNumber->value = static_cast<const char*>(FMemory::Malloc(UEPhoneNumber.Value.Len() + 1));
		FMemory::Memzero((void*)MLPhoneNumber->value, UEPhoneNumber.Value.Len() + 1);
		FMemory::Memcpy((void*)MLPhoneNumber->value, TCHAR_TO_UTF8(*UEPhoneNumber.Value), UEPhoneNumber.Value.Len());
		MLPhoneNumbers[iPhoneNumber] = MLPhoneNumber;
	}

	OutMLContact.email_address_list.count = static_cast<size_t>(InUEContact.EmailAddresses.Num());
	MLContactsTaggedAttribute** MLEmailAddresses = new MLContactsTaggedAttribute*[InUEContact.EmailAddresses.Num()];
	OutMLContact.email_address_list.items = MLEmailAddresses;
	for (int32 iEmailAddress = 0; iEmailAddress < InUEContact.EmailAddresses.Num(); ++iEmailAddress)
	{
		const FMagicLeapTaggedAttribute& UEEmailAddress = InUEContact.EmailAddresses[iEmailAddress];
		MLContactsTaggedAttribute* MLEmailAddress = new MLContactsTaggedAttribute;
		MLEmailAddress->tag = static_cast<const char*>(FMemory::Malloc(UEEmailAddress.Tag.Len() + 1));
		FMemory::Memzero((void*)MLEmailAddress->tag, UEEmailAddress.Tag.Len() + 1);
		FMemory::Memcpy((void*)MLEmailAddress->tag, TCHAR_TO_UTF8(*UEEmailAddress.Tag), UEEmailAddress.Tag.Len());
		MLEmailAddress->value = static_cast<const char*>(FMemory::Malloc(UEEmailAddress.Value.Len() + 1));
		FMemory::Memzero((void*)MLEmailAddress->value, UEEmailAddress.Value.Len() + 1);
		FMemory::Memcpy((void*)MLEmailAddress->value, TCHAR_TO_UTF8(*UEEmailAddress.Value), UEEmailAddress.Value.Len());
		MLEmailAddresses[iEmailAddress] = MLEmailAddress;
	}
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

void FMagicLeapContactsPlugin::DestroyMLContact(MLContactsContact& OutMLContact)
{
	FMemory::Free((void*)OutMLContact.id);
	FMemory::Free((void*)OutMLContact.name);

	for (size_t iPhoneNumber = 0; iPhoneNumber < OutMLContact.phone_number_list.count; ++iPhoneNumber)
	{
		MLContactsTaggedAttribute* MLPhoneNumber = OutMLContact.phone_number_list.items[iPhoneNumber];
		FMemory::Free((void*)MLPhoneNumber->tag);
		FMemory::Free((void*)MLPhoneNumber->value);
		delete MLPhoneNumber;
	}
	delete OutMLContact.phone_number_list.items;

	for (size_t iEmailAddress = 0; iEmailAddress < OutMLContact.email_address_list.count; ++iEmailAddress)
	{
		MLContactsTaggedAttribute* MLEmailAddress = OutMLContact.email_address_list.items[iEmailAddress];
		FMemory::Free((void*)MLEmailAddress->tag);
		FMemory::Free((void*)MLEmailAddress->value);
		delete MLEmailAddress;
	}
	delete OutMLContact.email_address_list.items;
}

void FMagicLeapContactsPlugin::MLToUEContact(const MLContactsContact* InMLContact, FMagicLeapContact& OutUEContact)
{
	OutUEContact.Id = UTF8_TO_TCHAR(InMLContact->id);
	OutUEContact.Name = UTF8_TO_TCHAR(InMLContact->name);

	const int32 NumPhoneNumbers = static_cast<int32>(InMLContact->phone_number_list.count);
	OutUEContact.PhoneNumbers.AddZeroed(NumPhoneNumbers);
	for (int32 iPhoneNumber = 0; iPhoneNumber < NumPhoneNumbers; ++iPhoneNumber)
	{
		FMagicLeapTaggedAttribute& UEPhoneNumber = OutUEContact.PhoneNumbers[iPhoneNumber];
		const MLContactsTaggedAttribute* MLPhoneNumber = InMLContact->phone_number_list.items[iPhoneNumber];
		UEPhoneNumber.Tag = UTF8_TO_TCHAR(MLPhoneNumber->tag);
		UEPhoneNumber.Value = UTF8_TO_TCHAR(MLPhoneNumber->value);
	}

	const int32 NumEmailAddresses = static_cast<int32>(InMLContact->email_address_list.count);
	OutUEContact.EmailAddresses.AddZeroed(NumEmailAddresses);
	for (int32 iEmailAddress = 0; iEmailAddress < NumEmailAddresses; ++iEmailAddress)
	{
		FMagicLeapTaggedAttribute& UEAddress = OutUEContact.EmailAddresses[iEmailAddress];
		const MLContactsTaggedAttribute* MLEmailAddress = InMLContact->email_address_list.items[iEmailAddress];
		UEAddress.Tag = UTF8_TO_TCHAR(MLEmailAddress->tag);
		UEAddress.Value = UTF8_TO_TCHAR(MLEmailAddress->value);
	}
}

MLHandle FMagicLeapContactsPlugin::FGuidToMLHandle(const FGuid& InGuid)
{
	return static_cast<uint64_t>(InGuid.B) << 32 | InGuid.A;
}

FGuid FMagicLeapContactsPlugin::MLHandleToFGuid(const MLHandle& InHandle)
{
	return FGuid(InHandle, InHandle >> 32, 0, 0);
}
#endif // WITH_MLSDK

bool FMagicLeapContactsPlugin::ValidateUEContact(const FMagicLeapContact& InUEContact)
{
	bool bIsValid = true;

	if (InUEContact.Name.IsEmpty())
	{
		Log(EMagicLeapContactsOperationStatus::Fail, TEXT("Contact name is required!"));
		bIsValid = false;
	}
	else if (InUEContact.Name.Len() > MAX_NAME_LENGTH)
	{
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Contact name '%s' is too long. Max length is: %d"), *InUEContact.Name, MAX_NAME_LENGTH));
		bIsValid = false;
	}
	if (0 == InUEContact.PhoneNumbers.Num() && 0 == InUEContact.EmailAddresses.Num())
	{
		Log(EMagicLeapContactsOperationStatus::Fail, TEXT("Either a phone number or e-mail address is required"));
		bIsValid = false;
	}
	if (InUEContact.PhoneNumbers.Num() > MAX_PHONE_COUNT)
	{
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Too many phone numbers have been added. Max count is: %d"), MAX_PHONE_COUNT));
		bIsValid = false;
	}
	else
	{
		for (const FMagicLeapTaggedAttribute& Phone : InUEContact.PhoneNumbers)
		{
			if (!Phone.Tag.IsEmpty() && Phone.Tag.Len() > MAX_TAG_LENGTH)
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Phone Tag '%s' is too long. Limit is: %d"), *Phone.Tag, MAX_TAG_LENGTH));
				bIsValid = false;
			}

			FRegexMatcher PhonePatternMatcher(PhoneNumberPattern, Phone.Value);
			if (Phone.Value.IsEmpty() || !PhonePatternMatcher.FindNext())
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Phone number '%s' is invalid."), *Phone.Value));
				bIsValid = false;
			}
			else if (Phone.Value.Len() > MAX_PHONE_LENGTH)
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Phone number '%s' is too long. Max length is: %d"), *Phone.Value, MAX_PHONE_LENGTH));
				bIsValid = false;
			}
		}
	}
	if (InUEContact.EmailAddresses.Num() > MAX_EMAIL_COUNT)
	{
		Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Too many email addresses have been added. Limit is: %d"), MAX_EMAIL_COUNT));
		bIsValid = false;
	}
	else
	{
		for (const FMagicLeapTaggedAttribute& Email : InUEContact.EmailAddresses)
		{
			if (!Email.Tag.IsEmpty() && Email.Tag.Len() > MAX_TAG_LENGTH)
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Email Tag is too long. Max length is: %d"), MAX_TAG_LENGTH));
				bIsValid = false;
			}
			FRegexMatcher EmailPatternMatcher(EmailPattern, Email.Value);
			if (Email.Value.IsEmpty() || !EmailPatternMatcher.FindNext())
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Email address '%s' is invalid."), *Email.Value));
				bIsValid = false;
			}
			else if (Email.Value.Len() > MAX_EMAIL_LENGTH)
			{
				Log(EMagicLeapContactsOperationStatus::Fail, FString::Printf(TEXT("Email address '%s' is too long.  Max length is %d"), *Email.Value, MAX_EMAIL_LENGTH));
				bIsValid = false;
			}
		}
	}

	return bIsValid;
}

void FMagicLeapContactsPlugin::ForceEmailAddressessToLower(FMagicLeapContact& InUEContact)
{
	for (FMagicLeapTaggedAttribute& Email : InUEContact.EmailAddresses)
	{
		if (!Email.Value.IsEmpty())
		{
			Email.Value.ToLowerInline();
		}
	}
}

bool FMagicLeapContactsPlugin::TryAddPendingTask(const FContactRequest& ContactRequest)
{
	bool bAddedTask = false;
	EPrivilegeState PrivilegeStatus = PrivilegesManager.GetPrivilegeStatus(ContactRequest.RequiredPrivilege, false);
	if (PrivilegeStatus != EPrivilegeState::Granted)
	{
		if (PrivilegeStatus != EPrivilegeState::Pending)
		{
			if (ContactRequest.SingleContactResultDelegate.IsBound())
			{
				ContactRequest.SingleContactResultDelegate.Broadcast(EMagicLeapContactsOperationStatus::Fail);
			}
			else if (ContactRequest.MultipleContactsResultDelegate.IsBound())
			{
				TArray<FMagicLeapContact> DummyContacts;
				ContactRequest.MultipleContactsResultDelegate.Broadcast(DummyContacts, EMagicLeapContactsOperationStatus::Fail);
			}

			FString ErrorMsg = FString::Printf(TEXT("TryAddPendingTask failed due to privilege '%s' having status '%s'"), *PrivilegesManager.PrivilegeToString(ContactRequest.RequiredPrivilege), PrivilegesManager.PrivilegeStateToString(PrivilegeStatus));
			Log(EMagicLeapContactsOperationStatus::Fail, ErrorMsg);
		}
		else
		{
			PendingRequests.Add(ContactRequest);
			bAddedTask = true;
		}
	}

	return bAddedTask;
}

void FMagicLeapContactsPlugin::ProcessPendingRequests()
{
	for (int32 iPendingRequest = PendingRequests.Num() - 1; iPendingRequest > -1; --iPendingRequest)
	{
		const FContactRequest& PendingRequest = PendingRequests[iPendingRequest];
		EPrivilegeState PrivilegeStatus = PrivilegesManager.GetPrivilegeStatus(PendingRequest.RequiredPrivilege, false);

		switch (PrivilegeStatus)
		{
		case EPrivilegeState::NotYetRequested:
		{
			checkf(false, TEXT("Invalid request status 'EPrivilegeState::NotYetRequested' encountered in pending request!"));
		}
		break;

		case EPrivilegeState::Pending:
		{
			continue;
		}
		break;

		case EPrivilegeState::Granted:
		{
			switch (PendingRequest.Type)
			{
			case FContactRequest::EType::Add:
			{
				AddContactAsync(PendingRequest.Contact, PendingRequest.SingleContactResultDelegate);
			}
			break;
			case FContactRequest::EType::Edit:
			{
				EditContactAsync(PendingRequest.Contact, PendingRequest.SingleContactResultDelegate);
			}
			break;
			case FContactRequest::EType::Delete:
			{
				DeleteContactAsync(PendingRequest.Contact, PendingRequest.SingleContactResultDelegate);
			}
			break;
			case FContactRequest::EType::GetAll:
			{
				RequestContactsAsync(PendingRequest.MultipleContactsResultDelegate, PendingRequest.MaxNumResults);
			}
			break;
			case FContactRequest::EType::Search:
			{
				SearchContactsAsync(PendingRequest.Query, PendingRequest.SearchField, PendingRequest.MultipleContactsResultDelegate);
			}
			break;
			}

			PendingRequests.RemoveAt(iPendingRequest);
		}
		break;

		case EPrivilegeState::Denied:
		{
			PendingRequests.RemoveAt(iPendingRequest);
		}
		break;

		case EPrivilegeState::Error:
		{
			PendingRequests.RemoveAt(iPendingRequest);
		}
		break;
		}
	}
}

void FMagicLeapContactsPlugin::Log(EMagicLeapContactsOperationStatus OpStatus, const FString& LogString)
{
	if (OpStatus != EMagicLeapContactsOperationStatus::Success)
	{
		UE_LOG(LogMagicLeapContacts, Error, TEXT("%s"), *LogString);
	}
	else
	{
		UE_LOG(LogMagicLeapContacts, Log, TEXT("%s"), *LogString);
	}

	if (LogDelegate.IsBound())
	{
		LogDelegate.Broadcast(LogString, OpStatus);
	}
}

IMPLEMENT_MODULE(FMagicLeapContactsPlugin, MagicLeapContacts);
