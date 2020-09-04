// Copyright Epic Games, Inc. All Rights Reserved. 

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

const TCHAR* ToString(EWorldType::Type Type)
{
	switch (Type)
	{
		case EWorldType::None			: return TEXT("None");
		case EWorldType::Game			: return TEXT("Game");
		case EWorldType::Editor			: return TEXT("Editor");
		case EWorldType::PIE			: return TEXT("PIE");
		case EWorldType::EditorPreview	: return TEXT("EditorPreview");
		case EWorldType::GamePreview	: return TEXT("GamePreview");
		case EWorldType::GameRPC		: return TEXT("GameRPC");
		case EWorldType::Inactive		: return TEXT("Inactive");
		default							: return TEXT("Unknown");
	}
}

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

static void GetGroomInterpolationData(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FHairGroupInstance*> Instances,
	const EWorldType::Type WorldType,
	const EHairStrandsProjectionMeshType MeshType,
	const FGPUSkinCache* SkinCache,
	FHairStrandsProjectionMeshData::LOD& OutGeometries)
{
	for (const FHairGroupInstance* Instance : Instances)
	{
		if (Instance->WorldType != WorldType)
			continue;

		FCachedGeometry CachedGeometry;
		if (SkinCache)
		{
			CachedGeometry = SkinCache->GetCachedGeometry(Instance->Debug.SkeletalComponent->ComponentId.PrimIDValue);
		}
		else if (Instance->Debug.SkeletalComponent)
		{
			const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
			BuildCacheGeometry(RHICmdList, ShaderMap, Instance->Debug.SkeletalComponent, CachedGeometry);
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
		return IsHairStrandsSupported(Parameters.Platform);
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
};

class FHairProjectionMeshDebugPS : public FHairProjectionMeshDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugPS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainPS", SF_Pixel);

static void AddDebugProjectionMeshPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntRect Viewport,
	TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
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

	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition0Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition1Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition2Buffer)

	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition0Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition1Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition2Buffer)

	// Change for actual frame data (stored or computed only)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootPositionBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootNormalBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootBarycentricBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionHairDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
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
	TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
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
		(!RestRootResources->RootPositionBuffer.SRV ||
			!RestRootResources->RootNormalBuffer.SRV ||
			!RestRootResources->LODs[MeshLODIndex].RootTriangleBarycentricBuffer.SRV))
		return;

	const FHairStrandsRestRootResource::FLOD& RestLODDatas = RestRootResources->LODs[MeshLODIndex];
	const FHairStrandsDeformedRootResource::FLOD& DeformedLODDatas = DeformedRootResources->LODs[MeshLODIndex];

	if (!RestLODDatas.RestRootTrianglePosition0Buffer.SRV ||
		!RestLODDatas.RestRootTrianglePosition1Buffer.SRV ||
		!RestLODDatas.RestRootTrianglePosition2Buffer.SRV ||
		!DeformedLODDatas.DeformedRootTrianglePosition0Buffer.SRV ||
		!DeformedLODDatas.DeformedRootTrianglePosition1Buffer.SRV ||
		!DeformedLODDatas.DeformedRootTrianglePosition2Buffer.SRV)
		return;

	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionHairDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionHairDebugParameters>();
	Parameters->OutputResolution = Resolution;
	Parameters->MaxRootCount = RootCount;
	Parameters->RootLocalToWorld = LocalToWorld.ToMatrixWithScale();
	Parameters->DeformedFrameEnable = PoseType == HairStrandsTriangleType::DeformedPose;

	if (EDebugProjectionHairType::HairFrame == GeometryType)
	{
		Parameters->RootPositionBuffer = RestRootResources->RootPositionBuffer.SRV;
		Parameters->RootNormalBuffer = RestRootResources->RootNormalBuffer.SRV;
		Parameters->RootBarycentricBuffer = RestLODDatas.RootTriangleBarycentricBuffer.SRV;
	}

	Parameters->RestPosition0Buffer = RestLODDatas.RestRootTrianglePosition0Buffer.SRV;
	Parameters->RestPosition1Buffer = RestLODDatas.RestRootTrianglePosition1Buffer.SRV;
	Parameters->RestPosition2Buffer = RestLODDatas.RestRootTrianglePosition2Buffer.SRV;

	Parameters->DeformedPosition0Buffer = DeformedLODDatas.DeformedRootTrianglePosition0Buffer.SRV;
	Parameters->DeformedPosition1Buffer = DeformedLODDatas.DeformedRootTrianglePosition1Buffer.SRV;
	Parameters->DeformedPosition2Buffer = DeformedLODDatas.DeformedRootTrianglePosition2Buffer.SRV;

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
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PLAIN"), 1);
	}	
};

