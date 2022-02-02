// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraDataInterfaceArrayFloat.generated.h"

template<>
struct FNDIArrayImplHelper<float> : public FNDIArrayImplHelperBase<float>
{
	typedef float TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float");
	static constexpr EPixelFormat ReadPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index] = Value;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetFloatDef(); }
	static const float GetDefaultValue() { return 0.0f; }
};

template<>
struct FNDIArrayImplHelper<FVector2f> : public FNDIArrayImplHelperBase<FVector2f>
{
	typedef FVector2f TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float2");
	static constexpr EPixelFormat ReadPixelFormat = PF_G32R32F;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float2");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = float2(BUFFER_NAME[Index]);");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = float2(BUFFER_NAME[Index * 2 + 0], BUFFER_NAME[Index * 2 + 1]);");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index * 2 + 0] = Value.x, BUFFER_NAME[Index * 2 + 1] = Value.y;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec2Def(); }
	static const FVector2f GetDefaultValue() { return FVector2f::ZeroVector; }
};

// LWC_TODO: This is represented as an FVector2f internally (array is converted to floats during PushToRenderThread)
template<>
struct FNDIArrayImplHelper<FVector2d> : public FNDIArrayImplHelper<FVector2f>
{
	static void CopyToGpuMemory(void* Dest, const FVector2d* Src, int32 NumElements)
	{
		FVector2f* TypedDest = reinterpret_cast<FVector2f*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector2f(*Src++);
		}
	}

	static void CopyToCpuMemory(void* Dest, const FVector2f* Src, int32 NumElements)
	{
		FVector2d* TypedDest = reinterpret_cast<FVector2d*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector2d(*Src++);
		}
	}
};

template<>
struct FNDIArrayImplHelper<FVector3f> : public FNDIArrayImplHelperBase<FVector3f>
{
	typedef FVector3f TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float3");
	static constexpr EPixelFormat ReadPixelFormat = PF_R32_FLOAT;		// Lack of float3 format
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = float3(BUFFER_NAME[Index * 3 + 0], BUFFER_NAME[Index * 3 + 1], BUFFER_NAME[Index * 3 + 2]);");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = float3(BUFFER_NAME[Index * 3 + 0], BUFFER_NAME[Index * 3 + 1], BUFFER_NAME[Index * 3 + 2]);");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index * 3 + 0] = Value.x, BUFFER_NAME[Index * 3 + 1] = Value.y, BUFFER_NAME[Index * 3 + 2] = Value.z;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec3Def(); }
	static const FVector3f GetDefaultValue() { return FVector3f::ZeroVector; }
};

// LWC_TODO: This is represented as an FVector3f internally (array is converted to floats during PushToRenderThread)
template<>
struct FNDIArrayImplHelper<FVector3d> : public FNDIArrayImplHelper<FVector3f>
{
	static void CopyToGpuMemory(void* Dest, const FVector3d* Src, int32 NumElements)
	{
		FVector3f* TypedDest = reinterpret_cast<FVector3f*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector3f(*Src++);
		}
	}


	static void CopyToCpuMemory(void* Dest, const FVector3f* Src, int32 NumElements)
	{
		FVector3d* TypedDest = reinterpret_cast<FVector3d*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector3d(*Src++);
		}
	}
};

template<>
struct FNDIArrayImplHelper<FNiagaraPosition> : public FNDIArrayImplHelper<FVector3f>
{
	typedef FNiagaraPosition TVMArrayType;

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetPositionDef(); }
	static const FNiagaraPosition GetDefaultValue() { return FVector3f::ZeroVector; }
};

template<>
struct FNDIArrayImplHelper<FVector4f> : public FNDIArrayImplHelperBase<FVector4f>
{
	typedef FVector4f TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float4");
	static constexpr EPixelFormat ReadPixelFormat = PF_A32B32G32R32F;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float4");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = float4(BUFFER_NAME[Index * 4 + 0], BUFFER_NAME[Index * 4 + 1], BUFFER_NAME[Index * 4 + 2], BUFFER_NAME[Index * 4 + 3]);");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index * 4 + 0] = Value.x, BUFFER_NAME[Index * 4 + 1] = Value.y, BUFFER_NAME[Index * 4 + 2] = Value.z, BUFFER_NAME[Index * 4 + 3] = Value.w;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetVec4Def(); }
	static const FVector4f GetDefaultValue() { return FVector4f(ForceInitToZero); }
};

