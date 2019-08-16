// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParticleVertexFactory.cpp: Particle vertex factory implementation.
=============================================================================*/

#include "NiagaraSpriteVertexFactory.h"
#include "NiagaraCutoutVertexBuffer.h"
#include "ParticleHelper.h"
#include "ParticleResources.h"
#include "ShaderParameterUtils.h"
#include "MeshMaterialShader.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraSpriteUniformParameters,"NiagaraSpriteVF");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraSpriteVFLooseParameters, "NiagaraSpriteVFLooseParameters");

TGlobalResource<FNullDynamicParameterVertexBuffer> GNullNiagaraDynamicParameterVertexBuffer;

/**
 * Shader parameters for the particle vertex factory.
 */
class FNiagaraSpriteVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
public:

	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
	}

	virtual void Serialize(FArchive& Ar) override
	{
	}
};

class FNiagaraSpriteVertexFactoryShaderParametersVS : public FNiagaraSpriteVertexFactoryShaderParameters
{
public:
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		NumCutoutVerticesPerFrame.Bind(ParameterMap, TEXT("NumCutoutVerticesPerFrame"));
		CutoutGeometry.Bind(ParameterMap, TEXT("CutoutGeometry"));

		NiagaraParticleDataFloat.Bind(ParameterMap, TEXT("NiagaraParticleDataFloat"));
		FloatDataOffset.Bind(ParameterMap, TEXT("NiagaraFloatDataOffset"));
		FloatDataStride.Bind(ParameterMap, TEXT("NiagaraFloatDataStride"));

//  		NiagaraParticleDataInt.Bind(ParameterMap, TEXT("NiagaraParticleDataInt"));
//  		Int32DataOffset.Bind(ParameterMap, TEXT("NiagaraInt32DataOffset"));
//  		Int32DataStride.Bind(ParameterMap, TEXT("NiagaraInt3DataStride"));

		ParticleAlignmentMode.Bind(ParameterMap, TEXT("ParticleAlignmentMode"));
		ParticleFacingMode.Bind(ParameterMap, TEXT("ParticleFacingMode"));

		SortedIndices.Bind(ParameterMap, TEXT("SortedIndices"));
		SortedIndicesOffset.Bind(ParameterMap, TEXT("SortedIndicesOffset"));
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << NumCutoutVerticesPerFrame;
		Ar << CutoutGeometry;
		Ar << ParticleFacingMode;
		Ar << ParticleAlignmentMode;

		Ar << NiagaraParticleDataFloat;
		Ar << FloatDataOffset;
		Ar << FloatDataStride;

//  		Ar << NiagaraParticleDataInt;
//  		Ar << Int32DataOffset;
//  		Ar << Int32DataStride;

		Ar << SortedIndices;
		Ar << SortedIndicesOffset;
	}

	virtual void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType VertexStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const override
	{
		FNiagaraSpriteVertexFactory* SpriteVF = (FNiagaraSpriteVertexFactory*)VertexFactory;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNiagaraSpriteUniformParameters>(), SpriteVF->GetSpriteUniformBuffer() );

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNiagaraSpriteVFLooseParameters>(), SpriteVF->LooseParameterUniformBuffer);
		
		ShaderBindings.Add(NumCutoutVerticesPerFrame, SpriteVF->GetNumCutoutVerticesPerFrame());
		FRHIShaderResourceView* NullSRV = GFNiagaraNullCutoutVertexBuffer.VertexBufferSRV;
		ShaderBindings.Add(CutoutGeometry, SpriteVF->GetCutoutGeometrySRV() ? SpriteVF->GetCutoutGeometrySRV() : NullSRV);

		ShaderBindings.Add(ParticleAlignmentMode, SpriteVF->GetAlignmentMode());
		ShaderBindings.Add(ParticleFacingMode, SpriteVF->GetFacingMode());

		ShaderBindings.Add(NiagaraParticleDataFloat, SpriteVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(FloatDataOffset, SpriteVF->GetFloatDataOffset());
		ShaderBindings.Add(FloatDataStride, SpriteVF->GetFloatDataStride());

		ShaderBindings.Add(SortedIndices, SpriteVF->GetSortedIndicesSRV() ? SpriteVF->GetSortedIndicesSRV() : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV);
		ShaderBindings.Add(SortedIndicesOffset, SpriteVF->GetSortedIndicesOffset());
	}
private:
	FShaderParameter NumCutoutVerticesPerFrame;

	FShaderParameter ParticleAlignmentMode;
	FShaderParameter ParticleFacingMode;

	FShaderResourceParameter CutoutGeometry;

	FShaderResourceParameter NiagaraParticleDataFloat;
	FShaderParameter FloatDataOffset;
	FShaderParameter FloatDataStride;

