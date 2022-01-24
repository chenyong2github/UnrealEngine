// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Lobbies.h"
#include "Online/OnlineComponent.h"

#define LOBBIES_FUNCTIONAL_TEST_ENABLED !UE_BUILD_SHIPPING

namespace UE::Online {

class FOnlineServicesCommon;
class FAccountInfo;

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
struct FFunctionalTestLobbies
{
	static constexpr TCHAR Name[] = TEXT("FunctionalTest");

	struct Params
	{
	};

	struct Result
	{
	};
};
#endif

struct FLobbyConfig
{
	TArray<FLobbySchemaId> RegisteredSchemas;
};

struct FLobbySchemaAttributeConfig
{
	// The attribute name.
	FLobbyAttributeId Name;
	
	// Override default service attribute field assignment.
	// Specifying an override allows custom grouping of attributes within a
	// platform attribute. This grouping is useful when a set of attributes are
	// known to update at the same cadence.
	TOptional<FLobbyAttributeId> ServiceAttributeFieldId;
	
	// The size in bytes of the attribute value.
	// A schema will fail validation if all attributes will not fit within the
	// platform's lobby fields.
	uint32 MaxByteSize = 0;

	// Control whether the attribute is visible to players who are not joined
	// to the lobby.
	ELobbyAttributeVisibility Visibility = ELobbyAttributeVisibility::Private;

};

struct FLobbySchemaConfig
{
	// The schema name.
	FLobbySchemaId SchemaName;

	// The optional base schema name.
	FLobbySchemaId BaseSchemaName;
	
	// The definitions for attributes attached to a lobby.
	TArray<FLobbySchemaAttributeConfig> LobbyAttributes;

	// The definitions for attributes attached to a lobby member.
	TArray<FLobbySchemaAttributeConfig> LobbyMemberAttributes;
};

class FLobbyServiceAttributeChanges final
{
public:
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;
	TSet<FLobbyAttributeId> ClearedAttributes;
};

class FLobbyClientAttributeChanges final
{
public:
	// Apply attribute changes to attribute parameter.
	// Applying the changes will clear MutatedAttributes and ClearedAttributes.
	// Returns a list of the attributes which were changed.
	TSet<FLobbyAttributeId> Apply(TMap<FLobbyAttributeId, FLobbyVariant>& InOutAttributes)
	{
		TSet<FLobbyAttributeId> Changes;
		Changes.Reserve(MutatedAttributes.Num() + ClearedAttributes.Num());

		for (TPair<FLobbyAttributeId, FLobbyVariant>& Attribute : MutatedAttributes)
		{
			Changes.Add(Attribute.Key);
			InOutAttributes.Emplace(Attribute.Key, MoveTemp(Attribute.Value));
		}

		for (FLobbyAttributeId AttributeId : ClearedAttributes)
		{
			Changes.Add(AttributeId);
			InOutAttributes.Remove(AttributeId);
		}

		MutatedAttributes.Empty();
		ClearedAttributes.Empty();

		return Changes;
	}

	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;
	TSet<FLobbyAttributeId> ClearedAttributes;
};

struct FLobbyEvents final
{
	TOnlineEventCallable<void(const FLobbyJoined&)> OnLobbyJoined;
	TOnlineEventCallable<void(const FLobbyLeft&)> OnLobbyLeft;
	TOnlineEventCallable<void(const FLobbyMemberJoined&)> OnLobbyMemberJoined;
	TOnlineEventCallable<void(const FLobbyMemberLeft&)> OnLobbyMemberLeft;
	TOnlineEventCallable<void(const FLobbyLeaderChanged&)> OnLobbyLeaderChanged;
	TOnlineEventCallable<void(const FLobbySchemaChanged&)> OnLobbySchemaChanged;
	TOnlineEventCallable<void(const FLobbyAttributesChanged&)> OnLobbyAttributesChanged;
	TOnlineEventCallable<void(const FLobbyMemberAttributesChanged&)> OnLobbyMemberAttributesChanged;
	TOnlineEventCallable<void(const FLobbyInvitationAdded&)> OnLobbyInvitationAdded;
	TOnlineEventCallable<void(const FLobbyInvitationRemoved&)> OnLobbyInvitationRemoved;
};

class FLobbySchema final
{
public:
	static TSharedPtr<FLobbySchema> Create(FLobbySchemaConfig LobbySchemaConfig);

