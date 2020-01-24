// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

enum class ECopyTextureResourceType
{
	Texture2D      = 0,
	Texture2DArray = 1,
	Texture3D      = 2
};

enum class ECopyTextureValueType
{
	Float,
	Int32,
	Uint32
};

namespace CopyTextureCS
{
	template <ECopyTextureResourceType> struct TThreadGroupSize              { static constexpr int32 X = 8, Y = 8, Z = 1; };
	template <> struct TThreadGroupSize<ECopyTextureResourceType::Texture3D> { static constexpr int32 X = 4, Y = 4, Z = 4; };
}

class FCopyTextureCS : public FGlobalShader
{
protected:
	FCopyTextureCS() {}
	FCopyTextureCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DstOffsetParam.Bind(Initializer.ParameterMap, TEXT("DstOffset"), SPF_Mandatory);
		SrcOffsetParam.Bind(Initializer.ParameterMap, TEXT("SrcOffset"), SPF_Mandatory);
		DimensionsParam.Bind(Initializer.ParameterMap, TEXT("Dimensions"), SPF_Mandatory);
		SrcResourceParam.Bind(Initializer.ParameterMap, TEXT("SrcResource"), SPF_Mandatory);
		DstResourceParam.Bind(Initializer.ParameterMap, TEXT("DstResource"), SPF_Mandatory);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << DstOffsetParam;
		Ar << SrcOffsetParam;
		Ar << DimensionsParam;
		Ar << SrcResourceParam;
		Ar << DstResourceParam;

		return bShaderHasOutdatedParameters;
	}

public:
	inline const FShaderResourceParameter& GetSrcResourceParam() { return SrcResourceParam; }
	inline const FShaderResourceParameter& GetDstResourceParam() { return DstResourceParam; }

	virtual void Dispatch(
		FRHIComputeCommandList& RHICmdList,
		FIntVector const& SrcOffset,
		FIntVector const& DstOffset,
		FIntVector const& Dimensions
	) = 0;

	static inline FCopyTextureCS* SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureResourceType SrcType, ECopyTextureResourceType DstType, ECopyTextureValueType ValueType);

protected:
	template <ECopyTextureResourceType SrcType>
	static inline FCopyTextureCS* SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureResourceType DstType, ECopyTextureValueType ValueType);

	template <ECopyTextureResourceType SrcType, ECopyTextureResourceType DstType>
	static inline FCopyTextureCS* SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureValueType ValueType);

	FShaderParameter DstOffsetParam;
	FShaderParameter SrcOffsetParam;
	FShaderParameter DimensionsParam;
	FShaderResourceParameter SrcResourceParam;
	FShaderResourceParameter DstResourceParam;
};

template <ECopyTextureResourceType SrcType, ECopyTextureResourceType DstType, ECopyTextureValueType ValueType, uint32 NumChannels>
class TCopyResourceCS : public FCopyTextureCS
{
	static_assert(NumChannels >= 1 && NumChannels <= 4, "Only 1 to 4 channels are supported.");

	DECLARE_EXPORTED_SHADER_TYPE(TCopyResourceCS, Global, RENDERCORE_API);

public:
	TCopyResourceCS() {}
	TCopyResourceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FCopyTextureCS(Initializer)
	{}

	static constexpr uint32 ThreadGroupSizeX = CopyTextureCS::TThreadGroupSize<DstType>::X;
	static constexpr uint32 ThreadGroupSizeY = CopyTextureCS::TThreadGroupSize<DstType>::Y;
	static constexpr uint32 ThreadGroupSizeZ = CopyTextureCS::TThreadGroupSize<DstType>::Z;

