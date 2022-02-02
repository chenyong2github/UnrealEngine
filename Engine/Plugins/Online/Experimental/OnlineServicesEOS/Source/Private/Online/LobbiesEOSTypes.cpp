// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/LobbiesEOSTypes.h"
#include "Online/AuthEOS.h"
#include "Online/LobbiesCommon.h"

namespace UE::Online {

namespace Private {

FString TranslateLobbyId(EOS_LobbyId EOSLobbyId)
{
	return FString(UTF8_TO_TCHAR(EOSLobbyId));
}

FString TranslateLobbyInviteId(const char* InviteId)
{
	return FString(UTF8_TO_TCHAR(InviteId));
}

FLobbyAttributeId TranslateLobbyAttributeId(const char* AttributeId)
{
	return FLobbyAttributeId(UTF8_TO_TCHAR(AttributeId));
}

EOS_EComparisonOp TranslateSearchComparison(ELobbyComparisonOp Op)
{
	switch (Op)
	{
	default:									checkNoEntry(); // Intentional fallthrough
	case ELobbyComparisonOp::Equals:			return EOS_EComparisonOp::EOS_CO_EQUAL;
	case ELobbyComparisonOp::NotEquals:			return EOS_EComparisonOp::EOS_CO_NOTEQUAL;
	case ELobbyComparisonOp::GreaterThan:		return EOS_EComparisonOp::EOS_CO_GREATERTHAN;
	case ELobbyComparisonOp::GreaterThanEquals:	return EOS_EComparisonOp::EOS_CO_GREATERTHANOREQUAL;
	case ELobbyComparisonOp::LessThan:			return EOS_EComparisonOp::EOS_CO_LESSTHAN;
	case ELobbyComparisonOp::LessThanEquals:	return EOS_EComparisonOp::EOS_CO_LESSTHANOREQUAL;
	case ELobbyComparisonOp::Near:				return EOS_EComparisonOp::EOS_CO_DISTANCE;
	case ELobbyComparisonOp::In:				return EOS_EComparisonOp::EOS_CO_ONEOF;
	case ELobbyComparisonOp::NotIn:				return EOS_EComparisonOp::EOS_CO_NOTANYOF;

	// todo:
	// EOS_EComparisonOp::EOS_CO_ANYOF
	// EOS_EComparisonOp::EOS_CO_NOTONEOF
	// EOS_EComparisonOp::EOS_CO_CONTAINS
	}
}

} // Private

const FString FLobbyBucketIdEOS::Separator = TEXT("|");

FLobbyBucketIdEOS::FLobbyBucketIdEOS(FString ProductName, int32 ProductVersion)
	: ProductName(ProductName.Replace(*Separator, TEXT("_")))
	, ProductVersion(ProductVersion)
{
}

// Attribute translators.
FLobbyAttributeTranslator<ELobbyTranslationType::ToService>::FLobbyAttributeTranslator(const TPair<FLobbyAttributeId, FLobbyVariant>& FromAttributeData)
	: FLobbyAttributeTranslator(FromAttributeData.Key, FromAttributeData.Value)
{
}

FLobbyAttributeTranslator<ELobbyTranslationType::ToService>::FLobbyAttributeTranslator(FLobbyAttributeId FromAttributeId, const FLobbyVariant& FromAttributeData)
	: KeyConverterStorage(*FromAttributeId.ToString())
{
	AttributeData.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
	AttributeData.Key = KeyConverterStorage.Get();

	if (FromAttributeData.VariantData.IsType<FString>())
	{
		ValueConverterStorage.Emplace(*FromAttributeData.VariantData.Get<FString>());
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
		AttributeData.Value.AsUtf8 = ValueConverterStorage->Get();
	}
	else if (FromAttributeData.VariantData.IsType<int64>())
	{
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_INT64;
		AttributeData.Value.AsInt64 = FromAttributeData.VariantData.Get<int64>();
	}
	else if (FromAttributeData.VariantData.IsType<double>())
	{
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_DOUBLE;
		AttributeData.Value.AsDouble = FromAttributeData.VariantData.Get<double>();
	}
	else if (FromAttributeData.VariantData.IsType<bool>())
	{
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_BOOLEAN;
		AttributeData.Value.AsBool = FromAttributeData.VariantData.Get<bool>();
	}
}

FLobbyAttributeTranslator<ELobbyTranslationType::FromService>::FLobbyAttributeTranslator(const EOS_Lobby_AttributeData& FromAttributeData)
{
	FLobbyAttributeId AttributeId = Private::TranslateLobbyAttributeId(FromAttributeData.Key);
	FLobbyVariant VariantData;

	switch (FromAttributeData.ValueType)
	{
	case EOS_ELobbyAttributeType::EOS_AT_BOOLEAN:
		VariantData.Set(FromAttributeData.Value.AsBool != 0);
		break;

	case EOS_ELobbyAttributeType::EOS_AT_INT64:
		VariantData.Set(static_cast<int64>(FromAttributeData.Value.AsInt64));
		break;

	case EOS_ELobbyAttributeType::EOS_AT_DOUBLE:
		VariantData.Set(FromAttributeData.Value.AsDouble);
		break;

	case EOS_ELobbyAttributeType::EOS_AT_STRING:
		VariantData.Set(UTF8_TO_TCHAR(FromAttributeData.Value.AsUtf8));
		break;

	default:
		checkNoEntry();
		break;
	}

	AttributeData = TPair<FLobbyAttributeId, FLobbyVariant>(MoveTemp(AttributeId), MoveTemp(VariantData));
}

FLobbyBucketIdTranslator<ELobbyTranslationType::ToService>::FLobbyBucketIdTranslator(const FLobbyBucketIdEOS& BucketId)
	: BucketConverterStorage(*FString::Printf(TEXT("%s%s%d"), *BucketId.GetProductName(), *FLobbyBucketIdEOS::Separator, BucketId.GetProductVersion()))
{
}

FLobbyBucketIdTranslator<ELobbyTranslationType::FromService>::FLobbyBucketIdTranslator(const char* BucketIdEOS)
{
	FUTF8ToTCHAR BucketConverterStorage(BucketIdEOS);
	FString BucketString(BucketConverterStorage.Get());

	constexpr int32 ExpectedPartsNum = 2;
	TArray<FString> Parts;
	if (BucketString.ParseIntoArray(Parts, *FLobbyBucketIdEOS::Separator) == ExpectedPartsNum)
	{
		int32 BuildId = 0;
		::LexFromString(BuildId, *Parts[1]);
		BucketId = FLobbyBucketIdEOS(Parts[0], BuildId);
	}
}

const EOS_HLobbyDetails FLobbyDetailsEOS::InvalidLobbyDetailsHandle = {};

TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromLobbyId(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	FOnlineAccountIdHandle LocalUserId,
	EOS_LobbyId LobbyId)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
	Options.LobbyId = LobbyId;
	Options.LocalUserId = GetProductUserIdChecked(LocalUserId);

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandle(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue(), LocalUserId, ELobbyDetailsSource::Active, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromInviteId(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	FOnlineAccountIdHandle LocalUserId,
	const char* InviteId)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYINVITEID_API_LATEST;
	Options.InviteId = InviteId;

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandleByInviteId(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue(), LocalUserId, ELobbyDetailsSource::Invite, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromUiEventId(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	FOnlineAccountIdHandle LocalUserId,
	EOS_UI_EventId UiEventId)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYUIEVENTID_API_LATEST;
	Options.UiEventId = UiEventId;

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue(), LocalUserId, ELobbyDetailsSource::UiEvent, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromSearchResult(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	FOnlineAccountIdHandle LocalUserId,
	EOS_HLobbySearch SearchHandle,
	uint32_t ResultIndex)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_LobbySearch_CopySearchResultByIndexOptions Options;
	Options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
	Options.LobbyIndex = ResultIndex;

	EOS_EResult EOSResult = EOS_LobbySearch_CopySearchResultByIndex(SearchHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue(), LocalUserId, ELobbyDetailsSource::Search, LobbyDetailsHandle));
}

FLobbyDetailsEOS::~FLobbyDetailsEOS()
{
	EOS_LobbyDetails_Release(LobbyDetailsHandle);
}

TFuture<TDefaultErrorResultInternal<TSharedRef<FClientLobbySnapshot>>> FLobbyDetailsEOS::GetLobbySnapshot() const
{
	TSharedPtr<FAuthEOS> AuthInterface = Prerequisites->AuthInterface.Pin();
	if (!AuthInterface)
	{
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FClientLobbySnapshot>>>(Errors::MissingInterface()).GetFuture();
	}

	EOS_LobbyDetails_GetMemberCountOptions GetMemberCountOptions = {};
	GetMemberCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
	const uint32_t MemberCount = EOS_LobbyDetails_GetMemberCount(LobbyDetailsHandle, &GetMemberCountOptions);

	TSharedRef<TArray<EOS_ProductUserId>> MemberProductUserIds = MakeShared<TArray<EOS_ProductUserId>>();
	MemberProductUserIds->Reserve(MemberCount);

	for (uint32_t MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
	{
		EOS_LobbyDetails_GetMemberByIndexOptions GetMemberByIndexOptions = {};
		GetMemberByIndexOptions.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;
		GetMemberByIndexOptions.MemberIndex = MemberIndex;
		MemberProductUserIds->Emplace(EOS_LobbyDetails_GetMemberByIndex(LobbyDetailsHandle, &GetMemberByIndexOptions));
	}

	TPromise<TDefaultErrorResultInternal<TSharedRef<FClientLobbySnapshot>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedRef<FClientLobbySnapshot>>> Future = Promise.GetFuture();

	// Resolve lobby member product user ids to FOnlineAccountIdHandle before proceeding.
	AuthInterface->ResolveAccountIds(AssociatedLocalUser, *MemberProductUserIds)
	.Then([StrongThis = AsShared(), Promise = MoveTemp(Promise), MemberProductUserIds](TFuture<TArray<FOnlineAccountIdHandle>>&& Future) mutable
	{
		const TArray<FOnlineAccountIdHandle>& ResolvedAccountIds = Future.Get();
		if (MemberProductUserIds->Num() != ResolvedAccountIds.Num())
		{
			// Todo: Errors
			Promise.EmplaceValue(Errors::Unknown());
			return;
		}

		TSharedRef<FClientLobbySnapshot> ClientLobbySnapshot = MakeShared<FClientLobbySnapshot>();
		ClientLobbySnapshot->MaxMembers = StrongThis->GetInfo()->GetMaxMembers();
		ClientLobbySnapshot->JoinPolicy = TranslateJoinPolicy(StrongThis->GetInfo()->GetPermissionLevel());

		// Resolve member info.
		{
			EOS_LobbyDetails_GetLobbyOwnerOptions GetLobbyOwnerOptions = {};
			GetLobbyOwnerOptions.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
			const EOS_ProductUserId LobbyOwner = EOS_LobbyDetails_GetLobbyOwner(StrongThis->LobbyDetailsHandle, &GetLobbyOwnerOptions);

			for (int32 MemberIndex = 0; MemberIndex < MemberProductUserIds->Num(); ++MemberIndex)
			{
				const EOS_ProductUserId MemberProductUserId = (*MemberProductUserIds)[MemberIndex];
				const FOnlineAccountIdHandle ResolvedMemberAccountId = ResolvedAccountIds[MemberIndex];

				if (MemberProductUserId == LobbyOwner)
				{
					ClientLobbySnapshot->OwnerAccountId = ResolvedMemberAccountId;
				}

				ClientLobbySnapshot->Members.Add(ResolvedMemberAccountId);
			}
		}

		// Resolve lobby attributes
		{
			EOS_LobbyDetails_GetAttributeCountOptions GetAttributeCountOptions = {};
			GetAttributeCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETATTRIBUTECOUNT_API_LATEST;

			const uint32_t AttributeCount = EOS_LobbyDetails_GetAttributeCount(StrongThis->LobbyDetailsHandle, &GetAttributeCountOptions);
			for (uint32_t AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
			{
				EOS_LobbyDetails_CopyAttributeByIndexOptions CopyAttributeByIndexOptions = {};
				CopyAttributeByIndexOptions.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYINDEX_API_LATEST;
				CopyAttributeByIndexOptions.AttrIndex = AttributeIndex;

				EOS_Lobby_Attribute* LobbyAttribute = nullptr;
				ON_SCOPE_EXIT
				{
					EOS_Lobby_Attribute_Release(LobbyAttribute);
				};

				EOS_EResult EOSResult = EOS_LobbyDetails_CopyAttributeByIndex(StrongThis->LobbyDetailsHandle, &CopyAttributeByIndexOptions, &LobbyAttribute);
				if (EOSResult != EOS_EResult::EOS_Success)
				{
					// todo: errors
					Promise.EmplaceValue(FromEOSError(EOSResult));
					return;
				}

				FLobbyAttributeTranslator<ELobbyTranslationType::FromService> AttributeTranslator(*LobbyAttribute->Data);
				ClientLobbySnapshot->Attributes.Add(MoveTemp(AttributeTranslator.GetMutableAttributeData()));
			}
		}

		Promise.EmplaceValue(MoveTemp(ClientLobbySnapshot));
	});

	return Future;
}

TDefaultErrorResultInternal<TSharedRef<FClientLobbyMemberSnapshot>> FLobbyDetailsEOS::GetLobbyMemberSnapshot(FOnlineAccountIdHandle MemberAccountId) const
{
	EOS_ProductUserId MemberProductUserId = GetProductUserIdChecked(MemberAccountId);

	TSharedRef<FClientLobbyMemberSnapshot> LobbyMemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
	LobbyMemberSnapshot->AccountId = MemberAccountId;
	// Todo: 
	//ClientMemberData->PlatformAccountId;
	//ClientMemberData->PlatformDisplayName;

	// Fetch attributes.
	{
		EOS_LobbyDetails_GetMemberAttributeCountOptions GetMemberAttributeCountOptions = {};
		GetMemberAttributeCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERATTRIBUTECOUNT_API_LATEST;
		GetMemberAttributeCountOptions.TargetUserId = MemberProductUserId;

		const uint32_t MemberAttributeCount = EOS_LobbyDetails_GetMemberAttributeCount(LobbyDetailsHandle, &GetMemberAttributeCountOptions);
		for (uint32_t MemberAttributeIndex = 0; MemberAttributeIndex < MemberAttributeCount; ++MemberAttributeIndex)
		{
			EOS_LobbyDetails_CopyMemberAttributeByIndexOptions CopyMemberAttributeByIndexOptions = {};
			CopyMemberAttributeByIndexOptions.ApiVersion = EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYINDEX_API_LATEST;
			CopyMemberAttributeByIndexOptions.TargetUserId = MemberProductUserId;
			CopyMemberAttributeByIndexOptions.AttrIndex = MemberAttributeIndex;

			EOS_Lobby_Attribute* LobbyAttribute = nullptr;
			ON_SCOPE_EXIT
			{
				EOS_Lobby_Attribute_Release(LobbyAttribute);
			};

			EOS_EResult EOSResult = EOS_LobbyDetails_CopyMemberAttributeByIndex(LobbyDetailsHandle, &CopyMemberAttributeByIndexOptions, &LobbyAttribute);
			if (EOSResult != EOS_EResult::EOS_Success)
			{
				return TDefaultErrorResultInternal<TSharedRef<FClientLobbyMemberSnapshot>>(FromEOSError(EOSResult));
			}

			FLobbyAttributeTranslator<ELobbyTranslationType::FromService> AttributeTranslator(*LobbyAttribute->Data);
			LobbyMemberSnapshot->Attributes.Add(MoveTemp(AttributeTranslator.GetMutableAttributeData()));
		}
	}

	return TDefaultErrorResultInternal<TSharedRef<FClientLobbyMemberSnapshot>>(MoveTemp(LobbyMemberSnapshot));
}

TFuture<EOS_EResult> FLobbyDetailsEOS::ApplyLobbyDataUpdateFromLocalChanges(
	FOnlineAccountIdHandle LocalUserId,
	const FClientLobbyDataChanges& Changes) const
{
	EOS_HLobbyModification LobbyModificationHandle = {};

	ON_SCOPE_EXIT
	{
		EOS_LobbyModification_Release(LobbyModificationHandle);
	};

	// Create lobby modification handle.
	EOS_Lobby_UpdateLobbyModificationOptions ModificationOptions = {};
	ModificationOptions.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
	ModificationOptions.LocalUserId = GetProductUserIdChecked(LocalUserId);
	ModificationOptions.LobbyId = GetInfo()->GetLobbyId();

	EOS_EResult EOSResultCode = EOS_Lobby_UpdateLobbyModification(Prerequisites->LobbyInterfaceHandle, &ModificationOptions, &LobbyModificationHandle);
	if (EOSResultCode != EOS_EResult::EOS_Success)
	{
		// Todo: Errors
		return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
	}

	if (Changes.JoinPolicy)
	{
		// Set lobby join policy.
		EOS_LobbyModification_SetPermissionLevelOptions SetPermissionOptions = {};
		SetPermissionOptions.ApiVersion = EOS_LOBBYMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
		SetPermissionOptions.PermissionLevel = TranslateJoinPolicy(*Changes.JoinPolicy);

		EOSResultCode = EOS_LobbyModification_SetPermissionLevel(LobbyModificationHandle, &SetPermissionOptions);
		if (EOSResultCode != EOS_EResult::EOS_Success)
		{
			// Todo: Errors
			return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
		}
	}

	// Add attributes.
	for (const TPair<FLobbyAttributeId, FLobbyVariant>& MutatedAttribute : Changes.MutatedAttributes)
	{
		const FLobbyAttributeTranslator<ELobbyTranslationType::ToService> AttributeTranslator(MutatedAttribute);

		EOS_LobbyModification_AddAttributeOptions AddAttributeOptions = {};
		AddAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
		AddAttributeOptions.Attribute = &AttributeTranslator.GetAttributeData();
		AddAttributeOptions.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC; // todo - get from schema
		EOSResultCode = EOS_LobbyModification_AddAttribute(LobbyModificationHandle, &AddAttributeOptions);
		if (EOSResultCode != EOS_EResult::EOS_Success)
		{
			// Todo: Errors
			return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
		}
	}

	// Remove attributes.
	for (const FLobbyAttributeId& ClearedAttribute : Changes.ClearedAttributes)
	{
		const FTCHARToUTF8 KeyConverter(*ClearedAttribute.ToString());

		EOS_LobbyModification_RemoveAttributeOptions RemoveAttributeOptions = {};
		RemoveAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_REMOVEATTRIBUTE_API_LATEST;
		RemoveAttributeOptions.Key = KeyConverter.Get();
		EOSResultCode = EOS_LobbyModification_RemoveAttribute(LobbyModificationHandle, &RemoveAttributeOptions);
		if (EOSResultCode != EOS_EResult::EOS_Success)
		{
			// Todo: Errors
			return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
		}
	}

	TPromise<EOS_EResult> Promise;
	TFuture<EOS_EResult> Future = Promise.GetFuture();

	// Apply lobby updates.
	EOS_Lobby_UpdateLobbyOptions UpdateLobbyOptions = {};
	UpdateLobbyOptions.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
	UpdateLobbyOptions.LobbyModificationHandle = LobbyModificationHandle;

	EOS_Async<EOS_Lobby_UpdateLobbyCallbackInfo>(EOS_Lobby_UpdateLobby, Prerequisites->LobbyInterfaceHandle, UpdateLobbyOptions)
	.Then([Promise = MoveTemp(Promise)](TFuture<const EOS_Lobby_UpdateLobbyCallbackInfo*> Future) mutable
	{
		Promise.EmplaceValue(Future.Get()->ResultCode);
	});

	return Future;
}

TFuture<EOS_EResult> FLobbyDetailsEOS::ApplyLobbyMemberDataUpdateFromLocalChanges(
	FOnlineAccountIdHandle LocalUserId,
	const FClientLobbyMemberDataChanges& Changes) const
{
	EOS_HLobbyModification LobbyModificationHandle = {};

	ON_SCOPE_EXIT
	{
		EOS_LobbyModification_Release(LobbyModificationHandle);
	};

	// Create lobby modification handle.
	EOS_Lobby_UpdateLobbyModificationOptions ModificationOptions = {};
	ModificationOptions.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
	ModificationOptions.LocalUserId = GetProductUserIdChecked(LocalUserId);
	ModificationOptions.LobbyId = GetInfo()->GetLobbyId();

	EOS_EResult EOSResultCode = EOS_Lobby_UpdateLobbyModification(Prerequisites->LobbyInterfaceHandle, &ModificationOptions, &LobbyModificationHandle);
	if (EOSResultCode != EOS_EResult::EOS_Success)
	{
		// Todo: Errors
		return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
	}

	// Add member attributes.
	for (const TPair<FLobbyAttributeId, FLobbyVariant>& MutatedAttribute : Changes.MutatedAttributes)
	{
		const FLobbyAttributeTranslator<ELobbyTranslationType::ToService> AttributeTranslator(MutatedAttribute);

		EOS_LobbyModification_AddMemberAttributeOptions AddMemberAttributeOptions = {};
		AddMemberAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST;
		AddMemberAttributeOptions.Attribute = &AttributeTranslator.GetAttributeData();
		AddMemberAttributeOptions.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC; // todo - get from schema
		EOSResultCode = EOS_LobbyModification_AddMemberAttribute(LobbyModificationHandle, &AddMemberAttributeOptions);
		if (EOSResultCode != EOS_EResult::EOS_Success)
		{
			// Todo: Errors
			return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
		}
	}

	// Remove member attributes.
	for (const FLobbyAttributeId& ClearedAttribute : Changes.ClearedAttributes)
	{
		const FTCHARToUTF8 KeyConverter(*ClearedAttribute.ToString());

		EOS_LobbyModification_RemoveMemberAttributeOptions RemoveMemberAttributeOptions = {};
		RemoveMemberAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_REMOVEMEMBERATTRIBUTE_API_LATEST;
		RemoveMemberAttributeOptions.Key = KeyConverter.Get();
		EOSResultCode = EOS_LobbyModification_RemoveMemberAttribute(LobbyModificationHandle, &RemoveMemberAttributeOptions);
		if (EOSResultCode != EOS_EResult::EOS_Success)
		{
			// Todo: Errors
			return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
		}
	}

	// Apply lobby updates.
	EOS_Lobby_UpdateLobbyOptions UpdateLobbyOptions = {};
	UpdateLobbyOptions.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
	UpdateLobbyOptions.LobbyModificationHandle = LobbyModificationHandle;

	TPromise<EOS_EResult> Promise;
	TFuture<EOS_EResult> Future = Promise.GetFuture();

	EOS_Async<EOS_Lobby_UpdateLobbyCallbackInfo>(EOS_Lobby_UpdateLobby, Prerequisites->LobbyInterfaceHandle, UpdateLobbyOptions)
	.Then([Promise = MoveTemp(Promise)](TFuture<const EOS_Lobby_UpdateLobbyCallbackInfo*> Future) mutable
	{
		Promise.EmplaceValue(Future.Get()->ResultCode);
	});

	return Future;
}

FLobbyDetailsEOS::FLobbyDetailsEOS(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	const TSharedRef<FLobbyDetailsInfoEOS>& LobbyDetailsInfo,
	FOnlineAccountIdHandle LocalUserId,
	ELobbyDetailsSource LobbyDetailsSource,
	EOS_HLobbyDetails LobbyDetailsHandle)
	: Prerequisites(Prerequisites)
	, LobbyDetailsInfo(LobbyDetailsInfo)
	, AssociatedLocalUser(LocalUserId)
	, LobbyDetailsSource(LobbyDetailsSource)
	, LobbyDetailsHandle(LobbyDetailsHandle)
{
}

TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>> FLobbyDetailsInfoEOS::Create(EOS_HLobbyDetails LobbyDetailsHandle)
{
	EOS_LobbyDetails_CopyInfoOptions CopyInfoOptions = {};
	CopyInfoOptions.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;

	EOS_LobbyDetails_Info* LobbyDetailsInfo = nullptr;
	EOS_EResult EOSResult = EOS_LobbyDetails_CopyInfo(LobbyDetailsHandle, &CopyInfoOptions, &LobbyDetailsInfo);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>>(FromEOSError(EOSResult));
	}

	return TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsInfoEOS>>(MakeShared<FLobbyDetailsInfoEOS>(FLobbyDetailsInfoPtr(LobbyDetailsInfo)));
}

FLobbyDetailsInfoEOS::FLobbyDetailsInfoEOS(FLobbyDetailsInfoPtr&& InLobbyDetailsInfo)
	: LobbyDetailsInfo(MoveTempIfPossible(InLobbyDetailsInfo))
{
	const FLobbyBucketIdTranslator<ELobbyTranslationType::FromService> BucketTranslator(LobbyDetailsInfo->BucketId);
	BucketId = BucketTranslator.GetBucketId();

	if (!BucketId.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FLobbyDetailsInfoEOS] Failed to parse lobby bucket id. Lobby: %s, Bucket: %s"), UTF8_TO_TCHAR(LobbyDetailsInfo->LobbyId), UTF8_TO_TCHAR(LobbyDetailsInfo->BucketId));
	}
}

FLobbyDataEOS::~FLobbyDataEOS()
{
	if (UnregisterFn)
	{
		UnregisterFn(ClientLobbyData->GetPublicData().LobbyId);
	}
}

void FLobbyDataEOS::AddUserLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedPtr<FLobbyDetailsEOS>& LobbyDetails)
{
	if (TSharedPtr<FLobbyDetailsEOS> ExistingDetails = GetUserLobbyDetails(LocalUserId))
	{
		if (ExistingDetails->GetDetailsSource() < LobbyDetails->GetDetailsSource())
		{
			return;
		}
	}

	UserLobbyDetails.Add(LocalUserId, LobbyDetails);
}

