// Copyright Epic Games, Inc. All Rights Reserved.

#include "LobbiesEOSTypes.h"
#include "AuthEOS.h"
#include "Online/LobbiesCommon.h"

namespace UE::Online {

namespace detail {

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

class FLobbyNotificationPauseHandleImpl final : public FLobbyNotificationPauseHandle
{
public:
	using FUnregisterFn = TFunction<void()>;

	FLobbyNotificationPauseHandleImpl(FUnregisterFn&& UnregisterFn)
		: UnregisterFn(MoveTemp(UnregisterFn))
	{
	}

	virtual ~FLobbyNotificationPauseHandleImpl()
	{
		if (UnregisterFn)
		{
			UnregisterFn();
		}
	}

private:
	FUnregisterFn UnregisterFn;
};

} // detail

const EOS_HLobbyDetails FLobbyDetailsEOS::InvalidLobbyDetailsHandle = {};

TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromLobbyId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_LobbyId LobbyId, FOnlineAccountIdHandle MemberHandle)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
	Options.LobbyId = LobbyId;
	Options.LocalUserId = GetProductUserIdChecked(MemberHandle);

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandle(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue().ToSharedRef(), ELobbyDetailsSource::Active, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromInviteId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, const char* InviteId)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYINVITEID_API_LATEST;
	Options.InviteId = InviteId;

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandleByInviteId(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue().ToSharedRef(), ELobbyDetailsSource::Invite, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromUiEventId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_UI_EventId UiEventId)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions Options;
	Options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYUIEVENTID_API_LATEST;
	Options.UiEventId = UiEventId;

	EOS_EResult EOSResult = EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(Prerequisites->LobbyInterfaceHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue().ToSharedRef(), ELobbyDetailsSource::UiEvent, LobbyDetailsHandle));
}

TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> FLobbyDetailsEOS::CreateFromSearchResult(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_HLobbySearch SearchHandle, uint32_t ResultIndex)
{
	EOS_HLobbyDetails LobbyDetailsHandle = {};

	EOS_LobbySearch_CopySearchResultByIndexOptions Options;
	Options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
	Options.LobbyIndex = ResultIndex;

	EOS_EResult EOSResult = EOS_LobbySearch_CopySearchResultByIndex(SearchHandle, &Options, &LobbyDetailsHandle);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(FromEOSError(EOSResult));
	}

	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> LobbyDetailsInfoResult = FLobbyDetailsInfoEOS::Create(LobbyDetailsHandle);
	if (LobbyDetailsInfoResult.IsError())
	{
		EOS_LobbyDetails_Release(LobbyDetailsHandle);
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MoveTemp(LobbyDetailsInfoResult.GetErrorValue()));
	}

	return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>>(MakeShared<FLobbyDetailsEOS>(Prerequisites, LobbyDetailsInfoResult.GetOkValue().ToSharedRef(), ELobbyDetailsSource::Search, LobbyDetailsHandle));
}

