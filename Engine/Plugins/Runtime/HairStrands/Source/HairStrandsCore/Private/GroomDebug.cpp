// Copyright Epic Games, Inc. All Rights Reserved. 

#include "GeometryCacheComponent.h"
#include "GroomInstance.h"
#include "GroomManager.h"
#include "GPUSkinCache.h"
#include "HairStrandsMeshProjection.h"
#include "HairStrandsInterface.h"
#include "CommonRenderResources.h"
#include "GroomGeometryCache.h"
#include "Components/SkeletalMeshComponent.h"
#include "UnrealEngine.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairDebugMeshProjection_SkinCacheMesh = 0;
static int32 GHairDebugMeshProjection_SkinCacheMeshInUVsSpace = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedFrames = 0;

static int32 GHairDebugMeshProjection_Render_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedFrames = 0;


static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMeshInUVsSpace(TEXT("r.HairStrands.MeshProjection.DebugInUVsSpace"), GHairDebugMeshProjection_SkinCacheMeshInUVsSpace, TEXT("Render debug mes projection in UVs space"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMesh(TEXT("r.HairStrands.MeshProjection.DebugSkinCache"), GHairDebugMeshProjection_SkinCacheMesh, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestTriangles(TEXT("r.HairStrands.MeshProjection.Render.Rest.Triangles"), GHairDebugMeshProjection_Render_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestFrames(TEXT("r.HairStrands.MeshProjection.Render.Rest.Frames"), GHairDebugMeshProjection_Render_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedTriangles(TEXT("r.HairStrands.MeshProjection.Render.Deformed.Triangles"), GHairDebugMeshProjection_Render_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedFrames(TEXT("r.HairStrands.MeshProjection.Render.Deformed.Frames"), GHairDebugMeshProjection_Render_HairDeformedFrames, TEXT("Render debug mes projection"));

static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestTriangles(TEXT("r.HairStrands.MeshProjection.Sim.Rest.Triangles"), GHairDebugMeshProjection_Sim_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestFrames(TEXT("r.HairStrands.MeshProjection.Sim.Rest.Frames"), GHairDebugMeshProjection_Sim_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedTriangles(TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Triangles"), GHairDebugMeshProjection_Sim_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedFrames(TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Frames"), GHairDebugMeshProjection_Sim_HairDeformedFrames, TEXT("Render debug mes projection"));

static int32 GHairCardsAtlasDebug = 0;
static FAutoConsoleVariableRef CVarHairCardsAtlasDebug(TEXT("r.HairStrands.Cards.DebugAtlas"), GHairCardsAtlasDebug, TEXT("Draw debug hair cards atlas."));

static int32 GHairCardsVoxelDebug = 0;
static FAutoConsoleVariableRef CVarHairCardsVoxelDebug(TEXT("r.HairStrands.Cards.DebugVoxel"), GHairCardsVoxelDebug, TEXT("Draw debug hair cards voxel datas."));

static int32 GHairCardsGuidesDebug_Ren = 0;
static int32 GHairCardsGuidesDebug_Sim = 0;
static FAutoConsoleVariableRef CVarHairCardsGuidesDebug_Ren(TEXT("r.HairStrands.Cards.DebugGuides.Render"), GHairCardsGuidesDebug_Ren, TEXT("Draw debug hair cards guides (1: Rest, 2: Deformed)."));
static FAutoConsoleVariableRef CVarHairCardsGuidesDebug_Sim(TEXT("r.HairStrands.Cards.DebugGuides.Sim"), GHairCardsGuidesDebug_Sim, TEXT("Draw debug hair sim guides (1: Rest, 2: Deformed)."));

///////////////////////////////////////////////////////////////////////////////////////////////////

namespace GroomDebug
{
	// Internal helper class for FCanvas to be able to get screen size
	class FRenderTargetTemp : public FRenderTarget
	{
		const FTexture2DRHIRef Texture;
		const FIntPoint SizeXY;

	public:
		FRenderTargetTemp(const FIntRect& ViewRect, const FTexture2DRHIRef InTexture) 
		: Texture(InTexture)
		, SizeXY(ViewRect.Size())
		{}

		virtual FIntPoint GetSizeXY() const override { return SizeXY; }

		virtual const FTexture2DRHIRef& GetRenderTargetTexture() const override
		{
			return Texture;
		}
	};
}

bool IsHairStrandsSkinCacheEnable();

static void GetGroomInterpolationData(
	FRDGBuilder& GraphBuilder,
	const TArray<FHairGroupInstance*> Instances,
	const EWorldType::Type WorldType,
	const EHairStrandsProjectionMeshType MeshType,
	const FGPUSkinCache* SkinCache,
	FHairStrandsProjectionMeshData::LOD& OutGeometries)
{
	for (const FHairGroupInstance* Instance : Instances)
	{
		if (!Instance || Instance->WorldType != WorldType || !Instance->Debug.MeshComponent)
			continue;

		FCachedGeometry CachedGeometry;
		if (Instance->Debug.GroomBindingType == EGroomBindingMeshType::SkeletalMesh)
		{
			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Instance->Debug.MeshComponent))
			{
				if (SkinCache)
				{
					CachedGeometry = SkinCache->GetCachedGeometry(SkeletalMeshComponent->ComponentId.PrimIDValue);
				}

				if (IsHairStrandsSkinCacheEnable() && CachedGeometry.Sections.Num() == 0)
				{
					FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					BuildCacheGeometry(GraphBuilder, ShaderMap, SkeletalMeshComponent, CachedGeometry);
				}
			}
		}
		else if (UGeometryCacheComponent* GeometryCacheComponent = Cast<UGeometryCacheComponent>(Instance->Debug.MeshComponent))
		{
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			BuildCacheGeometry(GraphBuilder, ShaderMap, GeometryCacheComponent, CachedGeometry);
		}
		if (CachedGeometry.Sections.Num() == 0)
			continue;

		if (MeshType == EHairStrandsProjectionMeshType::DeformedMesh || MeshType == EHairStrandsProjectionMeshType::RestMesh)
		{
			for (const FCachedGeometry::Section& Section : CachedGeometry.Sections)
			{
				FHairStrandsProjectionMeshData::Section OutSection = ConvertMeshSection(Section);
				if (MeshType == EHairStrandsProjectionMeshType::RestMesh)
				{					
					// If the mesh has some mesh-tranferred data, we display that otherwise we use the rest data
					const bool bHasTransferData = Section.LODIndex < Instance->Debug.TransferredPositions.Num();
					if (bHasTransferData)
					{
						OutSection.PositionBuffer = Instance->Debug.TransferredPositions[Section.LODIndex].SRV;
					}
					else if (Instance->Debug.TargetMeshData.LODs.Num() > 0)
					{
						OutGeometries = Instance->Debug.TargetMeshData.LODs[0];
					}
				}
				OutGeometries.Sections.Add(OutSection);
			}
		}

		if (MeshType == EHairStrandsProjectionMeshType::TargetMesh && Instance->Debug.TargetMeshData.LODs.Num() > 0)
		{
			OutGeometries = Instance->Debug.TargetMeshData.LODs[0];
		}

		if (MeshType == EHairStrandsProjectionMeshType::SourceMesh && Instance->Debug.SourceMeshData.LODs.Num() > 0)
		{
			OutGeometries = Instance->Debug.SourceMeshData.LODs[0];
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionMeshDebugParameters, )
	SHADER_PARAMETER(FMatrix, LocalToWorld)
	SHADER_PARAMETER(uint32, VertexOffset)
	SHADER_PARAMETER(uint32, IndexOffset)
	SHADER_PARAMETER(uint32, MaxIndexCount)
	SHADER_PARAMETER(uint32, MaxVertexCount)
	SHADER_PARAMETER(uint32, MeshUVsChannelOffset)
	SHADER_PARAMETER(uint32, MeshUVsChannelCount)
	SHADER_PARAMETER(uint32, bOutputInUVsSpace)
	SHADER_PARAMETER(uint32, MeshType)
	SHADER_PARAMETER(uint32, SectionIndex)
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputIndexBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexPositionBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexUVsBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionMeshDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}


	FHairProjectionMeshDebug() = default;
	FHairProjectionMeshDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionMeshDebugVS : public FHairProjectionMeshDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugVS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}
};

class FHairProjectionMeshDebugPS : public FHairProjectionMeshDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugPS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainPS", SF_Pixel);

static void AddDebugProjectionMeshPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	const EHairStrandsProjectionMeshType MeshType,
	const bool bClearDepth,
	FHairStrandsProjectionMeshData::Section& MeshSectionData,
	FRDGTextureRef& ColorTexture,
	FRDGTextureRef& DepthTexture)
{
	const EPrimitiveType PrimitiveType = PT_TriangleList;
	const bool bHasIndexBuffer = MeshSectionData.IndexBuffer != nullptr;
	const uint32 PrimitiveCount = MeshSectionData.NumPrimitives;

	if (!MeshSectionData.PositionBuffer || PrimitiveCount == 0)
		return;

	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionMeshDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionMeshDebugParameters>();
	Parameters->LocalToWorld = MeshSectionData.LocalToWorld.ToMatrixWithScale();
	Parameters->OutputResolution = Resolution;
	Parameters->MeshType = uint32(MeshType);
	Parameters->bOutputInUVsSpace = GHairDebugMeshProjection_SkinCacheMeshInUVsSpace ? 1 : 0;
	Parameters->VertexOffset = MeshSectionData.VertexBaseIndex;
	Parameters->IndexOffset = MeshSectionData.IndexBaseIndex;
	Parameters->MaxIndexCount = MeshSectionData.TotalIndexCount;
	Parameters->MaxVertexCount = MeshSectionData.TotalVertexCount;
	Parameters->MeshUVsChannelOffset = MeshSectionData.UVsChannelOffset;
	Parameters->MeshUVsChannelCount = MeshSectionData.UVsChannelCount;
	Parameters->InputIndexBuffer = MeshSectionData.IndexBuffer;
	Parameters->InputVertexPositionBuffer = MeshSectionData.PositionBuffer;
	Parameters->InputVertexUVsBuffer = MeshSectionData.UVsBuffer;
	Parameters->SectionIndex = MeshSectionData.SectionIndex;
	Parameters->ViewUniformBuffer = ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTexture, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionMeshDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionMeshDebugVS::FInputType>(bHasIndexBuffer ? 1 : 0);

	TShaderMapRef<FHairProjectionMeshDebugVS> VertexShader(ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionMeshDebugPS> PixelShader(ShaderMap);

	FHairProjectionMeshDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionMeshDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMeshProjectionMeshDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
		{

			RHICmdList.SetViewport(
				Viewport.Min.X,
				Viewport.Min.Y,
				0.0f,
				Viewport.Max.X,
				Viewport.Max.Y,
				1.0f);

			// Apply additive blending pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

			// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
			RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
		});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionHairDebugParameters, )
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, DeformedFrameEnable)
	SHADER_PARAMETER(FMatrix, RootLocalToWorld)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition0Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition1Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition2Buffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition0Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition1Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition2Buffer)

	// Change for actual frame data (stored or computed only)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RootPositionBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RootNormalBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RootBarycentricBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionHairDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}

	FHairProjectionHairDebug() = default;
	FHairProjectionHairDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionHairDebugVS : public FHairProjectionHairDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugVS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

class FHairProjectionHairDebugPS : public FHairProjectionHairDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugPS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainPS", SF_Pixel);

enum class EDebugProjectionHairType
{
	HairFrame,
	HairTriangle,
};

static void AddDebugProjectionHairPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	const bool bClearDepth,
	const EDebugProjectionHairType GeometryType,
	const HairStrandsTriangleType PoseType,
	const int32 MeshLODIndex,
	const FHairStrandsRestRootResource* RestRootResources,
	const FHairStrandsDeformedRootResource* DeformedRootResources,
	const FTransform& LocalToWorld,
	FRDGTextureRef ColorTarget,
	FRDGTextureRef DepthTexture)
{
	const EPrimitiveType PrimitiveType = GeometryType == EDebugProjectionHairType::HairFrame ? PT_LineList : PT_TriangleList;
	const uint32 RootCount = RestRootResources->RootData.RootCount;
	const uint32 PrimitiveCount = RootCount;

	if (PrimitiveCount == 0 || MeshLODIndex < 0 || MeshLODIndex >= RestRootResources->LODs.Num() || MeshLODIndex >= DeformedRootResources->LODs.Num())
		return;

	if (EDebugProjectionHairType::HairFrame == GeometryType &&
		(!RestRootResources->RootPositionBuffer.Buffer ||
			!RestRootResources->RootNormalBuffer.Buffer ||
			!RestRootResources->LODs[MeshLODIndex].RootTriangleBarycentricBuffer.Buffer))
		return;

	const FHairStrandsRestRootResource::FLOD& RestLODDatas = RestRootResources->LODs[MeshLODIndex];
	const FHairStrandsDeformedRootResource::FLOD& DeformedLODDatas = DeformedRootResources->LODs[MeshLODIndex];

	if (!RestLODDatas.RestRootTrianglePosition0Buffer.Buffer ||
		!RestLODDatas.RestRootTrianglePosition1Buffer.Buffer ||
		!RestLODDatas.RestRootTrianglePosition2Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition0Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition1Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition2Buffer.Buffer)
		return;

	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionHairDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionHairDebugParameters>();
	Parameters->OutputResolution = Resolution;
	Parameters->MaxRootCount = RootCount;
	Parameters->RootLocalToWorld = LocalToWorld.ToMatrixWithScale();
	Parameters->DeformedFrameEnable = PoseType == HairStrandsTriangleType::DeformedPose;

	if (EDebugProjectionHairType::HairFrame == GeometryType)
	{
		Parameters->RootPositionBuffer		= RegisterAsSRV(GraphBuilder, RestRootResources->RootPositionBuffer);
		Parameters->RootNormalBuffer		= RegisterAsSRV(GraphBuilder, RestRootResources->RootNormalBuffer);
		Parameters->RootBarycentricBuffer	= RegisterAsSRV(GraphBuilder, RestLODDatas.RootTriangleBarycentricBuffer);
	}

	Parameters->RestPosition0Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition0Buffer);
	Parameters->RestPosition1Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition1Buffer);
	Parameters->RestPosition2Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition2Buffer);

	Parameters->DeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
	Parameters->DeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
	Parameters->DeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition2Buffer);

	Parameters->ViewUniformBuffer = ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTarget, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionHairDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionHairDebugVS::FInputType>(PrimitiveType == PT_LineList ? 0 : 1);

	TShaderMapRef<FHairProjectionHairDebugVS> VertexShader(ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionHairDebugPS> PixelShader(ShaderMap);

	FHairProjectionHairDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionHairDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMeshProjectionHairDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
		{

			RHICmdList.SetViewport(
				Viewport.Min.X,
				Viewport.Min.Y,
				0.0f,
				Viewport.Max.X,
				Viewport.Max.Y,
				1.0f);

			// Apply additive blending pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

			// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
			RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
		});
}	

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVoxelPlainRaymarchingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelPlainRaymarchingCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelPlainRaymarchingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		//SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(FVector2D, OutputResolution)		
		SHADER_PARAMETER(FIntVector, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_VoxelSize)
		SHADER_PARAMETER(FVector, Voxel_MinBound)
		SHADER_PARAMETER(FVector, Voxel_MaxBound)
		SHADER_PARAMETER_SRV(Buffer, Voxel_TangentBuffer)
		SHADER_PARAMETER_SRV(Buffer, Voxel_NormalBuffer)
		SHADER_PARAMETER_SRV(Buffer, Voxel_DensityBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Voxel_ProcessedDensityBuffer)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PLAIN"), 1);
	}	
};

