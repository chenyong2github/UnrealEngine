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
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
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
}
#endif

bool UNiagaraDataInterfaceArray::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceArray* OtherTyped = CastChecked<UNiagaraDataInterfaceArray>(Destination);
	OtherTyped->MaxElements = MaxElements;
	OtherTyped->GpuSyncMode = GpuSyncMode;
	return GetProxyAs<INDIArrayProxyBase>()->CopyToInternal(OtherTyped->GetProxyAs<INDIArrayProxyBase>());
}

bool UNiagaraDataInterfaceArray::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceArray* OtherTyped = CastChecked<UNiagaraDataInterfaceArray>(Other);
	if ((OtherTyped->MaxElements != MaxElements) ||
		(OtherTyped->GpuSyncMode != GpuSyncMode))
	{
		return false;
	}

	return GetProxyAs<INDIArrayProxyBase>()->Equals(OtherTyped->GetProxyAs<INDIArrayProxyBase>());
}

#undef LOCTEXT_NAMESPACE