	TSharedRef<FLobbyServiceAttributeChanges> TranslateLobbyAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const;
	TSharedRef<FLobbyClientAttributeChanges> TranslateLobbyAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const;
	TSharedRef<FLobbyServiceAttributeChanges> TranslateLobbyMemberAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const;
	TSharedRef<FLobbyClientAttributeChanges> TranslateLobbyMemberAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const;

private:
	FLobbySchemaId SchemaId;
};

class FLobbySchemaRegistry
{
public:
	bool Initialize(TArray<FLobbySchemaConfig> LobbySchemaConfigs);
	TSharedPtr<FLobbySchema> FindSchema(FLobbySchemaId SchemaId);

private:
	bool RegisterSchema(FLobbySchemaConfig LobbySchemaConfig);

	TMap<FLobbySchemaId, TSharedRef<FLobbySchema>> RegisteredSchemas;
};

// Todo: put this somewhere else
template <typename AwaitedType>
TFuture<TArray<AwaitedType>> WhenAll(TArray<TFuture<AwaitedType>>&& Futures)
{
	struct FWhenAllState
	{
		TArray<TFuture<AwaitedType>> Futures;
		TArray<AwaitedType> Results;
		TPromise<TArray<AwaitedType>> FinalPromise;
	};

	if (Futures.IsEmpty())
	{
		return MakeFulfilledPromise<TArray<AwaitedType>>().GetFuture();
	}
	else
	{
		TSharedRef<FWhenAllState> WhenAllState = MakeShared<FWhenAllState>();
		WhenAllState->Futures = MoveTemp(Futures);

		for (TFuture<AwaitedType>& Future : WhenAllState->Futures)
		{
			Future.Then([WhenAllState](TFuture<AwaitedType>&& AwaitedResult)
			{
				WhenAllState->Results.Emplace(MoveTempIfPossible(AwaitedResult.Get()));

				if (WhenAllState->Futures.Num() == WhenAllState->Results.Num())
				{
					WhenAllState->FinalPromise.EmplaceValue(MoveTemp(WhenAllState->Results));
				}
			});
		}

		return WhenAllState->FinalPromise.GetFuture();
	}
}

class ONLINESERVICESCOMMON_API FLobbiesCommon : public TOnlineComponent<ILobbies>
{
public:
	using Super = ILobbies;

	FLobbiesCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void Initialize() override;
	virtual void RegisterCommands() override;

