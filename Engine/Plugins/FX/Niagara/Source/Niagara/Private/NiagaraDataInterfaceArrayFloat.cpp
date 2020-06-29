// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraRenderer.h"

template<>
struct FNDIArrayImplHelper<float> : public FNDIArrayImplHelperBase<float>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float");
	static constexpr EPixelFormat PixelFormat = PF_R32_FLOAT;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloatBuffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetFloatDef(); }
};

template<>
struct FNDIArrayImplHelper<FVector2D> : public FNDIArrayImplHelperBase<FVector2D>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float2");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float2");
	static constexpr EPixelFormat PixelFormat = PF_G32R32F;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloat2Buffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec2Def(); }
};

template<>
struct FNDIArrayImplHelper<FVector> : public FNDIArrayImplHelperBase<FVector>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float3");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float");	//-OPT: Current we have no float3 pixel format, when we add one update this to use it
	static constexpr EPixelFormat PixelFormat = PF_R32_FLOAT;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloatBuffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec3Def(); }

	static void GPUGetFetchHLSL(FString& OutHLSL, const TCHAR* BufferName)
	{
		OutHLSL.Appendf(TEXT("OutValue.x = %s[ClampedIndex * 3 + 0];"), BufferName);
		OutHLSL.Appendf(TEXT("OutValue.y = %s[ClampedIndex * 3 + 1];"), BufferName);
		OutHLSL.Appendf(TEXT("OutValue.z = %s[ClampedIndex * 3 + 2];"), BufferName);
	}
	static int32 GPUGetTypeStride()
	{
		return sizeof(float);
	}
};

template<>
struct FNDIArrayImplHelper<FVector4> : public FNDIArrayImplHelperBase<FVector4>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	static constexpr EPixelFormat PixelFormat = PF_A32B32G32R32F;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloat4Buffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec4Def(); }
};

template<>
struct FNDIArrayImplHelper<FLinearColor> : public FNDIArrayImplHelperBase<FLinearColor>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	static constexpr EPixelFormat PixelFormat = PF_A32B32G32R32F;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloat4Buffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetColorDef(); }
};

template<>
struct FNDIArrayImplHelper<FQuat> : public FNDIArrayImplHelperBase<FQuat>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	static constexpr EPixelFormat PixelFormat = PF_A32B32G32R32F;
	static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloat4Buffer(); }
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetQuatDef(); }
};

UNiagaraDataInterfaceArrayFloat::UNiagaraDataInterfaceArrayFloat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<float, UNiagaraDataInterfaceArrayFloat>(Proxy, FloatData));
}

UNiagaraDataInterfaceArrayFloat2::UNiagaraDataInterfaceArrayFloat2(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector2D, UNiagaraDataInterfaceArrayFloat2>(Proxy, FloatData));
}

UNiagaraDataInterfaceArrayFloat3::UNiagaraDataInterfaceArrayFloat3(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector, UNiagaraDataInterfaceArrayFloat3>(Proxy, FloatData));
}

UNiagaraDataInterfaceArrayFloat4::UNiagaraDataInterfaceArrayFloat4(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector4, UNiagaraDataInterfaceArrayFloat4>(Proxy, FloatData));
}

UNiagaraDataInterfaceArrayColor::UNiagaraDataInterfaceArrayColor(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FLinearColor, UNiagaraDataInterfaceArrayColor>(Proxy, ColorData));
}

UNiagaraDataInterfaceArrayQuat::UNiagaraDataInterfaceArrayQuat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FQuat, UNiagaraDataInterfaceArrayQuat>(Proxy, QuatData));
}