IMPLEMENT_GLOBAL_SHADER(FVoxelPlainRaymarchingCS, "/Engine/Private/HairStrands/HairCardsVoxel.usf", "MainCS", SF_Compute);

static void AddVoxelPlainRaymarchingPass(
	FRHICommandListImmediate& RHICmdList,
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

		FRDGBuilder GraphBuilder(RHICmdList);

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
		GraphBuilder.Execute();
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, AtlasResolution)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ATLAS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardAtlasCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsAtlasPass(
	FRHICommandListImmediate& RHICmdList,
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

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(InOutputTexture, TEXT("SceneColorTexture"));
	FRDGTextureRef AtlasTexture = nullptr;

	const int32 DebugMode = FMath::Clamp(GHairCardsAtlasDebug, 1, 4);
	switch (DebugMode)
	{
	case 1: AtlasTexture = GraphBuilder.RegisterExternalTexture(Instance->Cards.LODs[LODIndex].RestResource->CardsDepthTextureRT); break;
	case 2: AtlasTexture = GraphBuilder.RegisterExternalTexture(Instance->Cards.LODs[LODIndex].RestResource->CardsCoverageTextureRT); break;
	case 3: AtlasTexture = GraphBuilder.RegisterExternalTexture(Instance->Cards.LODs[LODIndex].RestResource->CardsTangentTextureRT); break;
	case 4: AtlasTexture = GraphBuilder.RegisterExternalTexture(Instance->Cards.LODs[LODIndex].RestResource->CardsAttributeTextureRT); break;
	}

	if (AtlasTexture != nullptr)
	{
		TShaderMapRef<FDrawDebugCardAtlasCS> ComputeShader(ShaderMap);

		FDrawDebugCardAtlasCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardAtlasCS::FParameters>();
		Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
		Parameters->OutputResolution = SceneColorTexture->Desc.Extent;
		Parameters->AtlasResolution = AtlasTexture->Desc.Extent;
		Parameters->AtlasTexture = AtlasTexture;
		Parameters->DebugMode = DebugMode;
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->OutputTexture = GraphBuilder.CreateUAV(SceneColorTexture);

		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DrawDebugCardsAtlas"), ComputeShader, Parameters,
		FIntVector::DivideAndRoundUp(FIntVector(Parameters->OutputResolution.X, Parameters->OutputResolution.Y, 1), FIntVector(8, 8, 1)));

	}
	GraphBuilder.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugCardGuidesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCardGuidesCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCardGuidesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32,  DebugMode)
		
		SHADER_PARAMETER(uint32,  RenVertexCount)
		SHADER_PARAMETER(FVector, RenRestOffset)
		SHADER_PARAMETER(FVector, RenDeformedOffset)
		
		SHADER_PARAMETER(uint32,  SimVertexCount)
		SHADER_PARAMETER(FVector, SimRestOffset)
		SHADER_PARAMETER(FVector, SimDeformedOffset)

		SHADER_PARAMETER_SRV(Buffer, RenRestPosition)
		SHADER_PARAMETER_SRV(Buffer, RenDeformedPosition)

		SHADER_PARAMETER_SRV(Buffer, SimRestPosition)
		SHADER_PARAMETER_SRV(Buffer, SimDeformedPosition)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_GUIDE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardGuidesCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsGuidesPass(
	FRHICommandListImmediate& RHICmdList,
	const FSceneView& View,
	const FHairGroupInstance* Instance,
	const FShaderDrawDebugData* ShaderDrawData,
	const bool bDeformed, 
	const bool bRen)
{
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

	FRDGBuilder GraphBuilder(RHICmdList);
	
	TShaderMapRef<FDrawDebugCardGuidesCS> ComputeShader(ShaderMap);

	FDrawDebugCardGuidesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardGuidesCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->RenVertexCount = LOD.Guides.RestResource->GetVertexCount();
	Parameters->SimVertexCount = Instance->Guides.RestResource->GetVertexCount();

	Parameters->RenRestOffset = LOD.Guides.RestResource->PositionOffset;
	Parameters->RenRestPosition = LOD.Guides.RestResource->RestPositionBuffer.SRV;

	Parameters->RenDeformedOffset = LOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->RenDeformedPosition = LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV;

	Parameters->SimRestOffset = Instance->Guides.RestResource->PositionOffset;
	Parameters->SimRestPosition = Instance->Guides.RestResource->RestPositionBuffer.SRV;

	Parameters->SimDeformedOffset = Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->SimDeformedPosition = Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV;

	if (!bDeformed &&  bRen) Parameters->DebugMode = 1;
	if ( bDeformed &&  bRen) Parameters->DebugMode = 2;
	if (!bDeformed && !bRen) Parameters->DebugMode = 3;
	if ( bDeformed && !bRen) Parameters->DebugMode = 4;

	ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);

	const uint32 VertexCount = Parameters->DebugMode <= 2 ? Parameters->RenVertexCount : Parameters->SimVertexCount;
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DrawDebugCardsAtlas"), ComputeShader, Parameters,
	FIntVector::DivideAndRoundUp(FIntVector(VertexCount, 1, 1), FIntVector(32, 1, 1)));

	GraphBuilder.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RunHairStrandsDebug(
	FRHICommandListImmediate& RHICmdList,
	FGlobalShaderMap* ShaderMap,
	EWorldType::Type WorldType,
	const FSceneView& View,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	const TArray<FHairGroupInstance*>& Instances,
	TRefCountPtr<IPooledRenderTarget>& SceneColor,
	FIntRect Viewport,
	TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
{
	const EHairDebugMode HairDebugMode = GetHairStrandsDebugMode();

	if (HairDebugMode == EHairDebugMode::MacroGroups)
	{
		const float YStep = 14;
		float ClusterY = 38;

		// Component part of the clusters
		GroomDebug::FRenderTargetTemp TempRenderTarget(Viewport, (const FTexture2DRHIRef&)SceneColor->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, View.Family->CurrentWorldTime, View.Family->DeltaWorldTime, View.FeatureLevel);

		float X = 20;
		float Y = ClusterY;
		const FLinearColor InactiveColor(0.5, 0.5, 0.5);
		const FLinearColor DebugColor(1, 1, 0);
		const FLinearColor DebugGroupColor(0.5f, 0, 0);
		FString Line;

		Line = FString::Printf(TEXT("----------------------------------------------------------------"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		Line = FString::Printf(TEXT("Registered hair groups count : %d"), Instances.Num());
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		for (FHairGroupInstance* Instance : Instances)
		{
			const bool bIsActive = Instance->WorldType == WorldType;
			const bool bHasSkinInterpolation = Instance->Strands.RestRootResource != nullptr;
			const bool bHasBindingAsset = bHasSkinInterpolation && !Instance->Strands.bOwnRootResourceAllocation;

			Line = FString::Printf(TEXT(" * Id:%d | WorldType:%s | Group:%d/%d | Asset : %s | Skeletal : %s "), 
				Instance->Debug.ComponentId,
				ToString(Instance->WorldType), 
				Instance->Debug.GroupIndex,
				Instance->Debug.GroupCount, 
				*Instance->Debug.GroomAssetName, 
				*Instance->Debug.SkeletalComponentName);
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugColor : InactiveColor);

			Line = FString::Printf(TEXT("        |> CurveCount : %d | VertexCount : %d | MaxRadius : %f | MaxLength : %f | Skinned: %s | Binding: %s | Simulation: %s| LOD count : %d"),
				Instance->Strands.Data->GetNumCurves(),
				Instance->Strands.Data->GetNumPoints(),
				Instance->HairGroupPublicData->VFInput.Strands.HairRadius,
				Instance->HairGroupPublicData->VFInput.Strands.HairLength,
				bHasSkinInterpolation ? TEXT("True") : TEXT("False"),
				bHasBindingAsset ? TEXT("True") : TEXT("False"),
				Instance->Guides.bIsSimulationEnable ? TEXT("True") : TEXT("False"),
				Instance->Strands.ClusterCullingResource->ClusterLODInfos.Num());
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugGroupColor : InactiveColor);
		}

		Canvas.Flush_RenderThread(RHICmdList);

		ClusterY = Y;
	}

	if (HairDebugMode == EHairDebugMode::MeshProjection)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneColor, TEXT("SceneColorTexture"));
		{
			bool bClearDepth = true;
			FRDGTextureRef DepthTexture;
			{
				FRDGTextureDesc Desc;
				Desc.Extent = SceneColorTexture->Desc.Extent;
				Desc.Depth = 0;
				Desc.Format = PF_DepthStencil;
				Desc.NumMips = 1;
				Desc.NumSamples = 1;
				Desc.Flags = TexCreate_None;
				Desc.ClearValue = FClearValueBinding::DepthFar;
				DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairInterpolationDepthTexture"));
			}

			if (GHairDebugMeshProjection_SkinCacheMesh > 0)
			{
				auto RenderMeshProjection = [&bClearDepth, WorldType, ShaderMap, Viewport, &ViewUniformBuffer, Instances, SkinCache, &SceneColorTexture, &DepthTexture, &RHICmdList](FRDGBuilder& LocalGraphBuilder, EHairStrandsProjectionMeshType MeshType)
				{
					FHairStrandsProjectionMeshData::LOD MeshProjectionLODData;
					GetGroomInterpolationData(RHICmdList, Instances, WorldType, MeshType, SkinCache, MeshProjectionLODData);
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
					if (!Instance->HairGroupPublicData)
						continue;

					const int32 MeshLODIndex = Instance->Debug.MeshLODIndex;
					FHairStrandsRestRootResource* RestRootResource = Instance->Guides.RestRootResource;
					FHairStrandsDeformedRootResource* DeformedRootResource = Instance->Guides.DeformedRootResource;

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
		GraphBuilder.Execute();
	}

	if (GHairCardsVoxelDebug > 0)
	{
		for (FHairGroupInstance* Instance : Instances)
		{
			AddVoxelPlainRaymarchingPass(RHICmdList, View, Instance, ShaderDrawData, SceneColor);
		}
	}

	if (GHairCardsAtlasDebug > 0)
	{
		for (FHairGroupInstance* Instance : Instances)
		{
			AddDrawDebugCardsAtlasPass(RHICmdList, View, Instance, ShaderDrawData, SceneColor);
		}
	}

	if (GHairCardsGuidesDebug_Ren > 0 || GHairCardsGuidesDebug_Sim > 0)
	{
		for (FHairGroupInstance* Instance : Instances)
		{
			if (GHairCardsGuidesDebug_Ren > 0)
				AddDrawDebugCardsGuidesPass(RHICmdList, View, Instance, ShaderDrawData, GHairCardsGuidesDebug_Ren == 1, true);
			if (GHairCardsGuidesDebug_Sim > 0)
				AddDrawDebugCardsGuidesPass(RHICmdList, View, Instance, ShaderDrawData, GHairCardsGuidesDebug_Sim == 1, false);
		}
	}
}