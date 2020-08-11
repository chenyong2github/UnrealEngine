// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArray.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceArray"

UNiagaraDataInterfaceArray::UNiagaraDataInterfaceArray(FObjectInitializer const& ObjectInitializer)
{
}

void UNiagaraDataInterfaceArray::PostInitProperties()
{
	Super::PostInitProperties();
	
	if (HasAnyFlags(RF_ClassDefaultObject) && (GetClass() != UNiagaraDataInterfaceArray::StaticClass()))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}

	if (Impl.IsValid())
	{
		Impl->PushToRenderThread();
	}
}

void UNiagaraDataInterfaceArray::PostLoad()
{
	Super::PostLoad();
	if (Impl.IsValid())
	{
		Impl->PushToRenderThread();
	}
}

#if WITH_EDITOR
void UNiagaraDataInterfaceArray::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (Impl.IsValid())
	{
		Impl->PushToRenderThread();
	}
}
#endif

bool UNiagaraDataInterfaceArray::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	if (ensureMsgf(Impl.IsValid(), TEXT("Impl should always be valid for %s"), *GetNameSafe(GetClass())))
	{
		return Impl->CopyToInternal(CastChecked<UNiagaraDataInterfaceArray>(Destination)->Impl.Get());
	}
	return true;
}

bool UNiagaraDataInterfaceArray::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	if (ensureMsgf(Impl.IsValid(), TEXT("Impl should always be valid for %s"), *GetNameSafe(GetClass())))
	{
		return Impl->Equals(CastChecked<UNiagaraDataInterfaceArray>(Other)->Impl.Get());
	}
	return true;
}

void UNiagaraDataInterfaceArray::UpdateGPU()
{
	if (Impl.IsValid())
	{
		Impl->PushToRenderThread();
	}
}

#undef LOCTEXT_NAMESPACE