	static const TCHAR* GetSourceFilename() { return TEXT("/Engine/Private/CopyTextureShaders.usf"); }
	static const TCHAR* GetFunctionName() { return TEXT("CopyTextureCS"); }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), ThreadGroupSizeZ);
		OutEnvironment.SetDefine(TEXT("SRC_TYPE"), uint32(SrcType));
		OutEnvironment.SetDefine(TEXT("DST_TYPE"), uint32(DstType));

		switch (ValueType)
		{
		case ECopyTextureValueType::Float:
			switch (NumChannels)
			{
			case 1: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("float"));  break;
			case 2: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("float2")); break;
			case 3: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("float3")); break;
			case 4: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("float4")); break;
			}
			break;

		case ECopyTextureValueType::Int32:
			switch (NumChannels)
			{
			case 1: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("int"));  break;
			case 2: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("int2")); break;
			case 3: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("int3")); break;
			case 4: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("int4")); break;
			}
			break;

		case ECopyTextureValueType::Uint32:
			switch (NumChannels)
			{
			case 1: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("uint"));  break;
			case 2: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("uint2")); break;
			case 3: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("uint3")); break;
			case 4: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("uint4")); break;
			}
			break;
		}
	}

	virtual void Dispatch(
		FRHIComputeCommandList& RHICmdList,
		FIntVector const& SrcOffset,
		FIntVector const& DstOffset,
		FIntVector const& Dimensions) override final
	{
		check(SrcOffset.GetMin() >= 0 && DstOffset.GetMin() >= 0 && Dimensions.GetMin() >= 0);
		check(DstType != ECopyTextureResourceType::Texture2D || Dimensions.Z <= 1);

		FRHIComputeShader* ShaderRHI = GetComputeShader();
		SetShaderValue(RHICmdList, ShaderRHI, SrcOffsetParam, SrcOffset);
		SetShaderValue(RHICmdList, ShaderRHI, DstOffsetParam, DstOffset);
		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParam, Dimensions);

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp(uint32(Dimensions.X), ThreadGroupSizeX),
			FMath::DivideAndRoundUp(uint32(Dimensions.Y), ThreadGroupSizeY),
			FMath::DivideAndRoundUp(uint32(Dimensions.Z), ThreadGroupSizeZ)
		);
	}
};

template <ECopyTextureResourceType SrcType, ECopyTextureResourceType DstType>
inline FCopyTextureCS* FCopyTextureCS::SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureValueType ValueType)
{
	switch (ValueType)
	{
	default: checkNoEntry();
	case ECopyTextureValueType::Float:  return GlobalShaderMap->GetShader<TCopyResourceCS<SrcType, DstType, ECopyTextureValueType::Float,  4>>();
	case ECopyTextureValueType::Int32:  return GlobalShaderMap->GetShader<TCopyResourceCS<SrcType, DstType, ECopyTextureValueType::Int32,  4>>();
	case ECopyTextureValueType::Uint32: return GlobalShaderMap->GetShader<TCopyResourceCS<SrcType, DstType, ECopyTextureValueType::Uint32, 4>>();
	}
}

template <ECopyTextureResourceType SrcType>
inline FCopyTextureCS* FCopyTextureCS::SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureResourceType DstType, ECopyTextureValueType ValueType)
{
	switch (DstType)
	{
	default: checkNoEntry();
	case ECopyTextureResourceType::Texture2D:	   return FCopyTextureCS::SelectShader<SrcType, ECopyTextureResourceType::Texture2D     >(GlobalShaderMap, ValueType);
	case ECopyTextureResourceType::Texture2DArray: return FCopyTextureCS::SelectShader<SrcType, ECopyTextureResourceType::Texture2DArray>(GlobalShaderMap, ValueType);
	case ECopyTextureResourceType::Texture3D:      return FCopyTextureCS::SelectShader<SrcType, ECopyTextureResourceType::Texture3D     >(GlobalShaderMap, ValueType);
	}
}

inline FCopyTextureCS* FCopyTextureCS::SelectShader(TShaderMap<FGlobalShaderType>* GlobalShaderMap, ECopyTextureResourceType SrcType, ECopyTextureResourceType DstType, ECopyTextureValueType ValueType)
{
	switch (SrcType)
	{
	default: checkNoEntry();
	case ECopyTextureResourceType::Texture2D:	   return FCopyTextureCS::SelectShader<ECopyTextureResourceType::Texture2D     >(GlobalShaderMap, DstType, ValueType);
	case ECopyTextureResourceType::Texture2DArray: return FCopyTextureCS::SelectShader<ECopyTextureResourceType::Texture2DArray>(GlobalShaderMap, DstType, ValueType);
	case ECopyTextureResourceType::Texture3D:      return FCopyTextureCS::SelectShader<ECopyTextureResourceType::Texture3D     >(GlobalShaderMap, DstType, ValueType);
	}
}