TSharedPtr<FLobbyDetailsEOS> FLobbyDataEOS::GetUserLobbyDetails(FOnlineAccountIdHandle LocalUserId) const
{
	const TSharedPtr<FLobbyDetailsEOS>* Result = UserLobbyDetails.Find(LocalUserId);
	return Result ? *Result : TSharedPtr<FLobbyDetailsEOS>();
}

TSharedPtr<FLobbyDetailsEOS> FLobbyDataEOS::GetActiveLobbyDetails() const
{
	TSharedPtr<FLobbyDetailsEOS> FoundDetails;

	for (const TPair<FOnlineAccountIdHandle, TSharedPtr<FLobbyDetailsEOS>>& LobbyDetails : UserLobbyDetails)
	{
		if (LobbyDetails.Value->GetDetailsSource() == ELobbyDetailsSource::Active)
		{
			FoundDetails = LobbyDetails.Value;
			break;
		}
	}

	return FoundDetails;
}

FLobbyDataEOS::FLobbyDataEOS(
	const TSharedRef<FClientLobbyData>& ClientLobbyData,
	const TSharedRef<FLobbyDetailsInfoEOS>& LobbyDetailsInfo,
	FUnregisterFn UnregisterFn)
	: ClientLobbyData(ClientLobbyData)
	, LobbyDetailsInfo(LobbyDetailsInfo)
	, UnregisterFn(MoveTemp(UnregisterFn))
	, LobbyId(Private::TranslateLobbyId(LobbyDetailsInfo->GetLobbyId()))
{
}

TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> FLobbyDataEOS::Create(
	FOnlineLobbyIdHandle LobbyIdHandle,
	const TSharedRef<FLobbyDetailsEOS>& LobbyDetails,
	FUnregisterFn UnregisterFn)
{
	TPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> Future = Promise.GetFuture();
	
	LobbyDetails->GetLobbySnapshot()
	.Then(
	[
		Promise = MoveTemp(Promise),
		LobbyIdHandle,
		LobbyDetails,
		UnregisterFn = MoveTemp(UnregisterFn)
	]
	(TFuture<TDefaultErrorResultInternal<TSharedRef<FClientLobbySnapshot>>>&& Future) mutable
	{
		if (Future.Get().IsError())
		{
			// todo: errors.
			Promise.EmplaceValue(MoveTemp(Future.Get().GetErrorValue()));
			return;
		}

		TSharedRef<FClientLobbySnapshot> LobbySnapshot = Future.Get().GetOkValue();
		TSharedRef<FClientLobbyData> LobbyData = MakeShared<FClientLobbyData>(LobbyIdHandle);

		// Fetch member data and apply them to the lobby.
		TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> MemberSnapshots;
		for (FOnlineAccountIdHandle MemberAccountId : LobbySnapshot->Members)
		{
			TDefaultErrorResultInternal<TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshotResult = LobbyDetails->GetLobbyMemberSnapshot(MemberAccountId);
			if (LobbyMemberSnapshotResult.IsError())
			{
				// todo: errors.
				Promise.EmplaceValue(MoveTemp(LobbyMemberSnapshotResult.GetErrorValue()));
				return;
			}

			MemberSnapshots.Emplace(MemberAccountId, MoveTemp(LobbyMemberSnapshotResult.GetOkValue()));
		}

		LobbyData->ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(*LobbySnapshot), MoveTemp(MemberSnapshots));

		Promise.EmplaceValue(MakeShared<FLobbyDataEOS>(
			LobbyData,
			LobbyDetails->GetInfo(),
			MoveTemp(UnregisterFn)));
	});

	return Future;
}