FLobbyDetailsEOS::~FLobbyDetailsEOS()
{
	EOS_LobbyDetails_Release(LobbyDetailsHandle);
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobby>>> FLobbyDetailsEOS::GetLobbyData(FOnlineAccountIdHandle LocalUserId, FOnlineLobbyIdHandle LobbyIdHandle) const
{
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

	TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobby>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobby>>> Future = Promise.GetFuture();

	Prerequisites->AuthInterface->ResolveAccountIds(LocalUserId, *MemberProductUserIds)
	.Then([StrongThis = AsShared(), Promise = MoveTemp(Promise), MemberProductUserIds, LobbyIdHandle](TFuture<TArray<FOnlineAccountIdHandle>>&& Future) mutable
	{
		const TArray<FOnlineAccountIdHandle>& ResolvedAccountIds = Future.Get();
		if (MemberProductUserIds->Num() != ResolvedAccountIds.Num())
		{
			// Todo: Errors
			Promise.EmplaceValue(Errors::Unknown());
			return;
		}

		EOS_LobbyDetails_GetLobbyOwnerOptions GetLobbyOwnerOptions = {};
		GetLobbyOwnerOptions.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
		const EOS_ProductUserId LobbyOwner = EOS_LobbyDetails_GetLobbyOwner(StrongThis->LobbyDetailsHandle, &GetLobbyOwnerOptions);

		TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> GetAttributesResult = StrongThis->GetLobbyAttributes();
		if (GetAttributesResult.IsError())
		{
			// Todo: Errors
			Promise.EmplaceValue(Errors::Unknown(MoveTemp(GetAttributesResult.GetErrorValue())));
			return;
		}

		TSharedPtr<FLobby> Lobby = MakeShared<FLobby>();
		Lobby->LobbyId = LobbyIdHandle;
		Lobby->Attributes = GetAttributesResult.GetOkValue();

		for (int32 MemberIndex = 0; MemberIndex < MemberProductUserIds->Num(); ++MemberIndex)
		{
			const EOS_ProductUserId MemberProductUserId = (*MemberProductUserIds)[MemberIndex];
			const FOnlineAccountIdHandle ResolvedProductUserId = ResolvedAccountIds[MemberIndex];

			if (MemberProductUserId == LobbyOwner)
			{
				Lobby->OwnerAccountId = ResolvedProductUserId;
			}

			TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> GetMemberAttributesResult = StrongThis->GetLobbyMemberAttributes(MemberProductUserId);
			if (GetMemberAttributesResult.IsError())
			{
				// Todo: Errors
				Promise.EmplaceValue(Errors::Unknown(MoveTemp(GetMemberAttributesResult.GetErrorValue())));
				return;
			}

			Lobby->Members.Emplace(StrongThis->CreateLobbyMember(ResolvedProductUserId, MoveTemp(GetMemberAttributesResult.GetOkValue())));
		}

		Promise.EmplaceValue(MoveTemp(Lobby));
	});

	return Future;
}

TDefaultErrorResultInternal<TSharedPtr<FLobbyMember>> FLobbyDetailsEOS::GetLobbyMemberData(FOnlineAccountIdHandle MemberHandle) const
{
	TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> MemberAttributesResult = GetLobbyMemberAttributes(GetProductUserIdChecked(MemberHandle));
	if (MemberAttributesResult.IsError())
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyMember>>(MoveTemp(MemberAttributesResult.GetErrorValue()));
	}
	else
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyMember>>(CreateLobbyMember(MemberHandle, MoveTemp(MemberAttributesResult.GetOkValue())));
	}
}

TFuture<EOS_EResult> FLobbyDetailsEOS::ApplyLobbyDataUpdates(FOnlineAccountIdHandle LocalUserId, FLobbyDataChanges Changes) const
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
	ModificationOptions.LobbyId = GetInfo()->GetLobbyIdEOS();

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
		const FTCHARToUTF8 KeyConverter(*MutatedAttribute.Key.ToString());
		const FTCHARToUTF8 ValueConverter(*MutatedAttribute.Value);

		// Todo: handle variant properly.
		// Todo: Schema things.

		EOS_Lobby_AttributeData AttributeData;
		AttributeData.Key = KeyConverter.Get();
		AttributeData.Value.AsUtf8 = ValueConverter.Get();
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;

		EOS_LobbyModification_AddAttributeOptions AddAttributeOptions = {};
		AddAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
		AddAttributeOptions.Attribute = &AttributeData;
		//AddAttributeOptions.Visibility; // todo - get from schema
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

