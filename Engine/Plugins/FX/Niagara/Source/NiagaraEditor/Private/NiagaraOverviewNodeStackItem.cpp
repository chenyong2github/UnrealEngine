// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraOverviewNodeStackItem.h"
#include "NiagaraSystem.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewNodeStackItem"

UNiagaraOverviewNodeStackItem::UNiagaraOverviewNodeStackItem()
	: OwningSystem(nullptr)
	, EmitterHandleGuid(FGuid())
{
};

void UNiagaraOverviewNodeStackItem::Initialize(UNiagaraSystem* InOwningSystem)
{
	OwningSystem = InOwningSystem;
}

void UNiagaraOverviewNodeStackItem::Initialize(UNiagaraSystem* InOwningSystem, FGuid InEmitterHandleGuid)
{
	OwningSystem = InOwningSystem;
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

bool UNiagaraOverviewNodeStackItem::CanUserDeleteNode() const
{
	return EmitterHandleGuid.IsValid();
}

bool UNiagaraOverviewNodeStackItem::CanDuplicateNode() const
{
	return EmitterHandleGuid.IsValid();
}

#undef LOCTEXT_NAMESPACE