FLobbyDataRegistryEOS::FLobbyDataRegistryEOS(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites)
	: Prerequisites(Prerequisites)
{
}

TSharedPtr<FLobbyDataEOS> FLobbyDataRegistryEOS::Find(EOS_LobbyId EOSLobbyId) const
{
	const TWeakPtr<FLobbyDataEOS>* Result = LobbyIdIndex.Find(Private::TranslateLobbyId(EOSLobbyId));
	return Result ? Result->Pin() : TSharedPtr<FLobbyDataEOS>();
}

TSharedPtr<FLobbyDataEOS> FLobbyDataRegistryEOS::Find(FOnlineLobbyIdHandle LobbyIdHandle) const
{
	const TWeakPtr<FLobbyDataEOS>* Result = LobbyIdHandleIndex.Find(LobbyIdHandle);
	return Result ? Result->Pin() : TSharedPtr<FLobbyDataEOS>();
}

TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> FLobbyDataRegistryEOS::FindOrCreateFromLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDetailsEOS>& LobbyDetails)
{
	if (TSharedPtr<FLobbyDataEOS> FindResult = Find(LobbyDetails->GetInfo()->GetLobbyId()))
	{
		FindResult->AddUserLobbyDetails(LocalUserId, LobbyDetails);
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>(FindResult.ToSharedRef()).GetFuture();
	}

	TPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> Future = Promise.GetFuture();

	const FOnlineLobbyIdHandle LobbyId = FOnlineLobbyIdHandle(EOnlineServices::Epic, NextHandleIndex++);
	FLobbyDataEOS::Create(LobbyId, LobbyDetails, MakeUnregisterFn())
	.Then([WeakThis = AsWeak(), Promise = MoveTemp(Promise), LocalUserId, LobbyDetails](TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>&& Future) mutable
	{
		if (TSharedPtr<FLobbyDataRegistryEOS> StrongThis = WeakThis.Pin())
		{
			if (Future.Get().IsOk())
			{
				StrongThis->Register(Future.Get().GetOkValue());
			}
		}

		Future.Get().GetOkValue()->AddUserLobbyDetails(LocalUserId, LobbyDetails);
		Promise.EmplaceValue(MoveTempIfPossible(Future.Get()));
	});

	return Future;
}

