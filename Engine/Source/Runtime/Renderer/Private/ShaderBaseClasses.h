// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderBaseClasses.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "MeshMaterialShader.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;

/** The uniform shader parameters associated with a distance cull fade. */
// This was moved out of ScenePrivate.h to workaround MSVC vs clang template issue (it's used in this header file, so needs to be declared earlier)
// Z is the dither fade value (-1 = just fading in, 0 no fade, 1 = just faded out)
// W is unused and zero
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDistanceCullFadeUniformShaderParameters,)
	SHADER_PARAMETER_EX(FVector2D,FadeTimeScaleBias, EShaderPrecisionModifier::Half)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef< FDistanceCullFadeUniformShaderParameters > FDistanceCullFadeUniformBufferRef;

/** The uniform shader parameters associated with a LOD dither fade. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDitherUniformShaderParameters, )
	SHADER_PARAMETER_EX(float, LODFactor, EShaderPrecisionModifier::Half)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef< FDitherUniformShaderParameters > FDitherUniformBufferRef;


/** Base Hull shader for drawing policy rendering */
class FBaseHS : public FMeshMaterialShader
{
public:
	DECLARE_TYPE_LAYOUT(FBaseHS, NonVirtual);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (!RHISupportsTessellation(Parameters.Platform))
		{
			return false;
		}

		if (Parameters.VertexFactoryType && !Parameters.VertexFactoryType->SupportsTessellationShaders())
		{
			// VF can opt out of tessellation
			return false;	
		}

		if (Parameters.MaterialParameters.TessellationMode == MTM_NoTessellation)
		{
			// Material controls use of tessellation
			return false;	
		}

		return true;
	}

	FBaseHS() = default;
	FBaseHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

/** Base Domain shader for drawing policy rendering */
class FBaseDS : public FMeshMaterialShader
{
public:
	DECLARE_TYPE_LAYOUT(FBaseDS, NonVirtual);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (!RHISupportsTessellation(Parameters.Platform))
		{
			return false;
		}

		if (Parameters.VertexFactoryType && !Parameters.VertexFactoryType->SupportsTessellationShaders())
		{
			// VF can opt out of tessellation
			return false;
		}

		if (Parameters.MaterialParameters.TessellationMode == MTM_NoTessellation)
		{
			// Material controls use of tessellation
			return false;
		}

		return true;
	}

	FBaseDS() = default;
	FBaseDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};