// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomTextureBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "GroomAsset.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "GpuDebugRendering.h"
#include "CommonRenderResources.h"
#include "HairStrandsMeshProjection.h"
#include "Engine/StaticMesh.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY_STATIC(LogGroomTextureBuilder, Log, All);

#define LOCTEXT_NAMESPACE "GroomTextureBuilder"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Common

// Shared function for allocating and registering UTexture2D
// * TTextureAllocation implements the actual texture/resources allocation
// * TCreateFilename generates a unique filename. It is passed as a function pointer as it uses internally editor dependency, 
//   which we don't want to drag into this runtime module
typedef void (*TTextureAllocation)(UTexture2D* Out, uint32 Resolution, uint32 MipCount); 
static UTexture2D* InternalCreateTexture(const UGroomAsset* GroomAsset, uint32 Resolution, const FString& Suffix, TTextureAllocation TextureAllocation, FHairAssetHelper AssetHelper)
{
	FString Name;
	FString PackageName;
	AssetHelper.CreateFilename(GroomAsset->GetOutermost()->GetName(), Suffix, PackageName, Name);

	UObject* InParent = nullptr;
	UPackage* Package = Cast<UPackage>(InParent);
	if (InParent == nullptr && !PackageName.IsEmpty())
	{
		// Then find/create it.
		Package = CreatePackage(*PackageName);
		if (!ensure(Package))
		{
			// There was a problem creating the package
			return nullptr;
		}
	}

	if (UTexture2D* Out = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional))
	{
		const uint32 MipCount = FMath::FloorLog2(Resolution) + 1;
		TextureAllocation(Out, Resolution, MipCount);
		Out->MarkPackageDirty();

		// Notify the asset registry
		AssetHelper.RegisterTexture(Out);
		return Out;
	}

	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Follicle texture generation

void FGroomTextureBuilder::AllocateFollicleTextureResources(UTexture2D* Out)
{
	if (!Out)
	{
		return;
	}
	AllocateFollicleTextureResources(Out, Out->GetSizeX(), Out->GetNumMips());
}

void FGroomTextureBuilder::AllocateFollicleTextureResources(UTexture2D* Out, uint32 Resolution, uint32 MipCount)
{
	if (!Out)
	{
		return;
	}

	FTextureFormatSettings FormatSettings;
	FormatSettings.CompressionNone = true;
	FormatSettings.CompressionSettings = TC_Masks;
	FormatSettings.SRGB = false;

#if WITH_EDITORONLY_DATA
	Out->Source.Init(Resolution, Resolution, 1, MipCount, ETextureSourceFormat::TSF_BGRA8, nullptr);
#endif // #if WITH_EDITORONLY_DATA
	Out->LODGroup = TEXTUREGROUP_EffectsNotFiltered; // Mipmap filtering, no compression
#if WITH_EDITORONLY_DATA
	Out->SetLayerFormatSettings(0, FormatSettings);
#endif // #if WITH_EDITORONLY_DATA

	Out->PlatformData = new FTexturePlatformData();
	Out->PlatformData->SizeX = Resolution;
	Out->PlatformData->SizeY = Resolution;
	Out->PlatformData->PixelFormat = PF_B8G8R8A8;

	Out->UpdateResource();
}

UTexture2D* FGroomTextureBuilder::CreateGroomFollicleMaskTexture(const UGroomAsset* GroomAsset, uint32 Resolution, FHairAssetHelper Helper)
{
	if (!GroomAsset)
	{
		return nullptr;
	}

	UObject* FollicleMaskAsset = InternalCreateTexture(GroomAsset, Resolution, TEXT("_FollicleTexture"), FGroomTextureBuilder::AllocateFollicleTextureResources, Helper);
	return (UTexture2D*)FollicleMaskAsset;
}

struct FPixel
{
	uint8 V[4];
	FPixel() 
	{ 
		V[0] = 0;
		V[1] = 0;
		V[2] = 0;
		V[3] = 0;
	};

	FORCEINLINE uint8& Get(uint32 C) { return V[C]; }
	uint32 ToUint() const
	{
		return V[0] | (V[1] << 8) | (V[2] << 16) | (V[3] << 24);
	}
};

