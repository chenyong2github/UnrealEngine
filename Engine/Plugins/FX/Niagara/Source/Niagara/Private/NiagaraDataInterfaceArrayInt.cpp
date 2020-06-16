// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraRenderer.h"

template<>
struct FNDIArrayImplHelper<int32> : public FNDIArrayImplHelperBase<int32>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("int");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("int");
	static constexpr EPixelFormat PixelFormat = PF_R32_SINT;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyIntBuffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetIntDef(); }
};

UNiagaraDataInterfaceArrayInt32::UNiagaraDataInterfaceArrayInt32(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<int32, UNiagaraDataInterfaceArrayInt32>(Proxy, IntData));
}
