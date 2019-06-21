// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraOverviewNodeStackItem.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewNodeStackItem"

UNiagaraOverviewNodeStackItem::UNiagaraOverviewNodeStackItem()
	: OwningSystem(nullptr)
	, EmitterHandleGuid(FGuid())
{
};

void UNiagaraOverviewNodeStackItem::Initialize(const UNiagaraSystem* InOwningSystem)
{
	OwningSystem = const_cast<UNiagaraSystem*>(InOwningSystem);
}

void UNiagaraOverviewNodeStackItem::Initialize(const UNiagaraSystem* InOwningSystem, FGuid InEmitterHandleGuid)
{
	OwningSystem = const_cast<UNiagaraSystem*>(InOwningSystem);
	EmitterHandleGuid = InEmitterHandleGuid;
}

const FGuid UNiagaraOverviewNodeStackItem::GetEmitterHandleGuid() const
{
	return EmitterHandleGuid;
}

FText UNiagaraOverviewNodeStackItem::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (EmitterHandleGuid.IsValid())
	{
		for (const FNiagaraEmitterHandle& Handle : OwningSystem->GetEmitterHandles())
		{
			if (Handle.GetId() == EmitterHandleGuid)
			{
				return FText::FromName(Handle.GetName());
			}
		}
		ensureMsgf(false, TEXT("Failed to find matching emitter handle for existing overview node!"));
		return LOCTEXT("UnknownEmitterName", "Unknown Emitter");
	}
	else
	{
		return FText::FromString(OwningSystem->GetName());
	}
}

#undef LOCTEXT_NAMESPACE