// CPU raster
static void RasterToTexture(int32 Resolution, int32 KernelExtent, uint32 Channel, const FHairStrandsDatas& InStrandsData, FPixel* OutPixels)
{
	const uint32 CurveCount = InStrandsData.GetNumCurves();
	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const FVector2D& RootUV = InStrandsData.StrandsCurves.CurvesRootUV[CurveIndex];

		const FIntPoint RootCoord(
			FMath::Clamp(int32(RootUV.X * Resolution), 0, Resolution - 1),
			FMath::Clamp(int32(RootUV.Y * Resolution), 0, Resolution - 1));

		for (int32 Y = -KernelExtent; Y <= KernelExtent; ++Y)
		for (int32 X = -KernelExtent; X <= KernelExtent; ++X)
		{
			const FIntPoint Coord = RootCoord + FIntPoint(X, Y);
			if (Coord.X < 0 || Coord.X >= Resolution || Coord.Y < 0 || Coord.Y >= Resolution)
				continue;

			const FVector2D fCoord(Coord.X + 0.5f, Coord.Y+0.5f);
			const float Distance = FVector2D::Distance(fCoord, RootCoord);
			const float V = FMath::Clamp(1.f - (Distance / KernelExtent), 0.f, 1.f);

			const uint32 V8Bits = FMath::Clamp(uint32(V * 0xFF), 0u, 0xFFu);

			const uint32 LinearCoord = Coord.X + Coord.Y * Resolution;
			OutPixels[LinearCoord].Get(Channel) = FMath::Max(uint32(OutPixels[LinearCoord].Get(Channel)), V8Bits);
		}
	}
}

// GPU raster
static void InternalGenerateFollicleTexture_GPU(
	FRDGBuilder& GraphBuilder,
	bool bCopyDataBackToCPU,
	EPixelFormat Format,
	uint32 InKernelSizeInPixels,
	const TArray<FRWBuffer>& InRootUVBuffers_R,
	const TArray<FRWBuffer>& InRootUVBuffers_G,
	const TArray<FRWBuffer>& InRootUVBuffers_B,
	const TArray<FRWBuffer>& InRootUVBuffers_A,
	UTexture2D* OutTexture)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	if (OutTexture == nullptr ||
		(InRootUVBuffers_R.Num() == 0 &&
			InRootUVBuffers_G.Num() == 0 &&
			InRootUVBuffers_B.Num() == 0 &&
			InRootUVBuffers_A.Num() == 0))
	{
		return;
	}

	TRefCountPtr<IPooledRenderTarget> OutMaskTexture;

	const uint32 MipCount = OutTexture->GetNumMips();
	const FIntPoint Resolution(OutTexture->Resource->GetSizeX(), OutTexture->Resource->GetSizeY());
	check(OutTexture->Resource->GetSizeX() == OutTexture->Resource->GetSizeY());

	FRDGTextureRef FollicleMaskTexture = nullptr;
	if (InRootUVBuffers_R.Num())
	{
		GenerateFolliculeMask(
			GraphBuilder,
			ShaderMap,
			Format,
			Resolution,
			MipCount,
			InKernelSizeInPixels,
			0,
			InRootUVBuffers_R,
			FollicleMaskTexture);
	}
	if (InRootUVBuffers_G.Num())
	{
		GenerateFolliculeMask(
			GraphBuilder,
			ShaderMap,
			Format,
			Resolution,
			MipCount,
			InKernelSizeInPixels,
			1,
			InRootUVBuffers_G,
			FollicleMaskTexture);
	}
	if (InRootUVBuffers_B.Num())
	{
		GenerateFolliculeMask(
			GraphBuilder,
			ShaderMap,
			Format,
			Resolution,
			MipCount,
			InKernelSizeInPixels,
			2,
			InRootUVBuffers_B,
			FollicleMaskTexture);
	}
	if (InRootUVBuffers_A.Num())
	{
		GenerateFolliculeMask(
			GraphBuilder,
			ShaderMap,
			Format,
			Resolution,
			MipCount,
			InKernelSizeInPixels,
			3,
			InRootUVBuffers_A,
			FollicleMaskTexture);
	}
	AddComputeMipsPass(GraphBuilder, ShaderMap, FollicleMaskTexture);

	GraphBuilder.QueueTextureExtraction(FollicleMaskTexture, &OutMaskTexture);

	check(FollicleMaskTexture->Desc.Format == OutTexture->GetPixelFormat());

	// Select if the generated texture should be copy back to a CPU texture for being saved, or directly used