//  	FShaderResourceParameter NiagaraParticleDataInt;
//  	FShaderParameter Int32DataOffset;
//  	FShaderParameter Int32DataStride;
	
	FShaderResourceParameter SortedIndices;
	FShaderParameter SortedIndicesOffset;
};

class FNiagaraSpriteVertexFactoryShaderParametersPS : public FNiagaraSpriteVertexFactoryShaderParameters
{
public:

	virtual void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const override
	{
		FNiagaraSpriteVertexFactory* SpriteVF = (FNiagaraSpriteVertexFactory*)VertexFactory;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNiagaraSpriteUniformParameters>(), SpriteVF->GetSpriteUniformBuffer() );
	}
};

/**
 * The particle system vertex declaration resource type.
 */
class FNiagaraSpriteVertexDeclaration : public FRenderResource
{
public:

	FVertexDeclarationRHIRef VertexDeclarationRHI;

	// Constructor.
	FNiagaraSpriteVertexDeclaration() {}

	// Destructor.
	virtual ~FNiagaraSpriteVertexDeclaration() {}

	virtual void FillDeclElements(FVertexDeclarationElementList& Elements, int32& Offset)
	{
		uint32 InitialStride = sizeof(float) * 2;
		/** The stream to read the texture coordinates from. */
		check( Offset == 0 );
		Elements.Add(FVertexElement(0, Offset, VET_Float2, 0, InitialStride, false));
	}

	virtual void InitDynamicRHI()
	{
		FVertexDeclarationElementList Elements;
		int32	Offset = 0;

		FillDeclElements(Elements, Offset);

		// Create the vertex declaration for rendering the factory normally.
		// This is done in InitDynamicRHI instead of InitRHI to allow FParticleSpriteVertexFactory::InitRHI
		// to rely on it being initialized, since InitDynamicRHI is called before InitRHI.
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseDynamicRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

/** The simple element vertex declaration. */
static TGlobalResource<FNiagaraSpriteVertexDeclaration> GParticleSpriteVertexDeclaration;

bool FNiagaraSpriteVertexFactory::ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
	return (FNiagaraUtilities::SupportsNiagaraRendering(Platform)) && (Material->IsUsedWithNiagaraSprites() || Material->IsSpecialEngineMaterial());
}

/**
 * Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
 */
void FNiagaraSpriteVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
{
	FNiagaraVertexFactoryBase::ModifyCompilationEnvironment(Type, Platform, Material, OutEnvironment);

	// Set a define so we can tell in MaterialTemplate.usf when we are compiling a sprite vertex factory
	OutEnvironment.SetDefine(TEXT("PARTICLE_SPRITE_FACTORY"),TEXT("1"));
}

/**
 *	Initialize the Render Hardware Interface for this vertex factory
 */
void FNiagaraSpriteVertexFactory::InitRHI()
{
	InitStreams();
	SetDeclaration(GParticleSpriteVertexDeclaration.VertexDeclarationRHI);
}

void FNiagaraSpriteVertexFactory::InitStreams()
{
	const bool bInstanced = GRHISupportsInstancing;

	check(Streams.Num() == 0);
	if(bInstanced) 
	{
		FVertexStream* TexCoordStream = new(Streams) FVertexStream;
		TexCoordStream->VertexBuffer = VertexBufferOverride ? VertexBufferOverride : &GParticleTexCoordVertexBuffer;
		TexCoordStream->Stride = sizeof(FVector2D);
		TexCoordStream->Offset = 0;
	}
}

void FNiagaraSpriteVertexFactory::SetTexCoordBuffer(const FVertexBuffer* InTexCoordBuffer)
{
	FVertexStream& TexCoordStream = Streams[0];
	TexCoordStream.VertexBuffer = InTexCoordBuffer;
}

FVertexFactoryShaderParameters* FNiagaraSpriteVertexFactory::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	if (ShaderFrequency == SF_Vertex)
	{
		return new FNiagaraSpriteVertexFactoryShaderParametersVS();
	}
	else if (ShaderFrequency == SF_Pixel)
	{
		return new FNiagaraSpriteVertexFactoryShaderParametersPS();
	}
#if RHI_RAYTRACING
	else if (ShaderFrequency == SF_Compute)
	{
		return new FNiagaraSpriteVertexFactoryShaderParametersVS();
	}
	else if (ShaderFrequency == SF_RayHitGroup)
	{
		return new FNiagaraSpriteVertexFactoryShaderParametersVS();
	}
#endif
	return NULL;
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FNiagaraSpriteVertexFactory,"/Plugin/FX/Niagara/Private/NiagaraSpriteVertexFactory.ush",true,false,true,false,false);