TFuture<EOS_EResult> FLobbyDetailsEOS::ApplyLobbyMemberDataUpdates(FOnlineAccountIdHandle LocalUserId, FLobbyMemberDataChanges Changes) const
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
	ModificationOptions.LobbyId = GetInfo()->GetLobbyIdEOS();

	EOS_EResult EOSResultCode = EOS_Lobby_UpdateLobbyModification(Prerequisites->LobbyInterfaceHandle, &ModificationOptions, &LobbyModificationHandle);
	if (EOSResultCode != EOS_EResult::EOS_Success)
	{
		// Todo: Errors
		return MakeFulfilledPromise<EOS_EResult>(EOSResultCode).GetFuture();
	}

	// Add member attributes.
	for (const TPair<FLobbyAttributeId, FLobbyVariant>& MutatedAttribute : Changes.MutatedAttributes)
	{
		const FTCHARToUTF8 KeyConverter(*MutatedAttribute.Key.ToString());
		const FTCHARToUTF8 ValueConverter(*MutatedAttribute.Value);

		// Todo: handle variant properly.
		// Todo: Schema things.

		EOS_Lobby_AttributeData AttributeData;
		AttributeData.Key = KeyConverter.Get();
		AttributeData.Value.AsUtf8 = ValueConverter.Get();

		EOS_LobbyModification_AddMemberAttributeOptions AddMemberAttributeOptions = {};
		AddMemberAttributeOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST;
		AddMemberAttributeOptions.Attribute = &AttributeData;
		//AddMemberAttributeOptions.Visibility; // todo - get from schema
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
	ELobbyDetailsSource LobbyDetailsSource,
	EOS_HLobbyDetails LobbyDetailsHandle)
	: Prerequisites(Prerequisites)
	, LobbyDetailsInfo(LobbyDetailsInfo)
	, LobbyDetailsHandle(LobbyDetailsHandle)
	, LobbyDetailsSource(LobbyDetailsSource)
{
}

TPair<FLobbyAttributeId, FLobbyVariant> FLobbyDetailsEOS::TranslateLobbyAttribute(const EOS_Lobby_Attribute& LobbyAttribute) const
{
	const FLobbyAttributeId AttributeId = detail::TranslateLobbyAttributeId(LobbyAttribute.Data->Key);
	FLobbyVariant VariantData;

	switch (LobbyAttribute.Data->ValueType)
	{
	case EOS_ELobbyAttributeType::EOS_AT_BOOLEAN:
		// Todo:
		break;

	case EOS_ELobbyAttributeType::EOS_AT_INT64:
		// Todo:
		break;

	case EOS_ELobbyAttributeType::EOS_AT_DOUBLE:
		// Todo:
		break;

	case EOS_ELobbyAttributeType::EOS_AT_STRING:
		VariantData = UTF8_TO_TCHAR(LobbyAttribute.Data->Value.AsUtf8);
		break;

	default:
		// Todo: log
		break;
	}

	return TPair<FLobbyAttributeId, FLobbyVariant>(AttributeId, MoveTemp(VariantData));
}

TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> FLobbyDetailsEOS::GetLobbyAttributes() const
{
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;

	EOS_LobbyDetails_GetAttributeCountOptions GetAttributeCountOptions = {};
	GetAttributeCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETATTRIBUTECOUNT_API_LATEST;

	const uint32_t AttributeCount = EOS_LobbyDetails_GetAttributeCount(LobbyDetailsHandle, &GetAttributeCountOptions);
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

		EOS_EResult EOSResult = EOS_LobbyDetails_CopyAttributeByIndex(LobbyDetailsHandle, &CopyAttributeByIndexOptions, &LobbyAttribute);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			return TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>>(FromEOSError(EOSResult));
		}

		// Todo: Schema things.
		TPair<FLobbyAttributeId, FLobbyVariant> TranslatedAttribute = TranslateLobbyAttribute(*LobbyAttribute);
		Attributes.Emplace(TranslatedAttribute.Key, MoveTemp(TranslatedAttribute.Value));
	}

	return TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>>(MoveTemp(Attributes));
}

TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> FLobbyDetailsEOS::GetLobbyMemberAttributes(EOS_ProductUserId TargetMemberProductUserId) const
{
	TMap<FLobbyAttributeId, FLobbyVariant> Attributes;

	EOS_LobbyDetails_GetMemberAttributeCountOptions GetMemberAttributeCountOptions = {};
	GetMemberAttributeCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERATTRIBUTECOUNT_API_LATEST;
	GetMemberAttributeCountOptions.TargetUserId = TargetMemberProductUserId;

	const uint32_t MemberAttributeCount = EOS_LobbyDetails_GetMemberAttributeCount(LobbyDetailsHandle, &GetMemberAttributeCountOptions);
	for (uint32_t MemberAttributeIndex = 0; MemberAttributeIndex < MemberAttributeCount; ++MemberAttributeIndex)
	{
		EOS_LobbyDetails_CopyMemberAttributeByIndexOptions CopyMemberAttributeByIndexOptions = {};
		CopyMemberAttributeByIndexOptions.ApiVersion = EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYINDEX_API_LATEST;
		CopyMemberAttributeByIndexOptions.TargetUserId = TargetMemberProductUserId;
		CopyMemberAttributeByIndexOptions.AttrIndex = MemberAttributeIndex;

		EOS_Lobby_Attribute* LobbyAttribute = nullptr;
		ON_SCOPE_EXIT
		{
			EOS_Lobby_Attribute_Release(LobbyAttribute);
		};

		EOS_EResult EOSResult = EOS_LobbyDetails_CopyMemberAttributeByIndex(LobbyDetailsHandle, &CopyMemberAttributeByIndexOptions, &LobbyAttribute);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			return TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>>(FromEOSError(EOSResult));
		}

		// Todo: Schema things.
		TPair<FLobbyAttributeId, FLobbyVariant> TranslatedAttribute = TranslateLobbyAttribute(*LobbyAttribute);
		Attributes.Emplace(TranslatedAttribute.Key, MoveTemp(TranslatedAttribute.Value));
	}

	return TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>>(MoveTemp(Attributes));
}

TSharedRef<FLobbyMember> FLobbyDetailsEOS::CreateLobbyMember(FOnlineAccountIdHandle MemberHandle, TMap<FLobbyAttributeId, FLobbyVariant> Attributes) const
{
	TSharedRef<FLobbyMember> LobbyMember = MakeShared<FLobbyMember>();
	LobbyMember->AccountId = MemberHandle;
	LobbyMember->Attributes = MoveTemp(Attributes);

	//Todo: 
	//LobbyMember->PlatformAccountId;
	//LobbyMember->PlatformDisplayName;

	return LobbyMember;
}

TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> FLobbyDetailsInfoEOS::Create(EOS_HLobbyDetails LobbyDetailsHandle)
{
	EOS_LobbyDetails_CopyInfoOptions CopyInfoOptions = {};
	CopyInfoOptions.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;

	EOS_LobbyDetails_Info* LobbyDetailsInfo = nullptr;
	EOS_EResult EOSResult = EOS_LobbyDetails_CopyInfo(LobbyDetailsHandle, &CopyInfoOptions, &LobbyDetailsInfo);
	if (EOSResult != EOS_EResult::EOS_Success)
	{
		return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>>(FromEOSError(EOSResult));
	}

	TSharedRef<FLobbyDetailsInfoEOS> Result = MakeShared<FLobbyDetailsInfoEOS>();
	Result->LobbyDetailsInfo.Reset(LobbyDetailsInfo);

	// Resolve lobby info.

	return TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>>(MoveTemp(Result));
}

FLobbyDataEOS::~FLobbyDataEOS()
{
	if (UnregisterFn)
	{
		UnregisterFn(LobbyIdHandle);
	}
}

TSharedRef<FLobbyNotificationPauseHandle> FLobbyDataEOS::PauseLobbyNotifications(FOnlineAccountIdHandle LocalUserId)
{
	if (TSharedRef<FLobbyNotificationPauseHandle>* ExistingPause = NotificationPauses.Find(LocalUserId))
	{
		return *ExistingPause;
	}
	else
	{
		TSharedRef<FLobbyNotificationPauseHandle> NewPause = MakeShared<detail::FLobbyNotificationPauseHandleImpl>(
		[WeakThis = AsWeak(), LocalUserId]()
		{
			if (TSharedPtr<FLobbyDataEOS> StrongThis = WeakThis.Pin())
			{
				StrongThis->NotificationPauses.Remove(LocalUserId);
			}
		});

		NotificationPauses.Add(LocalUserId, NewPause);
		return NewPause;
	}
}