#if WITH_EDITOR
	if (bCopyDataBackToCPU)
	{
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ReadbackGroomTextures"),
			ERDGPassFlags::None,
			[FollicleMaskTexture, OutMaskTexture, MipCount, OutTexture](FRHICommandListImmediate& RHICmdList)
		{
			FRHIResourceCreateInfo CreateInfo;
			FTexture2DRHIRef StagingTexture = RHICreateTexture2D(
				FollicleMaskTexture->Desc.Extent.X,
				FollicleMaskTexture->Desc.Extent.Y,
				FollicleMaskTexture->Desc.Format,
				FollicleMaskTexture->Desc.NumMips,
				1, TexCreate_CPUReadback, CreateInfo);

			FRHICopyTextureInfo CopyInfo;
			CopyInfo.NumMips = MipCount;
			RHICmdList.CopyTexture(
				OutMaskTexture->GetRenderTargetItem().ShaderResourceTexture,
				StagingTexture->GetTexture2D(),// OutTexture->Resource->TextureRHI,
				CopyInfo);

			GDynamicRHI->RHISubmitCommandsAndFlushGPU();
			GDynamicRHI->RHIBlockUntilGPUIdle();

			void* InData = nullptr;
			int32 Width = 0, Height = 0;
			RHICmdList.MapStagingSurface(StagingTexture, InData, Width, Height);
			uint32* InDataRGBA8 = (uint32*)InData;

			uint64 Offset = 0;
			uint8 MipIndex = 0;
			for (FTexture2DMipMap& Mip : OutTexture->PlatformData->Mips)
			{
				const uint32 MipResolution = Mip.SizeX;
				const uint32 SizeInBytes = sizeof(uint32) * MipResolution * MipResolution;
				const uint32 PixelCount = MipResolution * MipResolution;

				// Store the mapped data into the texture 'source' data for being enable to 
				// reimport/recompression/process per platform (the bulk data will be populated on save)
				uint8* OutData = OutTexture->Source.LockMip(MipIndex);
				FMemory::Memcpy(OutData, InDataRGBA8 + Offset, SizeInBytes);
				OutTexture->Source.UnlockMip(MipIndex);

				Offset += PixelCount;
				++MipIndex;
			}

			RHICmdList.UnmapStagingSurface(StagingTexture);

			OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
			OutTexture->MarkPackageDirty();
		});
	}
	else
#endif
	{
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopyGroomTextures"),
			ERDGPassFlags::None,
		[OutMaskTexture, MipCount, OutTexture](FRHICommandListImmediate& RHICmdList)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.NumMips = MipCount;
			RHICmdList.CopyTexture(
				OutMaskTexture->GetRenderTargetItem().ShaderResourceTexture,
				OutTexture->Resource->TextureRHI,
				CopyInfo);
		});
	}
}


// CPU path
static void InternalBuildFollicleTexture_CPU(const TArray<FFollicleInfo>& InInfos, UTexture2D* OutTexture)
{
#if WITH_EDITORONLY_DATA
	const FIntPoint Resolution(OutTexture->GetSizeX(), OutTexture->GetSizeY());
	check(OutTexture->GetSizeX() == OutTexture->GetSizeY());

	uint8* OutData = OutTexture->Source.LockMip(0);
	FMemory::Memzero(OutData, Resolution.X * Resolution.Y * sizeof(uint32));
	FPixel* Pixels = (FPixel*)OutData;
	for (const FFollicleInfo& Info : InInfos)
	{
		if (!Info.GroomAsset)
		{
			continue;
		}

		// The output pixel format is PF_B8G8R8A8. So remap channel to map onto the RGBA enum Info.Channel
		uint32 Channel = 0;
		switch (Info.Channel)
		{
			case FFollicleInfo::B: Channel = 0; break;
			case FFollicleInfo::G: Channel = 1; break;
			case FFollicleInfo::R: Channel = 2; break;
			case FFollicleInfo::A: Channel = 3; break;
		}

		for (const FHairGroupData& HairGroupData : Info.GroomAsset->HairGroupsData)
		{
			RasterToTexture(Resolution.X, Info.KernelSizeInPixels, Channel, HairGroupData.Strands.Data, Pixels);
		}
	}
	OutTexture->Source.UnlockMip(0);
	OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
	OutTexture->MarkPackageDirty();
#endif // #if WITH_EDITORONLY_DATA
}

