// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraMessageStore.generated.h"

class UNiagaraMessageDataBase;

USTRUCT()
struct NIAGARA_API FNiagaraMessageStore
{
	GENERATED_BODY();

	const TMap<FGuid, TObjectPtr<UNiagaraMessageDataBase>>& GetMessages() const;
	void SetMessages(const TMap<FGuid, TObjectPtr<UNiagaraMessageDataBase>>& InMessageKeyToMessageMap);
	void AddMessage(const FGuid& MessageKey, UNiagaraMessageDataBase* NewMessage);
	void RemoveMessage(const FGuid& MessageKey);
	void DismissMessage(const FGuid& MessageKey);
	bool IsMessageDismissed(const FGuid& MessageKey);
	bool HasDismissedMessages() const;
	void ClearDismissedMessages();

private:
	UPROPERTY()
	TMap<FGuid, TObjectPtr<UNiagaraMessageDataBase>> MessageKeyToMessageMap;

	UPROPERTY()
	TArray<FGuid> DismissedMessageKeys;
};

struct NIAGARA_API FNiagaraMessageSourceAndStore
{
public:
	FNiagaraMessageSourceAndStore(UObject& InSource, FNiagaraMessageStore& InStore);

	UObject* GetSource() const;

	FNiagaraMessageStore* GetStore() const;

private:
	TWeakObjectPtr<UObject> SourceWeak;
	FNiagaraMessageStore* Store;
};
