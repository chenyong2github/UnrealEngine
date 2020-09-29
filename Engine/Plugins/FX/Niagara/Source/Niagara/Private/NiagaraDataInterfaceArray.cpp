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

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArray::PostLoad()
{
	Super::PostLoad();
	MarkRenderDataDirty();
}

#if WITH_EDITOR
void UNiagaraDataInterfaceArray::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderDataDirty();
}
#endif

bool UNiagaraDataInterfaceArray::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceArray* OtherTyped = CastChecked<UNiagaraDataInterfaceArray>(Destination);
	OtherTyped->MarkRenderDataDirty();
	OtherTyped->MaxElements = MaxElements;
	if (ensureMsgf(Impl.IsValid(), TEXT("Impl should always be valid for %s"), *GetNameSafe(GetClass())))
	{
		return Impl->CopyToInternal(OtherTyped->Impl.Get());
	}
	return true;
}

bool UNiagaraDataInterfaceArray::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceArray* OtherTyped = CastChecked<UNiagaraDataInterfaceArray>(Other);
	if (OtherTyped->MaxElements != MaxElements)
	{
		return false;
	}

	if (ensureMsgf(Impl.IsValid(), TEXT("Impl should always be valid for %s"), *GetNameSafe(GetClass())))
	{
		return Impl->Equals(OtherTyped->Impl.Get());
	}
	return true;
}


void UNiagaraDataInterfaceArray::PushToRenderThreadImpl()
{
	if (Impl.IsValid())
	{
		Impl->PushToRenderThread();
	}
}

#undef LOCTEXT_NAMESPACE