// GPU path
static void InternalBuildFollicleTexture_GPU(
	FRDGBuilder& GraphBuilder,
	const TArray<FFollicleInfo>& InInfos,
	UTexture2D* OutTexture)
{
	uint32 KernelSizeInPixels = ~0;
	TArray<FRWBuffer> RootUVBuffers[4];
	bool bCopyDataBackToCPU = false;
	for (const FFollicleInfo& Info : InInfos)
	{
		if (!Info.GroomAsset || Info.GroomAsset->GetNumHairGroups() == 0)
		{
			UE_LOG(LogHairStrands, Warning, TEXT("[Groom] Error - Groom follicle texture can be entirely created/rebuilt as some groom assets seems invalid."));
			continue;
		}

		if (KernelSizeInPixels == ~0)
		{
			KernelSizeInPixels = Info.KernelSizeInPixels;
			bCopyDataBackToCPU = !Info.bGPUOnly;
		}

		// Create root UVs buffer
		for (const FHairGroupData& GroupData : Info.GroomAsset->HairGroupsData)
		{
			GroupData.Strands.Data.StrandsCurves.CurvesRootUV.GetData();
			const uint32 DataCount = GroupData.Strands.Data.StrandsCurves.CurvesRootUV.Num();
			const uint32 DataSizeInBytes = sizeof(FVector2D) * DataCount;
			check(DataSizeInBytes != 0);

			FRWBuffer& OutBuffer = RootUVBuffers[Info.Channel].AddDefaulted_GetRef();
			OutBuffer.Initialize(sizeof(FVector2D), DataCount, PF_G32R32F, BUF_Static);
			void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);

			FMemory::Memcpy(BufferData, GroupData.Strands.Data.StrandsCurves.CurvesRootUV.GetData(), DataSizeInBytes);
			RHIUnlockVertexBuffer(OutBuffer.Buffer);
		}
	}

	const EPixelFormat Format = bCopyDataBackToCPU ? PF_B8G8R8A8 : PF_R8G8B8A8;		 
	InternalGenerateFollicleTexture_GPU(GraphBuilder, bCopyDataBackToCPU, Format, KernelSizeInPixels, RootUVBuffers[0], RootUVBuffers[1], RootUVBuffers[2], RootUVBuffers[3], OutTexture);

	for (uint32 Channel = 0; Channel < 4; ++Channel)
	{
		for (FRWBuffer& RootUVBuffer : RootUVBuffers[Channel])
		{
			RootUVBuffer.Release();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Asynchronous queuing for follicle texture mask generation
struct FFollicleQuery
{
	TArray<FFollicleInfo> Infos;
	UTexture2D* OutTexture = nullptr;
};
TQueue<FFollicleQuery> GFollicleQueries;

bool HasHairStrandsFolliculeMaskQueries()
{
	return !GFollicleQueries.IsEmpty();
}

void RunHairStrandsFolliculeMaskQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap)
{
	FFollicleQuery Q;
	while (GFollicleQueries.Dequeue(Q))
	{
		if (Q.Infos.Num() > 0 && Q.OutTexture)
		{
			InternalBuildFollicleTexture_GPU(GraphBuilder, Q.Infos, Q.OutTexture);
		}
	}
}

void FGroomTextureBuilder::BuildFollicleTexture(const TArray<FFollicleInfo>& InInfos, UTexture2D* OutTexture, bool bUseGPU)
{
	if (!OutTexture || InInfos.Num() == 0)
	{
		UE_LOG(LogGroomTextureBuilder, Warning, TEXT("[Groom] Error - Follicle texture can't be created/rebuilt."));
		return;
	}

	if (bUseGPU)
	{
		// Asynchronous (GPU)
		GFollicleQueries.Enqueue({ InInfos, OutTexture });
	}
	else
	{
		// Synchronous (CPU)
		InternalBuildFollicleTexture_CPU(InInfos, OutTexture);
		// maybe warning here?
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Strands texture generation

static void InternalAllocateStrandsTexture(UTexture2D* Out, uint32 Resolution, uint32 MipCount, EPixelFormat Format, ETextureSourceFormat SourceFormat)
{
	FTextureFormatSettings FormatSettings;
	FormatSettings.SRGB = false;
#if WITH_EDITORONLY_DATA
	Out->Source.Init(Resolution, Resolution, 1, MipCount, SourceFormat, nullptr);
	Out->SetLayerFormatSettings(0, FormatSettings);
#endif // #if WITH_EDITORONLY_DATA

	Out->PlatformData = new FTexturePlatformData();
	Out->PlatformData->SizeX = Resolution;
	Out->PlatformData->SizeY = Resolution;
	Out->PlatformData->PixelFormat = Format;

	Out->UpdateResource();
}

static void InternalAllocateStrandsTexture_Coverage(UTexture2D* Out, uint32 Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

static void InternalAllocateStrandsTexture_Tangent(UTexture2D* Out, uint32 Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

static void InternalAllocateStrandsTexture_Attribute(UTexture2D* Out, uint32 Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

FStrandsTexturesOutput FGroomTextureBuilder::CreateGroomStrandsTexturesTexture(const UGroomAsset* GroomAsset, uint32 Resolution, FHairAssetHelper Helper)
{
	FStrandsTexturesOutput Output;

	if (!GroomAsset)
	{
		return Output;
	}

	Output.Coverage = InternalCreateTexture(GroomAsset, Resolution, TEXT("_Opacity"), InternalAllocateStrandsTexture_Coverage, Helper);
	Output.Tangent = InternalCreateTexture(GroomAsset, Resolution, TEXT("_Tangent"), InternalAllocateStrandsTexture_Tangent, Helper);
	Output.Attribute = InternalCreateTexture(GroomAsset, Resolution, TEXT("_Attribute"), InternalAllocateStrandsTexture_Attribute, Helper);
	return Output;
}

class FHairStrandsTextureVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsTextureVS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsTextureVS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, VertexCount)

		SHADER_PARAMETER(uint32, UVsChannelIndex)
		SHADER_PARAMETER(uint32, UVsChannelCount)

		SHADER_PARAMETER_SRV(Buffer, VertexBuffer)
		SHADER_PARAMETER_SRV(Buffer, UVsBuffer)
		SHADER_PARAMETER_SRV(Buffer, NormalsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VERTEX"), 1);
	}
};

class FHairStrandsTexturePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsTexturePS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsTexturePS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(float, MaxDistance)

		SHADER_PARAMETER(uint32, UVsChannelIndex)
		SHADER_PARAMETER(uint32, UVsChannelCount)

		SHADER_PARAMETER(float, InVF_Radius)
		SHADER_PARAMETER(float, InVF_Length)
		SHADER_PARAMETER(FVector, InVF_PositionOffset)
		SHADER_PARAMETER_SRV(Buffer, InVF_PositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, InVF_AttributeBuffer)
		SHADER_PARAMETER(uint32, InVF_ControlPointCount)

		SHADER_PARAMETER(FVector, Voxel_MinBound)
		SHADER_PARAMETER(FVector, Voxel_MaxBound)
		SHADER_PARAMETER(FIntVector, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_Size)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Voxel_OffsetAndCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Voxel_Data)

		SHADER_PARAMETER_SRV(Buffer, VertexBuffer)
		SHADER_PARAMETER_SRV(Buffer, UVsBuffer)
		SHADER_PARAMETER_SRV(Buffer, NormalsBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PIXEL"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsTextureVS, "/Engine/Private/HairStrands/HairStrandsTexturesGeneration.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairStrandsTexturePS, "/Engine/Private/HairStrands/HairStrandsTexturesGeneration.usf", "MainPS", SF_Pixel);

static void InternalGenerateHairStrandsTextures(
	FRDGBuilder& GraphBuilder,
	const FShaderDrawDebugData* ShaderDrawData,
	const bool bClear,
	const float InMaxDistance, 
	const uint32 VertexCount,
	const uint32 PrimitiveCount,

	const uint32 VertexBaseIndex,
	const uint32 IndexBaseIndex,

	const uint32 UVsChannelIndex,
	const uint32 UVsChannelCount,

	FRHIIndexBuffer* InMeshIndexBuffer,
	FRHIShaderResourceView* InMeshVertexBuffer,
	FRHIShaderResourceView* InMeshUVsBuffer,
	FRHIShaderResourceView* InMeshNormalsBuffer,

	const FVector& VoxelMinBound,
	const FVector& VoxelMaxBound,
	const FIntVector& VoxelResolution,
	float VoxelSize,
	FRDGBufferRef VoxelOffsetAndCount,
	FRDGBufferRef VoxelData,
	
	FRHIShaderResourceView* InHairStrands_PositionBuffer,
	FRHIShaderResourceView* InHairStrands_AttributeBuffer,
	const FVector& InHairStrands_PositionOffset,
	float InHairStrands_Radius,
	float InHairStrands_Length,
	uint32 InHairStrands_ControlPointCount,

	FRDGTextureRef OutDepthTexture,
	FRDGTextureRef OutTangentTexture,
	FRDGTextureRef OutCoverageTexture,
	FRDGTextureRef OutRootUVStrandsUSeedTexture)
{
	const FIntPoint OutputResolution = OutDepthTexture->Desc.Extent;

	FHairStrandsTexturePS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FHairStrandsTexturePS::FParameters>();
	ParametersPS->OutputResolution = OutputResolution;
	ParametersPS->VertexCount = VertexCount;
	ParametersPS->VertexBuffer = InMeshVertexBuffer;
	ParametersPS->UVsBuffer = InMeshUVsBuffer;
	ParametersPS->NormalsBuffer = InMeshNormalsBuffer;
	ParametersPS->MaxDistance = InMaxDistance;

	ParametersPS->UVsChannelIndex = UVsChannelIndex;
	ParametersPS->UVsChannelCount = UVsChannelCount;

	ParametersPS->InVF_PositionBuffer = InHairStrands_PositionBuffer;
	ParametersPS->InVF_PositionOffset = InHairStrands_PositionOffset;
	ParametersPS->InVF_AttributeBuffer = InHairStrands_AttributeBuffer;
	ParametersPS->InVF_Radius = InHairStrands_Radius;
	ParametersPS->InVF_Length = InHairStrands_Length;
	ParametersPS->InVF_ControlPointCount = InHairStrands_ControlPointCount;

	ParametersPS->Voxel_MinBound = VoxelMinBound;
	ParametersPS->Voxel_MaxBound = VoxelMaxBound;
	ParametersPS->Voxel_Resolution = VoxelResolution;
	ParametersPS->Voxel_Size = VoxelSize;
	ParametersPS->Voxel_OffsetAndCount = GraphBuilder.CreateSRV(VoxelOffsetAndCount);
	ParametersPS->Voxel_Data = GraphBuilder.CreateSRV(VoxelData);

	if (ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, ParametersPS->ShaderDrawParameters);
	}

	ParametersPS->RenderTargets[0] = FRenderTargetBinding(OutDepthTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
	ParametersPS->RenderTargets[1] = FRenderTargetBinding(OutTangentTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
	ParametersPS->RenderTargets[2] = FRenderTargetBinding(OutCoverageTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
	ParametersPS->RenderTargets[3] = FRenderTargetBinding(OutRootUVStrandsUSeedTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FHairStrandsTextureVS> VertexShader(ShaderMap);
	TShaderMapRef<FHairStrandsTexturePS> PixelShader(ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsTexturePS"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, InMeshIndexBuffer, VertexCount, PrimitiveCount, IndexBaseIndex, VertexBaseIndex, OutputResolution](FRHICommandList& RHICmdList)
		{
			FHairStrandsTextureVS::FParameters ParametersVS;
			ParametersVS.OutputResolution = ParametersPS->OutputResolution;
			ParametersVS.VertexCount = ParametersPS->VertexCount;
			ParametersVS.VertexBuffer = ParametersPS->VertexBuffer;
			ParametersVS.UVsChannelIndex = ParametersPS->UVsChannelIndex;
			ParametersVS.UVsChannelCount = ParametersPS->UVsChannelCount;
			ParametersVS.UVsBuffer = ParametersPS->UVsBuffer;
			ParametersVS.NormalsBuffer = ParametersPS->NormalsBuffer;

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<
				CW_RGBA, BO_Max, BF_One, BF_Zero, BO_Max, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_One,  BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_One,  BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			
			// Divide the rendering work into small batches to reduce risk of TDR as the texture projection implies heavy works 
			// (i.e. long thread running due the the large amount of strands a groom can have)
			const int32 TileSize = 1024;
			const int32 TileCountX = FMath::DivideAndRoundUp(OutputResolution.X, TileSize);
			const int32 TileCountY = FMath::DivideAndRoundUp(OutputResolution.Y, TileSize);
			for (int32 TileY = 0; TileY < TileCountY; ++TileY)
			for (int32 TileX = 0; TileX < TileCountX; ++TileX)
			{
				const uint32 OffsetX = TileX * TileSize;
				const uint32 OffsetY = TileY * TileSize;
				RHICmdList.SetScissorRect(true, OffsetX, OffsetY, OffsetX + TileSize, OffsetY + TileSize);
				RHICmdList.DrawIndexedPrimitive(InMeshIndexBuffer, VertexBaseIndex, 0, VertexCount, IndexBaseIndex, PrimitiveCount, 1);

				// Flush, to ensure that all texture generation is done (TDR)
				GDynamicRHI->RHISubmitCommandsAndFlushGPU();
				GDynamicRHI->RHIBlockUntilGPUIdle();
			}
		});
}


// TODO: Wrap this into a Graph pass
void AddReadBackTexturePass(FRDGBuilder& GraphBuilder, uint32 OutputResolution, TRefCountPtr<IPooledRenderTarget>& InTexture, const FRDGTextureDesc& InDesc, UTexture2D* OutTexture)
{
	AddPass(GraphBuilder, [OutputResolution, InTexture, InDesc, OutTexture](FRHICommandListImmediate& RHICmdList)
	{
		check(OutTexture->GetSurfaceWidth() == OutputResolution);
		check(OutTexture->GetSurfaceHeight() == OutputResolution);

		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef StagingTexture = RHICreateTexture2D(
			InDesc.Extent.X,
			InDesc.Extent.Y,
			InDesc.Format,
			InDesc.NumMips,
			1, TexCreate_CPUReadback, CreateInfo);

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.NumMips = InDesc.NumMips;
		RHICmdList.CopyTexture(
			InTexture->GetRenderTargetItem().ShaderResourceTexture,
			StagingTexture->GetTexture2D(),
			CopyInfo);

		// Flush, to ensure that all texture generation is done
		GDynamicRHI->RHISubmitCommandsAndFlushGPU();
		GDynamicRHI->RHIBlockUntilGPUIdle();

		// don't think we can mark package as a dirty in the package build
	#if WITH_EDITORONLY_DATA
		void* InData = nullptr;
		int32 Width = 0, Height = 0;
		RHICmdList.MapStagingSurface(StagingTexture, InData, Width, Height);
		uint32* InDataRGBA8 = (uint32*)InData;

		uint8 MipIndex = 0;
		const uint32 SizeInBytes = sizeof(uint32) * OutputResolution * OutputResolution;
		uint8* OutData = OutTexture->Source.LockMip(0);
		FMemory::Memcpy(OutData, InDataRGBA8, SizeInBytes);
		OutTexture->Source.UnlockMip(0);

		RHICmdList.UnmapStagingSurface(StagingTexture);

		OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
		OutTexture->MarkPackageDirty();
	#endif // #if WITH_EDITORONLY_DATA
	});
}

static void InternalBuildStrandsTextures_GPU(
	FRDGBuilder& GraphBuilder,
	const FStrandsTexturesInfo& InInfo,
	const FStrandsTexturesOutput& Output,
	const struct FShaderDrawDebugData* DebugShaderData)
{
	USkeletalMesh* SkeletalMesh = (USkeletalMesh*)InInfo.SkeletalMesh;
	UStaticMesh* StaticMesh = (UStaticMesh*)InInfo.StaticMesh;

	if (!SkeletalMesh && !StaticMesh)
	{
		return;
	}

	const bool bUseSkeletalMesh = SkeletalMesh != nullptr;

	const uint32 OutputResolution = FMath::Clamp(InInfo.Resolution, 512u, 16384u);

	FRDGTextureDesc Desc;
	Desc.Extent.X = OutputResolution;
	Desc.Extent.Y = OutputResolution;
	Desc.Depth = 0;
	Desc.NumMips = 1;
	Desc.Flags = TexCreate_None;
	Desc.Format = PF_A8R8G8B8;

	Desc.Format = PF_R32_FLOAT;
	FRDGTextureRef DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("DepthTexture"));

	Desc.Format = PF_B8G8R8A8;
	FRDGTextureRef CoverageTexture = GraphBuilder.CreateTexture(Desc, TEXT("CoverageTexture"));

	Desc.Format = PF_B8G8R8A8;
	FRDGTextureRef TangentTexture = GraphBuilder.CreateTexture(Desc, TEXT("TangentTexture"));

	Desc.Format = PF_B8G8R8A8;
	FRDGTextureRef Attribute_Texture = GraphBuilder.CreateTexture(Desc, TEXT("StrandsU_Seed_Texture"));

	bool bClear = true;
	const uint32 GroupCount = InInfo.GroomAsset->GetNumHairGroups();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairGroupData& GroupData = InInfo.GroomAsset->HairGroupsData[GroupIndex];
		FHairGroupsRendering& RenderingData = InInfo.GroomAsset->HairGroupsRendering[GroupIndex];

		FRDGBufferRef VoxelOffsetAndCount = GraphBuilder.RegisterExternalBuffer(GroupData.Debug.Resource->VoxelOffsetAndCount);
		FRDGBufferRef VoxelData = GraphBuilder.RegisterExternalBuffer(GroupData.Debug.Resource->VoxelData);

		const uint32 MeshLODIndex = 0;
		{
			FRHIShaderResourceView* PositionBuffer = nullptr;
			FRHIShaderResourceView* UVsBuffer = nullptr;
			FRHIShaderResourceView* TangentBuffer = nullptr;
			FIndexBufferRHIRef IndexBuffer = nullptr;
			uint32 TotalVertexCount = 0;
			uint32 TotalIndexCount = 0;
			uint32 UVsChannelIndex = InInfo.UVChannelIndex;
			uint32 UVsChannelCount = 0;
			uint32 NumPrimitives = 0;
			uint32 IndexBaseIndex = 0;
			uint32 VertexBaseIndex = 0;

			// Skeletal mesh
			if (bUseSkeletalMesh)
			{
				const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
				const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[MeshLODIndex];
				const uint32 SectionCount = LODData.RenderSections.Num();
				const uint32 SectionIdx = FMath::Clamp(InInfo.SectionIndex, 0u, SectionCount);
				const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];

				PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer.GetSRV();
				UVsBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
				TangentBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
				IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer()->IndexBufferRHI;
				TotalIndexCount = LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
				UVsChannelCount = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
				TotalVertexCount = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();

				NumPrimitives = Section.NumTriangles;
				IndexBaseIndex = Section.BaseIndex;
				VertexBaseIndex = Section.BaseVertexIndex;
			}
			// Static mesh
			else
			{
				const FStaticMeshLODResources& LODData = StaticMesh->GetLODForExport(MeshLODIndex);

				const uint32 SectionCount = LODData.Sections.Num();
				const uint32 SectionIdx = FMath::Clamp(InInfo.SectionIndex, 0u, SectionCount);
				const FStaticMeshSection& Section = LODData.Sections[SectionIdx];

				PositionBuffer = LODData.VertexBuffers.PositionVertexBuffer.GetSRV();
				UVsBuffer = LODData.VertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
				TangentBuffer = LODData.VertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
				IndexBuffer = LODData.IndexBuffer.IndexBufferRHI;
				TotalIndexCount = LODData.IndexBuffer.GetNumIndices();
				UVsChannelCount = LODData.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
				TotalVertexCount = LODData.VertexBuffers.PositionVertexBuffer.GetNumVertices();

				NumPrimitives = Section.NumTriangles;
				IndexBaseIndex = Section.FirstIndex;
				VertexBaseIndex = 0;
			}

			InternalGenerateHairStrandsTextures(
				GraphBuilder,
				DebugShaderData,
				bClear,
				InInfo.MaxTracingDistance,

				TotalVertexCount,
				NumPrimitives,
				VertexBaseIndex,
				IndexBaseIndex,

				UVsChannelIndex,
				UVsChannelCount,

				IndexBuffer,
				PositionBuffer,
				UVsBuffer,
				TangentBuffer,

				GroupData.Debug.Resource->VoxelDescription.VoxelMinBound,
				GroupData.Debug.Resource->VoxelDescription.VoxelMaxBound,
				GroupData.Debug.Resource->VoxelDescription.VoxelResolution,
				GroupData.Debug.Resource->VoxelDescription.VoxelSize,
				VoxelOffsetAndCount,
				VoxelData,

				GroupData.Strands.RestResource->RestPositionBuffer.SRV,
				GroupData.Strands.RestResource->AttributeBuffer.SRV,
				GroupData.Strands.RestResource->PositionOffset,
				RenderingData.GeometrySettings.HairWidth * 0.5f,
				GroupData.Strands.Data.StrandsCurves.MaxLength,
				GroupData.Strands.Data.RenderData.Positions.Num(),

				DepthTexture,
				CoverageTexture,
				TangentTexture,
				Attribute_Texture);

			bClear = false;
		}
	}

	TRefCountPtr<IPooledRenderTarget> DepthTextureRT;
	TRefCountPtr<IPooledRenderTarget> CoverageTextureRT;
	TRefCountPtr<IPooledRenderTarget> TangentTextureRT;
	TRefCountPtr<IPooledRenderTarget> AttributeTextureRT;

	GraphBuilder.QueueTextureExtraction(DepthTexture, &DepthTextureRT);
	GraphBuilder.QueueTextureExtraction(CoverageTexture, &CoverageTextureRT);
	GraphBuilder.QueueTextureExtraction(TangentTexture, &TangentTextureRT);
	GraphBuilder.QueueTextureExtraction(Attribute_Texture, &AttributeTextureRT);

	// Readback


	/*
	// TODO: Wrap this into a Graph pass
	auto ReadBackTexture = [&RHICmdList, OutputResolution](TRefCountPtr<IPooledRenderTarget>& InTexture, const FRDGTextureDesc& InDesc, UTexture2D* OutTexture)
	{
		check(OutTexture->GetSurfaceWidth() == OutputResolution);
		check(OutTexture->GetSurfaceHeight() == OutputResolution);

		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef StagingTexture = RHICreateTexture2D(
			InDesc.Extent.X,
			InDesc.Extent.Y,
			InDesc.Format,
			InDesc.NumMips,
			1, TexCreate_CPUReadback, CreateInfo);

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.NumMips = InDesc.NumMips;
		RHICmdList.CopyTexture(
			InTexture->GetRenderTargetItem().ShaderResourceTexture,
			StagingTexture->GetTexture2D(),
			CopyInfo);

		// Flush, to ensure that all texture generation is done
		GDynamicRHI->RHISubmitCommandsAndFlushGPU();
		GDynamicRHI->RHIBlockUntilGPUIdle();

		// don't think we can mark package as a dirty in the package build
#if WITH_EDITORONLY_DATA
		void* InData = nullptr;
		int32 Width = 0, Height = 0;
		RHICmdList.MapStagingSurface(StagingTexture, InData, Width, Height);
		uint32* InDataRGBA8 = (uint32*)InData;

		uint8 MipIndex = 0;
		const uint32 SizeInBytes = sizeof(uint32) * OutputResolution * OutputResolution;
		uint8* OutData = OutTexture->Source.LockMip(0);
		FMemory::Memcpy(OutData, InDataRGBA8, SizeInBytes);
		OutTexture->Source.UnlockMip(0);

		RHICmdList.UnmapStagingSurface(StagingTexture);

		OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
		OutTexture->MarkPackageDirty();
#endif // #if WITH_EDITORONLY_DATA
	};
	*/

	AddReadBackTexturePass(GraphBuilder, OutputResolution, CoverageTextureRT, CoverageTexture->Desc, Output.Coverage);
	AddReadBackTexturePass(GraphBuilder, OutputResolution, TangentTextureRT, TangentTexture->Desc, Output.Tangent);
	AddReadBackTexturePass(GraphBuilder, OutputResolution, AttributeTextureRT, TangentTexture->Desc, Output.Attribute);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Asynchronous queuing for hair strands texture generation

struct FStrandsTexturesQuery
{
	FStrandsTexturesInfo Info;
	FStrandsTexturesOutput Output;
};
TQueue<FStrandsTexturesQuery> GStrandsTexturesQueries;

bool HasHairStrandsTexturesQueries()
{
	return !GStrandsTexturesQueries.IsEmpty();
}

void RunHairStrandsTexturesQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData)
{
	FStrandsTexturesQuery Q;
	while (GStrandsTexturesQueries.Dequeue(Q))
	{
		InternalBuildStrandsTextures_GPU(GraphBuilder, Q.Info, Q.Output, DebugShaderData);
	}
}

void FGroomTextureBuilder::BuildStrandsTextures(const FStrandsTexturesInfo& InInfo, const FStrandsTexturesOutput& Output)
{
	GStrandsTexturesQueries.Enqueue({ InInfo, Output });
}

#undef LOCTEXT_NAMESPACE

