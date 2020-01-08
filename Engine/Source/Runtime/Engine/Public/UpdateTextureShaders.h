// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"

class FUpdateTexture2DSubresouceCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateTexture2DSubresouceCS,Global);
public:
	FUpdateTexture2DSubresouceCS() {}
	FUpdateTexture2DSubresouceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader( Initializer )
	{
		SrcPitchParameter.Bind(Initializer.ParameterMap, TEXT("SrcPitch"), SPF_Mandatory);
		SrcBuffer.Bind(Initializer.ParameterMap, TEXT("SrcBuffer"), SPF_Mandatory);
		DestPosSizeParameter.Bind(Initializer.ParameterMap, TEXT("DestPosSize"), SPF_Mandatory);
		DestTexture.Bind(Initializer.ParameterMap, TEXT("DestTexture"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcPitchParameter << SrcBuffer << DestPosSizeParameter << DestTexture;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

//protected:
	FShaderParameter SrcPitchParameter;
	FShaderResourceParameter SrcBuffer;
	FShaderParameter DestPosSizeParameter;
	FShaderResourceParameter DestTexture;
};

template<uint32 NumComponents>
class TUpdateTexture2DSubresouceCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TUpdateTexture2DSubresouceCS, Global);
public:
	TUpdateTexture2DSubresouceCS() {}
	TUpdateTexture2DSubresouceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcPitchParameter.Bind(Initializer.ParameterMap, TEXT("TSrcPitch"), SPF_Mandatory);
		SrcBuffer.Bind(Initializer.ParameterMap, TEXT("TSrcBuffer"), SPF_Mandatory);
		DestPosSizeParameter.Bind(Initializer.ParameterMap, TEXT("DestPosSize"), SPF_Mandatory);
		DestTexture.Bind(Initializer.ParameterMap, TEXT("TDestTexture"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcPitchParameter << SrcBuffer << DestPosSizeParameter << DestTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const TCHAR* Type = nullptr;
		static_assert(NumComponents >= 1u && NumComponents <= 4u, "Invalid NumComponents");
		switch (NumComponents)
		{
		case 1u: Type = TEXT("uint"); break;
		case 2u: Type = TEXT("uint2"); break;
		case 3u: Type = TEXT("uint3"); break;
		case 4u: Type = TEXT("uint4"); break;
		}

		OutEnvironment.SetDefine(TEXT("COMPONENT_TYPE"), Type);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	//protected:
	FShaderParameter SrcPitchParameter;
	FShaderResourceParameter SrcBuffer;
	FShaderParameter DestPosSizeParameter;
	FShaderResourceParameter DestTexture;
};

class FUpdateTexture3DSubresouceCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateTexture3DSubresouceCS, Global);
public:
	FUpdateTexture3DSubresouceCS() {}
	FUpdateTexture3DSubresouceCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcPitchParameter.Bind(Initializer.ParameterMap, TEXT("SrcPitch"), SPF_Mandatory);
		SrcDepthPitchParameter.Bind(Initializer.ParameterMap, TEXT("SrcDepthPitch"), SPF_Mandatory);

		SrcBuffer.Bind(Initializer.ParameterMap, TEXT("SrcBuffer"), SPF_Mandatory);

		DestPosParameter.Bind(Initializer.ParameterMap, TEXT("DestPos"), SPF_Mandatory);
		DestSizeParameter.Bind(Initializer.ParameterMap, TEXT("DestSize"), SPF_Mandatory);

		DestTexture3D.Bind(Initializer.ParameterMap, TEXT("DestTexture3D"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcPitchParameter << SrcDepthPitchParameter << SrcBuffer << DestPosParameter << DestSizeParameter  << DestTexture3D;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	//protected:
	FShaderParameter SrcPitchParameter;
	FShaderParameter SrcDepthPitchParameter;
	FShaderResourceParameter SrcBuffer;

	FShaderParameter DestPosParameter;
	FShaderParameter DestSizeParameter;

	FShaderResourceParameter DestTexture3D;
};

class FCopyTexture2DCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyTexture2DCS,Global);
public:
	FCopyTexture2DCS() {}
	FCopyTexture2DCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader( Initializer )
	{
		SrcTexture.Bind(Initializer.ParameterMap, TEXT("SrcTexture"), SPF_Mandatory);
		DestTexture.Bind(Initializer.ParameterMap, TEXT("DestTexture"), SPF_Mandatory);
		DestPosSize.Bind(Initializer.ParameterMap, TEXT("DestPosSize"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcTexture << DestTexture << DestPosSize;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

//protected:
	FShaderResourceParameter SrcTexture;
	FShaderResourceParameter DestTexture;
	FShaderParameter DestPosSize;
};

template<uint32 NumComponents, typename ComponentType = uint32>
class TCopyTexture2DCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TCopyTexture2DCS, Global);
public:
	TCopyTexture2DCS() {}
	TCopyTexture2DCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcTexture.Bind(Initializer.ParameterMap, TEXT("TSrcTexture"), SPF_Mandatory);
		DestTexture.Bind(Initializer.ParameterMap, TEXT("TDestTexture"), SPF_Mandatory);
		SrcPosParameter.Bind(Initializer.ParameterMap, TEXT("SrcPos"), SPF_Mandatory);
		DestPosSizeParameter.Bind(Initializer.ParameterMap, TEXT("DestPosSize"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcTexture << DestTexture << SrcPosParameter << DestPosSizeParameter;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetTypename(uint32 Dummy)
	{
		switch (NumComponents)
		{
		case 1u: return TEXT("uint"); break;
		case 2u: return TEXT("uint2"); break;
		case 3u: return TEXT("uint3"); break;
		case 4u: return TEXT("uint4"); break;
		}
		return nullptr;
	}

	static const TCHAR* GetTypename(float Dummy)
	{
		switch (NumComponents)
		{
		case 1u: return TEXT("float"); break;
		case 2u: return TEXT("float2"); break;
		case 3u: return TEXT("float3"); break;
		case 4u: return TEXT("float4"); break;
		}
		return nullptr;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		static_assert(NumComponents >= 1u && NumComponents <= 4u, "Invalid NumComponents");
		OutEnvironment.SetDefine(TEXT("COMPONENT_TYPE"), GetTypename((ComponentType)0));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FShaderResourceParameter SrcTexture;
	FShaderResourceParameter DestTexture;
	FShaderParameter SrcPosParameter;
	FShaderParameter DestPosSizeParameter;
};

template<uint32 ElementsPerThread>
class TCopyDataCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TCopyDataCS, Global);
public:
	TCopyDataCS() {}
	TCopyDataCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcBuffer.Bind(Initializer.ParameterMap, TEXT("SrcCopyBuffer"), SPF_Mandatory);
		DestBuffer.Bind(Initializer.ParameterMap, TEXT("DestBuffer"), SPF_Mandatory);		
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SrcBuffer << DestBuffer;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	//protected:
	FShaderResourceParameter SrcBuffer;
	FShaderResourceParameter DestBuffer;	
};