	// ILobbies
	virtual TOnlineAsyncOpHandle<FCreateLobby> CreateLobby(FCreateLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindLobbies> FindLobbies(FFindLobbies::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRestoreLobbies> RestoreLobbies(FRestoreLobbies::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinLobby> JoinLobby(FJoinLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveLobby> LeaveLobby(FLeaveLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FInviteLobbyMember> InviteLobbyMember(FInviteLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FDeclineLobbyInvitation> DeclineLobbyInvitation(FDeclineLobbyInvitation::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FKickLobbyMember> KickLobbyMember(FKickLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FPromoteLobbyMember> PromoteLobbyMember(FPromoteLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbySchema> ModifyLobbySchema(FModifyLobbySchema::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyJoinPolicy> ModifyLobbyJoinPolicy(FModifyLobbyJoinPolicy::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyAttributes> ModifyLobbyAttributes(FModifyLobbyAttributes::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyMemberAttributes> ModifyLobbyMemberAttributes(FModifyLobbyMemberAttributes::Params&& Params) override;
	virtual TOnlineResult<FGetJoinedLobbies> GetJoinedLobbies(FGetJoinedLobbies::Params&& Params) override;
	virtual TOnlineResult<FGetReceivedInvitations> GetReceivedInvitations(FGetReceivedInvitations::Params&& Params) override;

	virtual TOnlineEvent<void(const FLobbyJoined&)> OnLobbyJoined() override;
	virtual TOnlineEvent<void(const FLobbyLeft&)> OnLobbyLeft() override;
	virtual TOnlineEvent<void(const FLobbyMemberJoined&)> OnLobbyMemberJoined() override;
	virtual TOnlineEvent<void(const FLobbyMemberLeft&)> OnLobbyMemberLeft() override;
	virtual TOnlineEvent<void(const FLobbyLeaderChanged&)> OnLobbyLeaderChanged() override;
	virtual TOnlineEvent<void(const FLobbySchemaChanged&)> OnLobbySchemaChanged() override;
	virtual TOnlineEvent<void(const FLobbyAttributesChanged&)> OnLobbyAttributesChanged() override;
	virtual TOnlineEvent<void(const FLobbyMemberAttributesChanged&)> OnLobbyMemberAttributesChanged() override;
	virtual TOnlineEvent<void(const FLobbyInvitationAdded&)> OnLobbyInvitationAdded() override;
	virtual TOnlineEvent<void(const FLobbyInvitationRemoved&)> OnLobbyInvitationRemoved() override;

protected:
	TFuture<TDefaultErrorResultInternal<FOnlineLobbyIdHandle>> AwaitInvitation(
		FOnlineAccountIdHandle TargetAccountId,
		FOnlineLobbyIdHandle LobbyId,
		float TimeoutSeconds);

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
	TOnlineAsyncOpHandle<FFunctionalTestLobbies> FunctionalTest(FFunctionalTestLobbies::Params&& Params);

	struct FFunctionalTestLoginUser
	{
		static constexpr TCHAR Name[] = TEXT("FunctionalTestLoginUser");

		struct Params
		{
			FPlatformUserId PlatformUserId;
			FString Type;
			FString Id;
			FString Token;
		};

		struct Result
		{
			TSharedPtr<FAccountInfo> AccountInfo;
		};
	};
	TFuture<TOnlineResult<FFunctionalTestLoginUser>> FunctionalTestLoginUser(FFunctionalTestLoginUser::Params&& Params);

	struct FFunctionalTestLogoutUser
	{
		static constexpr TCHAR Name[] = TEXT("FunctionalTestLogoutUser");

		struct Params
		{
			FPlatformUserId PlatformUserId;
		};

		struct Result
		{
		};
	};
	TFuture<TOnlineResult<FFunctionalTestLogoutUser>> FunctionalTestLogoutUser(FFunctionalTestLogoutUser::Params&& Params);

	struct FFunctionalTestLogoutAllUsers
	{
		static constexpr TCHAR Name[] = TEXT("FunctionalTestLogoutAllUsers");

		struct Params
		{
		};

		struct Result
		{
		};
	};
	TFuture<TOnlineResult<FFunctionalTestLogoutAllUsers>> FunctionalTestLogoutAllUsers(FFunctionalTestLogoutAllUsers::Params&& Params);
#endif

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> ConsumeStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func)
	{
		return [this, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable
		{
			TPromise<void> Promise;
			auto Future = Promise.GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared()](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
			{
				if (Future.Get().IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
				}
				Promise.EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> ConsumeStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func, typename SecondaryOpType::Params&& InParams)
	{
		return [this, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
		{
			TPromise<void> Promise;
			auto Future = Promise.GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared()](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
			{
				if (Future.Get().IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
				}
				Promise.EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> CaptureStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func)
	{
		return [this, ResultKey, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable
		{
			TPromise<void> Promise;
			auto Future = Promise.GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared(), ResultKey](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
			{
				if (Future.Get().IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
					Op->Data.Set(ResultKey, MoveTempIfPossible(Future.Get().GetOkValue()));
				}
				Promise.EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> CaptureStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func, typename SecondaryOpType::Params&& InParams)
	{
		return [this, ResultKey, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
		{
			TPromise<void> Promise;
			auto Future = Promise.GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared(), ResultKey](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
			{
				if (Future.Get().IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
					Op->Data.Set(ResultKey, MoveTempIfPossible(Future.Get().GetOkValue()));
				}
				Promise.EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> ConsumeOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func)
	{
		return [this, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable -> TFuture<void>
		{
			auto Promise = MakeShared<TPromise<void>>();
			auto Future = Promise->GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.OnComplete([Promise, Op = InAsyncOp.AsShared()](const TOnlineResult<SecondaryOpType>& Result) mutable -> void
			{
				if (Result.IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
				}
				Promise->EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> ConsumeOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func, typename SecondaryOpType::Params&& InParams)
	{
		return [this, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
		{
			auto Promise = MakeShared<TPromise<void>>();
			auto Future = Promise->GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.OnComplete([Promise, Op = InAsyncOp.AsShared()](const TOnlineResult<SecondaryOpType>& Result) mutable
			{
				if (Result.IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::ConsumeOperationStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
				}
				Promise->EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> CaptureOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func)
	{
		return [this, ResultKey, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable -> TFuture<void>
		{
			auto Promise = MakeShared<TPromise<void>>();
			auto Future = Promise->GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.OnComplete([Promise, Op = InAsyncOp.AsShared(), ResultKey](const TOnlineResult<SecondaryOpType>& Result) mutable -> void
			{
				if (Result.IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
					Op->Data.Set(ResultKey, Result.GetOkValue());
				}
				Promise->EmplaceValue();
			});

			return Future;
		};
	}

	template <typename SecondaryOpType, typename OpType, typename Function>
	TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> CaptureOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func, typename SecondaryOpType::Params&& InParams)
	{
		return [this, ResultKey, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
		{
			auto Promise = MakeShared<TPromise<void>>();
			auto Future = Promise->GetFuture();

			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

			(this->*Func)(MoveTempIfPossible(InParams))
			.OnComplete([Promise, Op = InAsyncOp.AsShared(), ResultKey](const TOnlineResult<SecondaryOpType>& Result) mutable
			{
				if (Result.IsError())
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
					Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::CaptureOperationStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
					Op->Data.Set(ResultKey, Result.GetOkValue());
				}
				Promise->EmplaceValue();
			});

			return Future;
		};
	}

	FLobbyEvents LobbyEvents;

	TSharedPtr<FLobbySchemaRegistry> LobbySchemaRegistry;
	TSharedPtr<FLobbySchema> ServiceSchema;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FLobbyConfig)
	ONLINE_STRUCT_FIELD(FLobbyConfig, RegisteredSchemas)
END_ONLINE_STRUCT_META()


BEGIN_ONLINE_STRUCT_META(FLobbySchemaAttributeConfig)
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, Name),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, ServiceAttributeFieldId),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, MaxByteSize),
	ONLINE_STRUCT_FIELD(FLobbySchemaAttributeConfig, Visibility)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbySchemaConfig)
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, BaseSchemaName),
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, LobbyAttributes),
	ONLINE_STRUCT_FIELD(FLobbySchemaConfig, LobbyMemberAttributes)
END_ONLINE_STRUCT_META()

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
	BEGIN_ONLINE_STRUCT_META(FFunctionalTestLobbies::Params)
	END_ONLINE_STRUCT_META()

	BEGIN_ONLINE_STRUCT_META(FFunctionalTestLobbies::Result)
	END_ONLINE_STRUCT_META()
#endif

/* Meta */ }

/* UE::Online */ }
