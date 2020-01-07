// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"

enum class EClearReplacementResourceType
{
	Buffer = 0,
	Texture2D = 1,
	Texture2DArray = 2,
	Texture3D = 3
};

template <typename ValueType, uint32 NumChannels, bool bZeroOutput = false, bool bEnableBounds = false>
struct TClearReplacementBase : public FGlobalShader
{
	static_assert(NumChannels >= 1 && NumChannels <= 4, "Only 1 to 4 channels are supported.");
	static_assert(TIsSame<ValueType, float>::Value || TIsSame<ValueType, uint32>::Value, "Type must be float or uint32.");
	static const bool bIsFloat = TIsSame<ValueType, float>::Value;

protected:
	TClearReplacementBase() {}
	TClearReplacementBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		if (!bZeroOutput)
		{
			ClearValueParam.Bind(Initializer.ParameterMap, TEXT("ClearValue"), SPF_Mandatory);
		}
		if (bEnableBounds)
		{
			MinBoundsParam.Bind(Initializer.ParameterMap, TEXT("MinBounds"), SPF_Mandatory);
			MaxBoundsParam.Bind(Initializer.ParameterMap, TEXT("MaxBounds"), SPF_Mandatory);
		}
	}

public:
	static const TCHAR* GetSourceFilename() { return TEXT("/Engine/Private/ClearReplacementShaders.usf"); }

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("ENABLE_CLEAR_VALUE"), !bZeroOutput);
		OutEnvironment.SetDefine(TEXT("ENABLE_BOUNDS"), bEnableBounds);

		switch (NumChannels)
		{
		case 1: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), bIsFloat ? TEXT("float")  : TEXT("uint"));  break;
		case 2: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), bIsFloat ? TEXT("float2") : TEXT("uint2")); break;
		case 3: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), bIsFloat ? TEXT("float3") : TEXT("uint3")); break;
		case 4: OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), bIsFloat ? TEXT("float4") : TEXT("uint4")); break;
		}
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		if (!bZeroOutput)
		{
			Ar << ClearValueParam;
		}
		if (bEnableBounds)
		{
			Ar << MinBoundsParam;
			Ar << MaxBoundsParam;
		}

		return bShaderHasOutdatedParameters;
	}

	template <typename T = const FShaderParameter&>	inline typename TEnableIf<!bZeroOutput, T>::Type GetClearValueParam() const { return ClearValueParam; }
	template <typename T = const FShaderParameter&> inline typename TEnableIf<bEnableBounds, T>::Type GetMinBoundsParam() const { return MinBoundsParam;  }
	template <typename T = const FShaderParameter&> inline typename TEnableIf<bEnableBounds, T>::Type GetMaxBoundsParam() const { return MaxBoundsParam;  }

private:
	FShaderParameter ClearValueParam;
	FShaderParameter MinBoundsParam;
	FShaderParameter MaxBoundsParam;
};

namespace ClearReplacementCS
{
	template <EClearReplacementResourceType> struct TThreadGroupSize {};
	template <> struct TThreadGroupSize<EClearReplacementResourceType::Buffer>         { static constexpr int32 X = 64, Y = 1, Z = 1; };
	template <> struct TThreadGroupSize<EClearReplacementResourceType::Texture2D>      { static constexpr int32 X =  8, Y = 8, Z = 1; };
	template <> struct TThreadGroupSize<EClearReplacementResourceType::Texture2DArray> { static constexpr int32 X =  8, Y = 8, Z = 1; };
	template <> struct TThreadGroupSize<EClearReplacementResourceType::Texture3D>      { static constexpr int32 X =  4, Y = 4, Z = 4; };
}

template <EClearReplacementResourceType ResourceType, typename BaseType>
class TClearReplacementCS : public BaseType
{
	DECLARE_EXPORTED_SHADER_TYPE(TClearReplacementCS, Global, RENDERCORE_API);

public:
	static constexpr uint32 ThreadGroupSizeX = ClearReplacementCS::TThreadGroupSize<ResourceType>::X;
	static constexpr uint32 ThreadGroupSizeY = ClearReplacementCS::TThreadGroupSize<ResourceType>::Y;
	static constexpr uint32 ThreadGroupSizeZ = ClearReplacementCS::TThreadGroupSize<ResourceType>::Z;

	TClearReplacementCS() {}
	TClearReplacementCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: BaseType(Initializer)
	{
		ClearResourceParam.Bind(Initializer.ParameterMap, TEXT("ClearResource"), SPF_Mandatory);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		BaseType::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), ThreadGroupSizeZ);
		OutEnvironment.SetDefine(TEXT("RESOURCE_TYPE"), uint32(ResourceType));
	}

	static const TCHAR* GetFunctionName() { return TEXT("ClearCS"); }

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = BaseType::Serialize(Ar);

		Ar << ClearResourceParam;

		return bShaderHasOutdatedParameters;
	}

	inline void SetResource(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV)
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, UAV);
		SetUAVParameter(RHICmdList, BaseType::GetComputeShader(), ClearResourceParam, UAV);
	}

	inline void FinalizeResource(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV)
	{
		SetUAVParameter(RHICmdList, BaseType::GetComputeShader(), ClearResourceParam, nullptr);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAV);
	}

	inline uint32 GetResourceParamIndex() const { return ClearResourceParam.GetBaseIndex(); }