void FLobbyDataRegistryEOS::Register(const TSharedRef<FLobbyDataEOS>& LobbyIdHandleData)
{
	LobbyIdIndex.Add(LobbyIdHandleData->GetLobbyId(), LobbyIdHandleData);
	LobbyIdHandleIndex.Add(LobbyIdHandleData->GetLobbyIdHandle(), LobbyIdHandleData);
}

void FLobbyDataRegistryEOS::Unregister(FOnlineLobbyIdHandle LobbyIdHandle)
{
	if (TSharedPtr<FLobbyDataEOS> HandleData = Find(LobbyIdHandle))
	{
		LobbyIdIndex.Remove(HandleData->GetLobbyId());
		LobbyIdHandleIndex.Remove(HandleData->GetLobbyIdHandle());
	}
}

FLobbyDataEOS::FUnregisterFn FLobbyDataRegistryEOS::MakeUnregisterFn()
{
	return [WeakThis = AsWeak()](FOnlineLobbyIdHandle LobbyId)
	{
		if (TSharedPtr<FLobbyDataRegistryEOS> StrongThis = WeakThis.Pin())
		{
			StrongThis->Unregister(LobbyId);
		}
	};
}

TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyInviteDataEOS>>> FLobbyInviteDataEOS::CreateFromInviteId(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	const TSharedRef<FLobbyDataRegistryEOS>& LobbyDataRegistry,
	FOnlineAccountIdHandle LocalUserId,
	const char* InviteIdEOS,
	EOS_ProductUserId Sender)
{
	TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> LobbyDetailsResult = FLobbyDetailsEOS::CreateFromInviteId(Prerequisites, LocalUserId, InviteIdEOS);
	if (LobbyDetailsResult.IsError())
	{
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyInviteDataEOS>>>(MoveTemp(LobbyDetailsResult.GetErrorValue())).GetFuture();
	}

	TSharedRef<FLobbyDetailsEOS> LobbyDetails = LobbyDetailsResult.GetOkValue();
	TPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyInviteDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyInviteDataEOS>>> Future = Promise.GetFuture();

	// Search for existing lobby data so that the LobbyIdHandle will match.
	TSharedRef<FLobbyInviteIdEOS> InviteId = MakeShared<FLobbyInviteIdEOS>(InviteIdEOS);
	LobbyDataRegistry->FindOrCreateFromLobbyDetails(LocalUserId, LobbyDetails)
	.Then([Promise = MoveTemp(Promise), InviteId, LocalUserId, Sender, LobbyDetails](TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>&& Future) mutable
	{
		if (Future.Get().IsError())
		{
			Promise.EmplaceValue(MoveTemp(Future.Get().GetErrorValue()));
			return;
		}

		// Once the lobby data has been resolved the FOnlineAccountIdHandle for the sender is expected to be in the AccountID cache.
		const FOnlineAccountIdHandle SenderUserId = FindAccountId(Sender);
		if (!SenderUserId.IsValid())
		{
			// Todo: Errors.
			Promise.EmplaceValue(MoveTemp(Future.Get().GetErrorValue()));
			return;
		}

		Promise.EmplaceValue(MakeShared<FLobbyInviteDataEOS>(InviteId, LocalUserId, SenderUserId, LobbyDetails, Future.Get().GetOkValue()));
	});

	return Future;
}

