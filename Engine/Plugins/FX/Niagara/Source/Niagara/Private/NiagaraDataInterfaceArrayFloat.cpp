// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraRenderer.h"

UNiagaraDataInterfaceArrayFloat::UNiagaraDataInterfaceArrayFloat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<float, UNiagaraDataInterfaceArrayFloat>(this));
}

UNiagaraDataInterfaceArrayFloat2::UNiagaraDataInterfaceArrayFloat2(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FVector2D, UNiagaraDataInterfaceArrayFloat2>(this));
}

UNiagaraDataInterfaceArrayFloat3::UNiagaraDataInterfaceArrayFloat3(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FVector, UNiagaraDataInterfaceArrayFloat3>(this));
}

UNiagaraDataInterfaceArrayPosition::UNiagaraDataInterfaceArrayPosition(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FNiagaraPosition, UNiagaraDataInterfaceArrayPosition>(this));
}

UNiagaraDataInterfaceArrayFloat4::UNiagaraDataInterfaceArrayFloat4(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FVector4, UNiagaraDataInterfaceArrayFloat4>(this));
}

UNiagaraDataInterfaceArrayColor::UNiagaraDataInterfaceArrayColor(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FLinearColor, UNiagaraDataInterfaceArrayColor>(this));
}

UNiagaraDataInterfaceArrayQuat::UNiagaraDataInterfaceArrayQuat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNDIArrayProxyImpl<FQuat, UNiagaraDataInterfaceArrayQuat>(this));
}