private:
	FShaderResourceParameter ClearResourceParam;
};

template <bool bEnableDepth, typename BaseType>
class TClearReplacementVS : public BaseType
{
	DECLARE_EXPORTED_SHADER_TYPE(TClearReplacementVS, Global, RENDERCORE_API);

public:
	TClearReplacementVS() {}
	TClearReplacementVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: BaseType(Initializer)
	{
		if (bEnableDepth)
		{
			DepthParam.Bind(Initializer.ParameterMap, TEXT("Depth"), SPF_Mandatory);
		}
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		BaseType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_DEPTH"), bEnableDepth);
	}

	static const TCHAR* GetFunctionName() { return TEXT("ClearVS"); }

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = BaseType::Serialize(Ar);

		if (bEnableDepth)
		{
			Ar << DepthParam;
		}

		return bShaderHasOutdatedParameters;
	}

	template <typename T = const FShaderParameter&>
	inline typename TEnableIf<bEnableDepth, T>::Type GetDepthParam() const
	{
		return DepthParam;
	}

private:
	FShaderParameter DepthParam;
};

template <bool b128BitOutput, typename BaseType>
class TClearReplacementPS : public BaseType
{
	DECLARE_EXPORTED_SHADER_TYPE(TClearReplacementPS, Global, RENDERCORE_API);

public:
	TClearReplacementPS() {}
	TClearReplacementPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: BaseType(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		BaseType::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		if (b128BitOutput)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		}
		OutEnvironment.SetDefine(TEXT("ENABLE_DEPTH"), false);
	}

	static const TCHAR* GetFunctionName() { return TEXT("ClearPS"); }
};

// Not all combinations are defined here. Add more if required.
//                             Type  NC  Zero   Bounds
typedef TClearReplacementBase<uint32, 1, false, false> FClearReplacementBase_Uint;
typedef TClearReplacementBase<uint32, 4, false, false> FClearReplacementBase_Uint4;
typedef TClearReplacementBase<float , 4, false, false> FClearReplacementBase_Float4;
typedef TClearReplacementBase<uint32, 1, true , false> FClearReplacementBase_Uint_Zero;
typedef TClearReplacementBase<float , 4, true , false> FClearReplacementBase_Float4_Zero;
typedef TClearReplacementBase<float , 4, true , true > FClearReplacementBase_Float4_Zero_Bounds;
typedef TClearReplacementBase<uint32, 1, false, true > FClearReplacementBase_Uint_Bounds;
typedef TClearReplacementBase<float , 4, false, true > FClearReplacementBase_Float4_Bounds;

// Simple vertex shaders for generating screen quads. Optionally with a min/max bounds in NDC space, and depth value.
typedef TClearReplacementVS<false, FClearReplacementBase_Float4_Zero       > FClearReplacementVS;
typedef TClearReplacementVS<false, FClearReplacementBase_Float4_Zero_Bounds> FClearReplacementVS_Bounds;
typedef TClearReplacementVS<true,  FClearReplacementBase_Float4_Zero       > FClearReplacementVS_Depth;

// Simple pixel shader which outputs a specified solid color to MRT0.
typedef TClearReplacementPS<false, FClearReplacementBase_Float4>             FClearReplacementPS;
typedef TClearReplacementPS<true,  FClearReplacementBase_Float4>             FClearReplacementPS_128;
// Simple pixel shader which outputs zero to MRT0
typedef TClearReplacementPS<false, FClearReplacementBase_Float4_Zero>        FClearReplacementPS_Zero;

// Compute shaders for clearing each resource type, with a min/max bounds enabled.
typedef TClearReplacementCS<EClearReplacementResourceType::Buffer,         FClearReplacementBase_Uint_Bounds>   FClearReplacementCS_Buffer_Uint_Bounds;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2D,      FClearReplacementBase_Float4_Bounds> FClearReplacementCS_Texture2D_Float4_Bounds;

// Compute shaders for clearing each resource type. No bounds checks enabled.
typedef TClearReplacementCS<EClearReplacementResourceType::Buffer,         FClearReplacementBase_Uint_Zero>     FClearReplacementCS_Buffer_Uint_Zero;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, FClearReplacementBase_Uint_Zero>     FClearReplacementCS_Texture2DArray_Uint_Zero;
typedef TClearReplacementCS<EClearReplacementResourceType::Buffer,         FClearReplacementBase_Uint>          FClearReplacementCS_Buffer_Uint;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, FClearReplacementBase_Uint>          FClearReplacementCS_Texture2DArray_Uint;

typedef TClearReplacementCS<EClearReplacementResourceType::Texture3D,      FClearReplacementBase_Float4>        FClearReplacementCS_Texture3D_Float4;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2D,      FClearReplacementBase_Float4>        FClearReplacementCS_Texture2D_Float4;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, FClearReplacementBase_Float4>        FClearReplacementCS_Texture2DArray_Float4;

// Used by ClearUAV_T in ClearQuad.cpp
typedef TClearReplacementCS<EClearReplacementResourceType::Texture3D,      FClearReplacementBase_Uint4>         FClearReplacementCS_Texture3D_Uint4;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2D,      FClearReplacementBase_Uint4>         FClearReplacementCS_Texture2D_Uint4;
typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, FClearReplacementBase_Uint4>         FClearReplacementCS_Texture2DArray_Uint4;
