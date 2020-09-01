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
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetFloatDef(); }
	static const float GetDefaultValue() { return 0.0f; }
};

template<>
struct FNDIArrayImplHelper<FVector2D> : public FNDIArrayImplHelperBase<FVector2D>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float2");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float2");
	static constexpr EPixelFormat PixelFormat = PF_G32R32F;
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec2Def(); }
	static const FVector2D GetDefaultValue() { return FVector2D::ZeroVector; }
};

template<>
struct FNDIArrayImplHelper<FVector> : public FNDIArrayImplHelperBase<FVector>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float3");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float");	//-OPT: Current we have no float3 pixel format, when we add one update this to use it
	static constexpr EPixelFormat PixelFormat = PF_R32_FLOAT;
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec3Def(); }
	static const FVector GetDefaultValue() { return FVector::ZeroVector; }

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
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec4Def(); }
	static const FVector4 GetDefaultValue() { return FVector4(ForceInitToZero); }
};

template<>
struct FNDIArrayImplHelper<FLinearColor> : public FNDIArrayImplHelperBase<FLinearColor>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	static constexpr EPixelFormat PixelFormat = PF_A32B32G32R32F;
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetColorDef(); }
	static const FLinearColor GetDefaultValue() { return FLinearColor::White; }
};

template<>
struct FNDIArrayImplHelper<FQuat> : public FNDIArrayImplHelperBase<FQuat>
{
	static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	static constexpr EPixelFormat PixelFormat = PF_A32B32G32R32F;
	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetQuatDef(); }
	static const FQuat GetDefaultValue() { return FQuat::Identity; }
};

UNiagaraDataInterfaceArrayFloat::UNiagaraDataInterfaceArrayFloat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<float, UNiagaraDataInterfaceArrayFloat>(this, FloatData));
}

UNiagaraDataInterfaceArrayFloat2::UNiagaraDataInterfaceArrayFloat2(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector2D, UNiagaraDataInterfaceArrayFloat2>(this, FloatData));
}

UNiagaraDataInterfaceArrayFloat3::UNiagaraDataInterfaceArrayFloat3(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector, UNiagaraDataInterfaceArrayFloat3>(this, FloatData));
}

UNiagaraDataInterfaceArrayFloat4::UNiagaraDataInterfaceArrayFloat4(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FVector4, UNiagaraDataInterfaceArrayFloat4>(this, FloatData));
}

UNiagaraDataInterfaceArrayColor::UNiagaraDataInterfaceArrayColor(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FLinearColor, UNiagaraDataInterfaceArrayColor>(this, ColorData));
}

UNiagaraDataInterfaceArrayQuat::UNiagaraDataInterfaceArrayQuat(FObjectInitializer const& ObjectInitializer)
	: UNiagaraDataInterfaceArray(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyArrayImpl());
	Impl.Reset(new FNiagaraDataInterfaceArrayImpl<FQuat, UNiagaraDataInterfaceArrayQuat>(this, QuatData));
}
