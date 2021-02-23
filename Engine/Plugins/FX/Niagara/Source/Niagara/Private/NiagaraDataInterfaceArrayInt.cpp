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
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetIntDef(); }
	static const int32 GetDefaultValue() { return 0; }
};

template<>
struct FNDIArrayImplHelper<bool> : public FNDIArrayImplHelperBase<bool>
{
	typedef FNiagaraBool TVMArrayType;
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("bool");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("uint");
	static constexpr EPixelFormat PixelFormat = PF_R8_UINT;
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetBoolDef(); }
	static const bool GetDefaultValue() { return false; }
};

UNiagaraDataInterfaceArrayInt32::UNiagaraDataInterfaceArrayInt32(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<int32, UNiagaraDataInterfaceArrayInt32>(this, IntData));
}

UNiagaraDataInterfaceArrayBool::UNiagaraDataInterfaceArrayBool(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	static_assert(sizeof(bool) == sizeof(uint8), "Bool != 1 byte this will mean the GPU array does not match in size");

	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<bool, UNiagaraDataInterfaceArrayBool>(this, BoolData));
}