IMPLEMENT_GLOBAL_SHADER(FVoxelPlainRaymarchingCS, "/Engine/Private/HairStrands/HairCardsVoxel.usf", "MainCS", SF_Compute);

static void AddVoxelPlainRaymarchingPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FHairGroupInstance* Instance,
	const FShaderDrawDebugData* ShaderDrawData,
	TRefCountPtr<IPooledRenderTarget>& InOutputTexture)
{
#if 0 // #hair_todo: renable if needed
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	const FHairStrandClusterData::FHairGroup& HairGroupClusters = HairClusterData.HairGroups[DataIndex];

	FViewInfo& View = Views[ViewIndex];
	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards)
			return;

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(InOutputTexture);
		const FIntPoint OutputResolution(OutputTexture->Desc.Extent);
		const FHairCardsVoxel& CardsVoxel = Instance->HairGroupPublicData->VFInput.Cards.Voxel;

		FRDGBufferRef VoxelDensityBuffer2 = nullptr;
		AddVoxelProcessPass(GraphBuilder, View, CardsVoxel, VoxelDensityBuffer2);

		FVoxelPlainRaymarchingCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelPlainRaymarchingCS::FParameters>();
		Parameters->ViewUniformBuffer	= View.ViewUniformBuffer;
		Parameters->OutputResolution	= OutputResolution;
		Parameters->Voxel_Resolution	= CardsVoxel.Resolution;
		Parameters->Voxel_VoxelSize		= CardsVoxel.VoxelSize;
		Parameters->Voxel_MinBound		= CardsVoxel.MinBound;
		Parameters->Voxel_MaxBound		= CardsVoxel.MaxBound;
		Parameters->Voxel_TangentBuffer	= CardsVoxel.TangentBuffer.SRV;
		Parameters->Voxel_NormalBuffer	= CardsVoxel.NormalBuffer.SRV;
		Parameters->Voxel_DensityBuffer = CardsVoxel.DensityBuffer.SRV;
		Parameters->Voxel_ProcessedDensityBuffer = GraphBuilder.CreateSRV(VoxelDensityBuffer2, PF_R32_UINT);

		ShaderDrawDebug::SetParameters(GraphBuilder, ShaderDrawData, Parameters->ShaderDrawParameters);
		//ShaderPrint::SetParameters(View, Parameters->ShaderPrintParameters);
		Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FVoxelPlainRaymarchingCS> ComputeShader(View.ShaderMap);
		const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y, 1), FIntVector(8, 8, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVoxelPlainRaymarching"), ComputeShader, Parameters, DispatchCount);
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugCardAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCardAtlasCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCardAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_TEXTURE(Texture2D, AtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, AtlasResolution)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ATLAS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardAtlasCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsAtlasPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FHairGroupInstance* Instance,
	const FShaderDrawDebugData* ShaderDrawData,
	TRefCountPtr<IPooledRenderTarget>& InOutputTexture)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards || ShaderDrawData == nullptr)
	{
		return;
	}

	const int32 LODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(LODIndex))
	{
		return;
	}

	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(InOutputTexture, TEXT("SceneColorTexture"));
	FTextureReferenceRHIRef AtlasTexture = nullptr;

	const int32 DebugMode = FMath::Clamp(GHairCardsAtlasDebug, 1, 6);
	switch (DebugMode)
	{
	case 1: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->DepthTexture; break;
	case 2: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->CoverageTexture; break;
	case 3: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->TangentTexture; break;
	case 4:
	case 5:
	case 6: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->AttributeTexture; break;
	}

	if (AtlasTexture != nullptr)
	{
		TShaderMapRef<FDrawDebugCardAtlasCS> ComputeShader(ShaderMap);

		FDrawDebugCardAtlasCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardAtlasCS::FParameters>();
		Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
		Parameters->OutputResolution = SceneColorTexture->Desc.Extent;
		Parameters->AtlasResolution = FIntPoint(AtlasTexture->GetSizeXYZ().X, AtlasTexture->GetSizeXYZ().Y);
		Parameters->AtlasTexture = AtlasTexture;
		Parameters->DebugMode = DebugMode;
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->OutputTexture = GraphBuilder.CreateUAV(SceneColorTexture);

		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DrawDebugCardsAtlas"), ComputeShader, Parameters,
		FIntVector::DivideAndRoundUp(FIntVector(Parameters->OutputResolution.X, Parameters->OutputResolution.Y, 1), FIntVector(8, 8, 1)));

	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugCardGuidesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCardGuidesCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCardGuidesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32,  DebugMode)
		SHADER_PARAMETER(FMatrix, LocalToWorld)
		
		SHADER_PARAMETER(uint32,  RenVertexCount)
		SHADER_PARAMETER(FVector, RenRestOffset)
		SHADER_PARAMETER(FVector, RenDeformedOffset)
		
		SHADER_PARAMETER(uint32,  SimVertexCount)
		SHADER_PARAMETER(FVector, SimRestOffset)
		SHADER_PARAMETER(FVector, SimDeformedOffset)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenRestPosition)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenDeformedPosition)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimRestPosition)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimDeformedPosition)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_GUIDE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardGuidesCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsGuidesPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FHairGroupInstance* Instance,
	const FShaderDrawDebugData* ShaderDrawData,
	const bool bDeformed, 
	const bool bRen)
{
	// Force shader debug to be enabled
	if (!ShaderDrawDebug::IsShaderDrawDebugEnabled())
	{
		ShaderDrawDebug::SetEnabled(true);
	}

	const uint32 MaxCount = 128000;
	if (ShaderDrawDebug::GetMaxElementCount() < MaxCount)
	{
		ShaderDrawDebug::SetMaxElementCount(MaxCount);
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards || ShaderDrawData == nullptr)
	{
		return;
	}

	const int32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(HairLODIndex))
	{
		return;
	}

	const FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
	
	if (!LOD.Guides.Data)
	{
		return;
	}
	TShaderMapRef<FDrawDebugCardGuidesCS> ComputeShader(ShaderMap);

	FDrawDebugCardGuidesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardGuidesCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->RenVertexCount = LOD.Guides.RestResource->GetVertexCount();
	Parameters->SimVertexCount = Instance->Guides.RestResource->GetVertexCount();

	Parameters->RenRestOffset = LOD.Guides.RestResource->PositionOffset;
	Parameters->RenRestPosition = RegisterAsSRV(GraphBuilder, LOD.Guides.RestResource->RestPositionBuffer);

	Parameters->RenDeformedOffset = LOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->RenDeformedPosition = RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current));

	Parameters->SimRestOffset = Instance->Guides.RestResource->PositionOffset;
	Parameters->SimRestPosition = RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer);

	Parameters->SimDeformedOffset = Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->SimDeformedPosition = RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current));
	Parameters->LocalToWorld = Instance->LocalToWorld.ToMatrixWithScale();

	if (!bDeformed &&  bRen) Parameters->DebugMode = 1;
	if ( bDeformed &&  bRen) Parameters->DebugMode = 2;
	if (!bDeformed && !bRen) Parameters->DebugMode = 3;
	if ( bDeformed && !bRen) Parameters->DebugMode = 4;

	ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);

	const uint32 VertexCount = Parameters->DebugMode <= 2 ? Parameters->RenVertexCount : Parameters->SimVertexCount;
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DrawDebugCardsAtlas"), ComputeShader, Parameters,
	FIntVector::DivideAndRoundUp(FIntVector(VertexCount, 1, 1), FIntVector(32, 1, 1)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairDebugCanvasParameter, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static const TCHAR* ToString(EHairGeometryType In)
{
	switch (In)
	{
	case EHairGeometryType::NoneGeometry:	return TEXT("None");
	case EHairGeometryType::Strands:		return TEXT("Strands");
	case EHairGeometryType::Cards:			return TEXT("Cards");
	case EHairGeometryType::Meshes:			return TEXT("Meshes");
	}
	return TEXT("None");
}

void RunHairStrandsDebug(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	EWorldType::Type WorldType,
	const FSceneView& View,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	const TArray<FHairGroupInstance*>& Instances,
	TRefCountPtr<IPooledRenderTarget>& SceneColorRT,
	FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
{
	const EHairDebugMode HairDebugMode = GetHairStrandsDebugMode();

	if (HairDebugMode == EHairDebugMode::MacroGroups)
	{
		FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneColorRT);
		FHairDebugCanvasParameter* PassParameters = GraphBuilder.AllocParameters<FHairDebugCanvasParameter>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad, 0);

		const FSceneView* LocalView = &View;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsMeshProjectionMeshDebug"),
			PassParameters,
			ERDGPassFlags::Raster,
			[LocalView, Viewport, WorldType, Instances, SceneColorRT](FRHICommandListImmediate& RHICmdList)
		{
			const float YStep = 14;
			float ClusterY = 68;

			// Component part of the clusters
			GroomDebug::FRenderTargetTemp TempRenderTarget(Viewport, (const FTexture2DRHIRef&)SceneColorRT->GetRenderTargetItem().TargetableTexture);
			FCanvas Canvas(&TempRenderTarget, nullptr, LocalView->Family->CurrentRealTime, LocalView->Family->CurrentWorldTime, LocalView->Family->DeltaWorldTime, LocalView->FeatureLevel);
			Canvas.SetRenderTargetRect(Viewport);

			float X = 20;
			float Y = ClusterY;
			const FLinearColor InactiveColor(0.5, 0.5, 0.5);
			const FLinearColor DebugColor(1, 1, 0);
			const FLinearColor DebugGroupColor(0.5f, 0, 0);
			FString Line;
			// Active groom
			// Name | Group x / x | LOD x / x | GeometryType | BindingType | Sim | RBF | VertexCount

			Line = FString::Printf(TEXT("----------------------------------------------------------------"));
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

			Line = FString::Printf(TEXT("Registered hair groups count : %d"), Instances.Num());
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

			for (FHairGroupInstance* Instance : Instances)
			{
				const bool bIsActive = Instance->WorldType == WorldType;
				const bool bHasSkinInterpolation = Instance->Strands.RestRootResource != nullptr;
				const bool bHasBindingAsset = bHasSkinInterpolation && !Instance->Strands.bOwnRootResourceAllocation;

				if (!bIsActive)
					continue;

				Line = FString::Printf(TEXT(" * Group:%d/%d | LOD:%1.1f/%d | GeometryType:%s | BindingType:%s | Sim:%d | RBF:%d | VertexCount:%d | Name: %s"),
					Instance->Debug.GroupIndex,
					Instance->Debug.GroupCount,

					Instance->HairGroupPublicData->LODIndex,
					Instance->HairGroupPublicData->GetLODScreenSizes().Num(),

					ToString(Instance->GeometryType),
					bHasBindingAsset ? TEXT("Skinned") : TEXT("Rigid"),
					0, // TODO: Instance->bHasSimulation,
					0, // TODO: Instance->bHasRBF,
					Instance->HairGroupPublicData->VertexCount,
					*Instance->Debug.GroomAssetName);
				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugGroupColor : InactiveColor);
			}

			const bool bFlush = false;
			const bool bInsideRenderPass = true;
			Canvas.Flush_RenderThread(RHICmdList, bFlush, bInsideRenderPass);

			ClusterY = Y;
		});
	}

	if (HairDebugMode == EHairDebugMode::MeshProjection)
	{
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneColorRT, TEXT("SceneColorTexture"));
		{
			bool bClearDepth = true;
			FRDGTextureRef DepthTexture;
			{
				const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(SceneColorTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairInterpolationDepthTexture"));
			}

			if (GHairDebugMeshProjection_SkinCacheMesh > 0)
			{
				auto RenderMeshProjection = [&bClearDepth, WorldType, ShaderMap, Viewport, &ViewUniformBuffer, Instances, SkinCache, &SceneColorTexture, &DepthTexture, &GraphBuilder](FRDGBuilder& LocalGraphBuilder, EHairStrandsProjectionMeshType MeshType)
				{
					FHairStrandsProjectionMeshData::LOD MeshProjectionLODData;
					GetGroomInterpolationData(GraphBuilder, Instances, WorldType, MeshType, SkinCache, MeshProjectionLODData);
					for (FHairStrandsProjectionMeshData::Section& Section : MeshProjectionLODData.Sections)
					{
						AddDebugProjectionMeshPass(LocalGraphBuilder, ShaderMap, Viewport, ViewUniformBuffer, MeshType, bClearDepth, Section, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
				};

				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::DeformedMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::RestMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::SourceMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::TargetMesh);
			}

			auto RenderProjectionData = [&GraphBuilder, ShaderMap, Viewport, &ViewUniformBuffer, Instances, WorldType, SkinCache, &bClearDepth, SceneColorTexture, DepthTexture](EHairStrandsInterpolationType StrandType, bool bRestTriangle, bool bRestFrame, bool bDeformedTriangle, bool bDeformedFrame)
			{
				TArray<int32> HairLODIndices;
				for (FHairGroupInstance* Instance : Instances)
				{
					if (Instance->WorldType != WorldType || !Instance->HairGroupPublicData || Instance->Guides.RestRootResource == nullptr || Instance->Guides.DeformedRootResource == nullptr)
						continue;

					const int32 MeshLODIndex = Instance->Debug.MeshLODIndex;
					const bool bRenderStrands = StrandType == EHairStrandsInterpolationType::RenderStrands;
					FHairStrandsRestRootResource* RestRootResource = bRenderStrands ? Instance->Strands.RestRootResource : Instance->Guides.RestRootResource;
					FHairStrandsDeformedRootResource* DeformedRootResource = bRenderStrands ? Instance->Strands.DeformedRootResource : Instance->Guides.DeformedRootResource;

					if (bRestTriangle)
					{
						AddDebugProjectionHairPass(
							GraphBuilder, 
							ShaderMap, 
							Viewport, 
							ViewUniformBuffer, 
							bClearDepth, 
							EDebugProjectionHairType::HairTriangle, 
							HairStrandsTriangleType::RestPose, 
							MeshLODIndex,
							RestRootResource, 
							DeformedRootResource, 
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform, 
							SceneColorTexture, 
							DepthTexture);
						bClearDepth = false;
					}
					if (bRestFrame)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairFrame,
							HairStrandsTriangleType::RestPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedTriangle)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairTriangle,
							HairStrandsTriangleType::DeformedPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedFrame)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairFrame,
							HairStrandsTriangleType::DeformedPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
				}
			};

			if (GHairDebugMeshProjection_Render_HairRestTriangles > 0 ||
				GHairDebugMeshProjection_Render_HairRestFrames > 0 ||
				GHairDebugMeshProjection_Render_HairDeformedTriangles > 0 ||
				GHairDebugMeshProjection_Render_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					EHairStrandsInterpolationType::RenderStrands,
					GHairDebugMeshProjection_Render_HairRestTriangles > 0,
					GHairDebugMeshProjection_Render_HairRestFrames > 0,
					GHairDebugMeshProjection_Render_HairDeformedTriangles > 0,
					GHairDebugMeshProjection_Render_HairDeformedFrames > 0);
			}

			if (GHairDebugMeshProjection_Sim_HairRestTriangles > 0 ||
				GHairDebugMeshProjection_Sim_HairRestFrames > 0 ||
				GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0 ||
				GHairDebugMeshProjection_Sim_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					EHairStrandsInterpolationType::SimulationStrands,
					GHairDebugMeshProjection_Sim_HairRestTriangles > 0,
					GHairDebugMeshProjection_Sim_HairRestFrames > 0,
					GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0,
					GHairDebugMeshProjection_Sim_HairDeformedFrames > 0);
			}
		}
	}

	if (GHairCardsVoxelDebug > 0)
	{
		for (FHairGroupInstance* Instance : Instances)
		{
			AddVoxelPlainRaymarchingPass(GraphBuilder, View, Instance, ShaderDrawData, SceneColorRT);
		}
	}

	if (GHairCardsAtlasDebug > 0)
	{
		for (FHairGroupInstance* Instance : Instances)
		{
			AddDrawDebugCardsAtlasPass(GraphBuilder, View, Instance, ShaderDrawData, SceneColorRT);
		}
	}

	for (FHairGroupInstance* Instance : Instances)
	{
		if (GHairCardsGuidesDebug_Ren > 0 || Instance->Debug.bDrawCardsGuides)
			AddDrawDebugCardsGuidesPass(GraphBuilder, View, Instance, ShaderDrawData, Instance->Debug.bDrawCardsGuides ? false : GHairCardsGuidesDebug_Ren == 1, true);
		if (GHairCardsGuidesDebug_Sim > 0)
			AddDrawDebugCardsGuidesPass(GraphBuilder, View, Instance, ShaderDrawData, GHairCardsGuidesDebug_Sim == 1, false);
	}
}