bool FLobbyDataEOS::CanNotify(FOnlineAccountIdHandle LocalUserId)
{
	return !LocalMembers.IsEmpty() && NotificationPauses.Find(LocalUserId) == nullptr;
}

void FLobbyDataEOS::SetUserLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedPtr<FLobbyDetailsEOS>& LobbyDetails)
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

FLobbyDataEOS::FLobbyDataEOS(
	FOnlineLobbyIdHandle LobbyIdHandle,
	const TSharedRef<FLobby>& LobbyImpl,
	const TSharedRef<FLobbyDetailsInfoEOS>& LobbyDetailsInfo,
	FUnregisterFn UnregisterFn)
	: LobbyIdHandle(LobbyIdHandle)
	, LobbyImpl(LobbyImpl)
	, LobbyDetailsInfo(LobbyDetailsInfo)
	, UnregisterFn(MoveTemp(UnregisterFn))
	, LobbyId(detail::TranslateLobbyId(LobbyDetailsInfo->GetLobbyIdEOS()))
{
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FLobbyDataEOS::Create(
	FOnlineAccountIdHandle LocalUserId,
	FOnlineLobbyIdHandle LobbyIdHandle,
	const TSharedRef<FLobbyDetailsEOS>& LobbyDetails,
	FUnregisterFn UnregisterFn)
{
	TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Future = Promise.GetFuture();
	
	LobbyDetails->GetLobbyData(LocalUserId, LobbyIdHandle)
	.Then(
	[
		Promise = MoveTemp(Promise),
		LocalUserId,
		LobbyIdHandle,
		LobbyDetails,
		UnregisterFn = MoveTemp(UnregisterFn)
	]
	(TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobby>>>&& Future) mutable
	{
		if (Future.Get().IsError())
		{
			Promise.EmplaceValue(MoveTemp(Future.Get().GetErrorValue()));
		}
		else
		{
			Promise.EmplaceValue(MakeShared<FLobbyDataEOS>(
				LobbyIdHandle,
				Future.Get().GetOkValue().ToSharedRef(),
				LobbyDetails->GetInfo(),
				MoveTemp(UnregisterFn)));
		}
	});

	return Future;
}

FLobbyDataRegistryEOS::FLobbyDataRegistryEOS(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites)
	: Prerequisites(Prerequisites)
{
}

TSharedPtr<FLobbyDataEOS> FLobbyDataRegistryEOS::Find(EOS_LobbyId EOSLobbyId) const
{
	const TWeakPtr<FLobbyDataEOS>* Result = LobbyIdIndex.Find(detail::TranslateLobbyId(EOSLobbyId));
	return Result ? Result->Pin() : TSharedPtr<FLobbyDataEOS>();
}

TSharedPtr<FLobbyDataEOS> FLobbyDataRegistryEOS::Find(FOnlineLobbyIdHandle LobbyIdHandle) const
{
	const TWeakPtr<FLobbyDataEOS>* Result = LobbyIdHandleIndex.Find(LobbyIdHandle);
	return Result ? Result->Pin() : TSharedPtr<FLobbyDataEOS>();
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FLobbyDataRegistryEOS::FindOrCreateFromLobbyId(FOnlineAccountIdHandle LocalUserId, EOS_LobbyId EOSLobbyId)
{
	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> LobbyDetailsResult = FLobbyDetailsEOS::CreateFromLobbyId(Prerequisites, EOSLobbyId, LocalUserId);
	if (LobbyDetailsResult.IsError())
	{
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>(MoveTemp(LobbyDetailsResult.GetErrorValue())).GetFuture();
	}

	return FindOrCreateFromLobbyDetails(LocalUserId, LobbyDetailsResult.GetOkValue().ToSharedRef());
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FLobbyDataRegistryEOS::FindOrCreateFromUiEventId(FOnlineAccountIdHandle LocalUserId, EOS_UI_EventId UiEventId)
{
	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> LobbyDetailsResult = FLobbyDetailsEOS::CreateFromUiEventId(Prerequisites, UiEventId);
	if (LobbyDetailsResult.IsError())
	{
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>(MoveTemp(LobbyDetailsResult.GetErrorValue())).GetFuture();
	}

	return FindOrCreateFromLobbyDetails(LocalUserId, LobbyDetailsResult.GetOkValue().ToSharedRef());
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FLobbyDataRegistryEOS::FindOrCreateFromLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDetailsEOS>& LobbyDetails)
{
	if (TSharedPtr<FLobbyDataEOS> FindResult = Find(LobbyDetails->GetInfo()->GetLobbyIdEOS()))
	{
		FindResult->SetUserLobbyDetails(LocalUserId, LobbyDetails);
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>(FindResult).GetFuture();
	}

	TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Future = Promise.GetFuture();

	const FOnlineLobbyIdHandle LobbyId = FOnlineLobbyIdHandle(EOnlineServices::Epic, NextHandleIndex++);
	FLobbyDataEOS::Create(LocalUserId, LobbyId, LobbyDetails, MakeUnregisterFn())
	.Then([WeakThis = AsWeak(), Promise = MoveTemp(Promise), LocalUserId, LobbyDetails](TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>&& Future) mutable
	{
		if (TSharedPtr<FLobbyDataRegistryEOS> StrongThis = WeakThis.Pin())
		{
			if (Future.Get().IsOk())
			{
				StrongThis->Register(Future.Get().GetOkValue().ToSharedRef());
			}
		}

		Future.Get().GetOkValue()->SetUserLobbyDetails(LocalUserId, LobbyDetails);
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

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyInviteDataEOS>>> FLobbyInviteDataEOS::CreateFromInviteId(
	const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
	const TSharedRef<FLobbyDataRegistryEOS>& LobbyDataRegistry,
	FOnlineAccountIdHandle LocalUserId,
	const char* InviteIdEOS,
	EOS_ProductUserId Sender)
{
	TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> LobbyDetailsResult = FLobbyDetailsEOS::CreateFromInviteId(Prerequisites, InviteIdEOS);
	if (LobbyDetailsResult.IsError())
	{
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyInviteDataEOS>>>(MoveTemp(LobbyDetailsResult.GetErrorValue())).GetFuture();
	}

	TSharedRef<FLobbyDetailsEOS> LobbyDetails = LobbyDetailsResult.GetOkValue().ToSharedRef();
	TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyInviteDataEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyInviteDataEOS>>> Future = Promise.GetFuture();

	// Search for existing lobby data so that the LobbyIdHandle will match.
	TSharedRef<FLobbyInviteIdEOS> InviteId = MakeShared<FLobbyInviteIdEOS>(InviteIdEOS);
	LobbyDataRegistry->FindOrCreateFromLobbyDetails(LocalUserId, LobbyDetails)
	.Then([Promise = MoveTemp(Promise), InviteId, LocalUserId, Sender, LobbyDetails](TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>&& Future) mutable
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
			Promise.EmplaceValue(Errors::Unknown());
			return;
		}

		Promise.EmplaceValue(MakeShared<FLobbyInviteDataEOS>(InviteId, LocalUserId, SenderUserId, LobbyDetails, Future.Get().GetOkValue().ToSharedRef()));
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
	, InviteId(detail::TranslateLobbyInviteId(InviteIdEOS->Get()))
{
}

TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>> FLobbySearchEOS::Create(
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
		return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
	}

	if (Params.LobbyId)
	{
		TSharedPtr<FLobbyDataEOS> LobbyData = LobbyRegistry->Find(*Params.LobbyId);
		if (!LobbyData)
		{
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>>(Errors::InvalidParams()).GetFuture();
		}

		EOS_LobbySearch_SetLobbyIdOptions SetLobbyIdOptions = {};
		SetLobbyIdOptions.ApiVersion = EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST;
		SetLobbyIdOptions.LobbyId = LobbyData->GetLobbyIdEOS();

		EOSResult = EOS_LobbySearch_SetLobbyId(SearchHandle->Get(), &SetLobbyIdOptions);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			// todo: errors
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
		}
	}

	if (Params.TargetUser)
	{
		EOS_LobbySearch_SetTargetUserIdOptions SetTargetUserIdOptions = {};
		SetTargetUserIdOptions.ApiVersion = EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST;
		SetTargetUserIdOptions.TargetUserId = GetProductUserIdChecked(*Params.TargetUser);

		EOSResult = EOS_LobbySearch_SetTargetUserId(SearchHandle->Get(), &SetTargetUserIdOptions);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			// todo: errors
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
		}
	}

	for (const FFindLobbySearchFilter& Filter :  Params.Filters)
	{
		FTCHARToUTF8 Key(*Filter.AttributeName.ToString());
		FTCHARToUTF8 Value(*Filter.ComparisonValue);

		EOS_Lobby_AttributeData AttributeData;
		AttributeData.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;

		// todo: support other attribute types.
		AttributeData.Key = Key.Get();
		AttributeData.Value.AsUtf8 = Value.Get();
		AttributeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;

		EOS_LobbySearch_SetParameterOptions SetParameterOptions = {};
		SetParameterOptions.ApiVersion = EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST;
		SetParameterOptions.Parameter = &AttributeData;
		SetParameterOptions.ComparisonOp = detail::TranslateSearchComparison(Filter.ComparisonOp);

		EOSResult = EOS_LobbySearch_SetParameter(SearchHandle->Get(), &SetParameterOptions);
		if (EOSResult != EOS_EResult::EOS_Success)
		{
			// todo: errors
			return MakeFulfilledPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>>(FromEOSError(EOSResult)).GetFuture();
		}
	}

	TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>> Promise;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>> Future = Promise.GetFuture();

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

		TArray<TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>> ResolvedLobbyDetails;

		EOS_LobbySearch_GetSearchResultCountOptions GetSearchResultCountOptions = {};
		GetSearchResultCountOptions.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
		const uint32_t NumSearchResults = EOS_LobbySearch_GetSearchResultCount(SearchHandle->Get(), &GetSearchResultCountOptions);

		for (uint32_t SearchResultIndex = 0; SearchResultIndex < NumSearchResults; ++SearchResultIndex)
		{
			TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> Result = FLobbyDetailsEOS::CreateFromSearchResult(
				Prerequisites, SearchHandle->Get(), SearchResultIndex);
			if (Result.IsError())
			{
				// todo: errors
				Promise.EmplaceValue(MoveTemp(Result.GetErrorValue()));
				return;
			}

			TPromise<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> ResolveLobbyDetailsPromise;
			ResolvedLobbyDetails.Add(ResolveLobbyDetailsPromise.GetFuture());

			LobbyRegistry->FindOrCreateFromLobbyDetails(LocalUserId, Result.GetOkValue().ToSharedRef())
			.Then([ResolveLobbyDetailsPromise = MoveTemp(ResolveLobbyDetailsPromise)](TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>&& Future) mutable
			{
				ResolveLobbyDetailsPromise.EmplaceValue(MoveTempIfPossible(Future.Get()));
			});
		}

		WhenAll(MoveTemp(ResolvedLobbyDetails))
		.Then([Promise = MoveTemp(Promise), SearchHandle](TFuture<TArray<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>>> && Future) mutable
		{
			TArray<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Results(Future.Get());
			TArray<TSharedPtr<FLobbyDataEOS>> ResolvedResults;
			ResolvedResults.Reserve(Results.Num());

			for (TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>& Result : Results)
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

	for (const TSharedPtr<FLobbyDataEOS>& LobbyData : Lobbies)
	{
		Result.Add(LobbyData->GetLobbyImpl());
	}

	return Result;
}

const TArray<TSharedPtr<FLobbyDataEOS>>& FLobbySearchEOS::GetLobbyData()
{
	return Lobbies;
}

FLobbySearchEOS::FLobbySearchEOS(const TSharedRef<FSearchHandle>& SearchHandle, TArray<TSharedPtr<FLobbyDataEOS>>&& Lobbies)
	: SearchHandle(SearchHandle)
	, Lobbies(Lobbies)
{
}

/* UE::Online */ }