FLobbyInviteDataEOS::FLobbyInviteDataEOS(
	const TSharedRef<FLobbyInviteIdEOS>& InviteIdEOS,
	FOnlineAccountIdHandle Receiver,
	FOnlineAccountIdHandle Sender,
	const TSharedRef<FLobbyDetailsEOS>& LobbyDetails,
	const TSharedRef<FLobbyDataEOS>& LobbyData)
	: InviteIdEOS(InviteIdEOS)
	, Receiver(Receiver)
	, Sender(Sender)
	, LobbyDetails(LobbyDetails)
	, LobbyData(LobbyData)
	, InviteId(Private::TranslateLobbyInviteId(InviteIdEOS->Get()))
{
}

TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>> FLobbySearchEOS::Create(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	const TSharedRef<FLobbyDataRegistryEOS>& LobbyRegistry,
	const FLobbySearchParameters& Params)
{
	TSharedRef<FSearchHandle> SearchHandle = MakeShared<FSearchHandle>();

	EOS_Lobby_CreateLobbySearchOptions CreateLobbySearchOptions = {};
	CreateLobbySearchOptions.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	CreateLobbySearchOptions.MaxResults = Params.MaxResults;

	EOS_EResult EOSResult = EOS_Lobby_CreateLobbySearch(Prerequisites->LobbyInterfaceHandle, &CreateLobbySearchOptions, &SearchHandle->Get());
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		// todo: errors
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
	}

	if (Params.LobbyId) // Search for specific lobby.
	{
		TSharedPtr<FLobbyDataEOS> LobbyData = LobbyRegistry->Find(*Params.LobbyId);
		if (!LobbyData)
		{
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(Errors::InvalidParams()).GetFuture();
		}

		EOS_LobbySearch_SetLobbyIdOptions SetLobbyIdOptions = {};
		SetLobbyIdOptions.ApiVersion = EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST;
		SetLobbyIdOptions.LobbyId = LobbyData->GetLobbyIdEOS();

		EOSResult = EOS_LobbySearch_SetLobbyId(SearchHandle->Get(), &SetLobbyIdOptions);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			// todo: errors
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
		}
	}
	else if (Params.TargetUser) // Search for specific user.
	{
		EOS_LobbySearch_SetTargetUserIdOptions SetTargetUserIdOptions = {};
		SetTargetUserIdOptions.ApiVersion = EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST;
		SetTargetUserIdOptions.TargetUserId = GetProductUserIdChecked(*Params.TargetUser);

		EOSResult = EOS_LobbySearch_SetTargetUserId(SearchHandle->Get(), &SetTargetUserIdOptions);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			// todo: errors
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
		}
	}
	else // Search using parameters.
	{
		// Bucket id.
		{
			const FLobbyBucketIdTranslator<ELobbyTranslationType::ToService> BucketTranslator(Prerequisites->BucketId);

			EOS_Lobby_AttributeData AttributeData;
			AttributeData.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
			AttributeData.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
			AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
			AttributeData.Value.AsUtf8 = BucketTranslator.GetBucketIdEOS();

			EOS_LobbySearch_SetParameterOptions SetParameterOptions = {};
			SetParameterOptions.ApiVersion = EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST;
			SetParameterOptions.Parameter = &AttributeData;
			SetParameterOptions.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;

			EOSResult = EOS_LobbySearch_SetParameter(SearchHandle->Get(), &SetParameterOptions);
			if (EOSResult != EOS_EResult::EOS_Success)
			{
				// todo: errors
				return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
			}
		}

		for (const FFindLobbySearchFilter& Filter :  Params.Filters)
		{
			const FLobbyAttributeTranslator<ELobbyTranslationType::ToService> AttributeTranslator(
				Filter.AttributeName, Filter.ComparisonValue);

			EOS_LobbySearch_SetParameterOptions SetParameterOptions = {};
			SetParameterOptions.ApiVersion = EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST;
			SetParameterOptions.Parameter = &AttributeTranslator.GetAttributeData();
			SetParameterOptions.ComparisonOp = Private::TranslateSearchComparison(Filter.ComparisonOp);

			EOSResult = EOS_LobbySearch_SetParameter(SearchHandle->Get(), &SetParameterOptions);
			if (EOSResult != EOS_EResult::EOS_Success)
			{
				// todo: errors
				return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
			}
		}
	}

	TPromise<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbySearchEOS>>> Future = Promise.GetFuture();

	EOS_LobbySearch_FindOptions FindOptions = {};
	FindOptions.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	FindOptions.LocalUserId = GetProductUserIdChecked(Params.LocalUserId);

	EOS_Async<EOS_LobbySearch_FindCallbackInfo>(EOS_LobbySearch_Find, SearchHandle->Get(), FindOptions)
	.Then([Promise = MoveTemp(Promise), Prerequisites, LobbyRegistry, LocalUserId = Params.LocalUserId, SearchHandle]
	(TFuture<const EOS_LobbySearch_FindCallbackInfo*>&& Future) mutable
	{
		if (Future.Get()->ResultCode != EOS_EResult::EOS_Success)
		{
			// todo: errors
			Promise.EmplaceValue(FromEOSError(Future.Get()->ResultCode));
			return;
		}

		TArray<TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>> ResolvedLobbyDetails;

		EOS_LobbySearch_GetSearchResultCountOptions GetSearchResultCountOptions = {};
		GetSearchResultCountOptions.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
		const uint32_t NumSearchResults = EOS_LobbySearch_GetSearchResultCount(SearchHandle->Get(), &GetSearchResultCountOptions);

		for (uint32_t SearchResultIndex = 0; SearchResultIndex < NumSearchResults; ++SearchResultIndex)
		{
			TDefaultErrorResultInternal<TSharedRef<FLobbyDetailsEOS>> Result = FLobbyDetailsEOS::CreateFromSearchResult(
				Prerequisites, LocalUserId, SearchHandle->Get(), SearchResultIndex);
			if (Result.IsError())
			{
				// todo: errors
				Promise.EmplaceValue(MoveTemp(Result.GetErrorValue()));
				return;
			}

			TPromise<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> ResolveLobbyDetailsPromise;
			ResolvedLobbyDetails.Add(ResolveLobbyDetailsPromise.GetFuture());

			LobbyRegistry->FindOrCreateFromLobbyDetails(LocalUserId, Result.GetOkValue())
			.Then([ResolveLobbyDetailsPromise = MoveTemp(ResolveLobbyDetailsPromise)](TFuture<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>&& Future) mutable
			{
				ResolveLobbyDetailsPromise.EmplaceValue(MoveTempIfPossible(Future.Get()));
			});
		}

		WhenAll(MoveTemp(ResolvedLobbyDetails))
		.Then([Promise = MoveTemp(Promise), SearchHandle](TFuture<TArray<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>>>&& Future) mutable
		{
			TArray<TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>> Results(Future.Get());
			TArray<TSharedRef<FLobbyDataEOS>> ResolvedResults;
			ResolvedResults.Reserve(Results.Num());

			for (TDefaultErrorResultInternal<TSharedRef<FLobbyDataEOS>>& Result : Results)
			{
				if (Result.IsError())
				{
					// todo: errors
					Promise.EmplaceValue(MoveTemp(Result.GetErrorValue()));
					return;
				}

				ResolvedResults.Add(MoveTemp(Result.GetOkValue()));
			}

			Promise.EmplaceValue(MakeShared<FLobbySearchEOS>(SearchHandle, MoveTemp(ResolvedResults)));
		});
	});

	return Future;
}

TArray<TSharedRef<const FLobby>> FLobbySearchEOS::GetLobbyResults() const
{
	TArray<TSharedRef<const FLobby>> Result;
	Result.Reserve(Lobbies.Num());

	for (const TSharedRef<FLobbyDataEOS>& LobbyData : Lobbies)
	{
		Result.Add(LobbyData->GetClientLobbyData()->GetPublicDataPtr());
	}

	return Result;
}

const TArray<TSharedRef<FLobbyDataEOS>>& FLobbySearchEOS::GetLobbyData()
{
	return Lobbies;
}

FLobbySearchEOS::FLobbySearchEOS(const TSharedRef<FSearchHandle>& SearchHandle, TArray<TSharedRef<FLobbyDataEOS>>&& Lobbies)
	: SearchHandle(SearchHandle)
	, Lobbies(Lobbies)
{
}

/* UE::Online */ }