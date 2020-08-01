// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystemInstance.h"

FNiagaraUserRedirectionParameterStore::FNiagaraUserRedirectionParameterStore() : FNiagaraParameterStore()
{

}

FNiagaraUserRedirectionParameterStore::FNiagaraUserRedirectionParameterStore(const FNiagaraParameterStore& Other)
{
	*this = Other;
}

FNiagaraUserRedirectionParameterStore& FNiagaraUserRedirectionParameterStore::operator=(const FNiagaraParameterStore& Other)
{
	Super::operator=(Other);	
	RecreateRedirections();
	return *this;
}

bool FNiagaraUserRedirectionParameterStore::IsUserParameter(const FNiagaraVariableBase& InVar) 
{
	return InVar.GetName().ToString().StartsWith(TEXT("User."));
}

void FNiagaraUserRedirectionParameterStore::MakeUserVariable(FNiagaraVariableBase& InVar)
{
	if (IsUserParameter(InVar))
	{
		return;
	}
	FName DisplayName(*(TEXT("User.") + InVar.GetName().ToString()));
	InVar.SetName(DisplayName);
	return;
}


FNiagaraVariable FNiagaraUserRedirectionParameterStore::GetUserRedirection(const FNiagaraVariable & InVar) const
{
	if (!IsUserParameter(InVar))
	{
		return InVar;
	}
	FNiagaraVariable SimpleVar = InVar;
	FName DisplayName(*InVar.GetName().ToString().RightChop(5));
	SimpleVar.SetName(DisplayName);
	return SimpleVar;
}

void FNiagaraUserRedirectionParameterStore::RecreateRedirections()
{
	UserParameterRedirects.Reset();

	for (const FNiagaraVariable Var : ReadParameterVariables())
	{
		if (IsUserParameter(Var))
		{
			UserParameterRedirects.Add(GetUserRedirection(Var), Var);
		}
	}
}

bool FNiagaraUserRedirectionParameterStore::RedirectUserVariable(FNiagaraVariableBase& UserVar) const
{
	if (const FNiagaraVariable* RedirectedKey = UserParameterRedirects.Find(UserVar))
	{
		UserVar = FNiagaraVariableBase(*RedirectedKey);
		return true;
	}

	if (IsUserParameter(UserVar))
	{
		return true;
	}

	return false;
}

bool FNiagaraUserRedirectionParameterStore::AddParameter(const FNiagaraVariable& Param, bool bInitialize /*= true*/, bool bTriggerRebind /*= true*/, int32* OutOffset /*= nullptr*/)
{
	FNiagaraVariable AddParam;
	if (IsUserParameter(Param))
	{
		AddParam = Param;
	}
	else
	{
		AddParam = FNiagaraVariable(Param.GetType(), *(TEXT("User.") + Param.GetName().ToString()));
	}

	UserParameterRedirects.Add(GetUserRedirection(AddParam), AddParam);
	return Super::AddParameter(AddParam, bInitialize, bTriggerRebind, OutOffset);
}

bool FNiagaraUserRedirectionParameterStore::RemoveParameter(const FNiagaraVariableBase& InVar)
{
	const FNiagaraVariable* Redirection = UserParameterRedirects.Find(InVar);
	const FNiagaraVariable& ToRemove = Redirection ? *Redirection : InVar;
	bool Result = Super::RemoveParameter(ToRemove);
	if (Result)
	{
		UserParameterRedirects.Remove(GetUserRedirection(ToRemove));
	}
	return Result;
}

void FNiagaraUserRedirectionParameterStore::InitFromSource(const FNiagaraParameterStore* SrcStore, bool bNotifyAsDirty)
{
	Super::InitFromSource(SrcStore, bNotifyAsDirty);
	RecreateRedirections();
}

void FNiagaraUserRedirectionParameterStore::Empty(bool bClearBindings /*= true*/)
{
	Super::Empty(bClearBindings);
	UserParameterRedirects.Empty();
}

void FNiagaraUserRedirectionParameterStore::Reset(bool bClearBindings /*= true*/)
{
	Super::Reset(bClearBindings);
	UserParameterRedirects.Reset();
}

bool FNiagaraUserRedirectionParameterStore::SerializeFromMismatchedTag(const FPropertyTag & Tag, FStructuredArchive::FSlot Slot)
{
	static FName StoreDataName("NiagaraParameterStore");
	if (Tag.Type == NAME_StructProperty && Tag.StructName == StoreDataName)
	{
		FNiagaraParameterStore OldStore;
		FNiagaraParameterStore::StaticStruct()->SerializeItem(Slot, &OldStore, nullptr);
		// Call PostLoad() to convert the serialized ParameterOffsets to SortedParameterOffsets.
		OldStore.PostLoad();
		*this = OldStore;
		return true;
	}

	return false;
}