// LWC_TODO: This is represented as an FVector4f internally (array is converted to floats during PushToRenderThread)
template<>
struct FNDIArrayImplHelper<FVector4d> : public FNDIArrayImplHelper<FVector4f>
{
	static void CopyToGpuMemory(void* Dest, const FVector4d* Src, int32 NumElements)
	{
		FVector4f* TypedDest = reinterpret_cast<FVector4f*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector4f(*Src++);
		}
	}

	static void CopyToCpuMemory(void* Dest, const FVector4f* Src, int32 NumElements)
	{
		FVector4d* TypedDest = reinterpret_cast<FVector4d*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FVector4d(*Src++);
		}
	}
};

template<>
struct FNDIArrayImplHelper<FLinearColor> : public FNDIArrayImplHelperBase<FLinearColor>
{
	typedef FLinearColor TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float4");
	static constexpr EPixelFormat ReadPixelFormat = PF_A32B32G32R32F;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float4");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = float4(BUFFER_NAME[Index * 4 + 0], BUFFER_NAME[Index * 4 + 1], BUFFER_NAME[Index * 4 + 2], BUFFER_NAME[Index * 4 + 3]);");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index * 4 + 0] = Value.x, BUFFER_NAME[Index * 4 + 1] = Value.y, BUFFER_NAME[Index * 4 + 2] = Value.z, BUFFER_NAME[Index * 4 + 3] = Value.w;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetColorDef(); }
	static const FLinearColor GetDefaultValue() { return FLinearColor::White; }
};

template<>
struct FNDIArrayImplHelper<FQuat4f> : public FNDIArrayImplHelperBase<FQuat4f>
{
	typedef FQuat4f TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("float4");
	static constexpr EPixelFormat ReadPixelFormat = PF_A32B32G32R32F;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("float4");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_FLOAT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("float");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = float4(BUFFER_NAME[Index * 4 + 0], BUFFER_NAME[Index * 4 + 1], BUFFER_NAME[Index * 4 + 2], BUFFER_NAME[Index * 4 + 3]);");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index * 4 + 0] = Value.x, BUFFER_NAME[Index * 4 + 1] = Value.y, BUFFER_NAME[Index * 4 + 2] = Value.z, BUFFER_NAME[Index * 4 + 3] = Value.w;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetQuatDef(); }
	static const FQuat4f GetDefaultValue() { return FQuat4f::Identity; }
};

// LWC_TODO: This is represented as an FQuat4f internally (array is converted to floats during PushToRenderThread)
template<>
struct FNDIArrayImplHelper<FQuat4d> : public FNDIArrayImplHelper<FQuat4f>
{
	static void CopyToGpuMemory(void* Dest, const FQuat4d* Src, int32 NumElements)
	{
		FQuat4f* TypedDest = reinterpret_cast<FQuat4f*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FQuat4f(*Src++);
		}
	}

	static void CopyToCpuMemory(void* Dest, const FQuat4f* Src, int32 NumElements)
	{
		FQuat4d* TypedDest = reinterpret_cast<FQuat4d*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = FQuat4d(*Src++);
		}
	}
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Float Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<float> FloatData;

	TArray<float>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector 2D Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat2 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector2D> FloatData;	// LWC_TODO: Should be FVector2f, but only FVector2D is blueprint accessible

	TArray<FVector2D>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat3 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector> FloatData;		// LWC_TODO: Should be FVector3f, but only FVector is blueprint accessible

	TArray<FVector>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Position Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayPosition : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FNiagaraPosition> PositionData;

	TArray<FNiagaraPosition>& GetArrayReference() { return PositionData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Vector 4 Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayFloat4 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FVector4> FloatData;		// LWC_TODO: Should be FVector4f, but only FVector4 is blueprint accessible

	TArray<FVector4>& GetArrayReference() { return FloatData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Color Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayColor : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FLinearColor> ColorData;

	TArray<FLinearColor>& GetArrayReference() { return ColorData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Quaternion Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayQuat : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<FQuat> QuatData;

	TArray<FQuat>& GetArrayReference() { return QuatData; }
};
