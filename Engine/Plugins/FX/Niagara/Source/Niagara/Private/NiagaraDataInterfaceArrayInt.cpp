// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraRenderer.h"

UNiagaraDataInterfaceArrayInt32::UNiagaraDataInterfaceArrayInt32(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<int32, UNiagaraDataInterfaceArrayInt32>(this));
}

UNiagaraDataInterfaceArrayBool::UNiagaraDataInterfaceArrayBool(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	static_assert(sizeof(bool) == sizeof(uint8), "Bool != 1 byte this will mean the GPU array does not match in size");

	Proxy.Reset(new FNDIArrayProxyImpl<bool, UNiagaraDataInterfaceArrayBool>(this));
}
