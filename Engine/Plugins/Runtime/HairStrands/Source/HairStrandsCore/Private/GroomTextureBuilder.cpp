// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomTextureBuilder.h"
#include "HairStrandsCore.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "GroomAsset.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "ShaderDebug.h"
#include "CommonRenderResources.h"
#include "HairStrandsMeshProjection.h"
#include "Engine/StaticMesh.h"

static int32 GHairStrandsTextureDilationPassCount = 8;
static FAutoConsoleVariableRef CVarHairStrandsTextureDilationPassCount(TEXT("r.HairStrands.Textures.DilationCount"), GHairStrandsTextureDilationPassCount, TEXT("Number of dilation pass run onto the generated hair strands textures (Default:8)."));

///////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY_STATIC(LogGroomTextureBuilder, Log, All);

#define LOCTEXT_NAMESPACE "GroomTextureBuilder"

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

void FGroomTextureBuilder::AllocateFollicleTextureResources(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
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
	Out->Source.Init(Resolution.X, Resolution.Y, 1, MipCount, ETextureSourceFormat::TSF_BGRA8, nullptr);
#endif // #if WITH_EDITORONLY_DATA
	Out->LODGroup = TEXTUREGROUP_EffectsNotFiltered; // Mipmap filtering, no compression
#if WITH_EDITORONLY_DATA
	Out->SetLayerFormatSettings(0, FormatSettings);
#endif // #if WITH_EDITORONLY_DATA

	Out->SetPlatformData(new FTexturePlatformData());
	Out->GetPlatformData()->SizeX = Resolution.X;
	Out->GetPlatformData()->SizeY = Resolution.Y;
	Out->GetPlatformData()->PixelFormat = PF_B8G8R8A8;

	// No need to create the resources when using the CPU path.
	//Out->UpdateResource();
}

#if WITH_EDITOR
UTexture2D* FGroomTextureBuilder::CreateGroomFollicleMaskTexture(const UGroomAsset* GroomAsset, uint32 InResolution)
{
	if (!GroomAsset)
	{
		return nullptr;
	}

	const FString PackageName = GroomAsset->GetOutermost()->GetName();
	const FIntPoint Resolution(InResolution, InResolution);
	UObject* FollicleMaskAsset = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_FollicleTexture"), FGroomTextureBuilder::AllocateFollicleTextureResources);
	return (UTexture2D*)FollicleMaskAsset;
}
#endif

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
		FVector2f RootUV = InStrandsData.StrandsCurves.CurvesRootUV[CurveIndex];
		RootUV.Y = FMath::Clamp(1.f - RootUV.Y, 0.f, 1.f);

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
	FGlobalShaderMap* ShaderMap,
	bool bCopyDataBackToCPU,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const EPixelFormat Format,
	const uint32 InKernelSizeInPixels,
	const TArray<FRDGBufferRef>& InRootUVBuffers_R,
	const TArray<FRDGBufferRef>& InRootUVBuffers_G,
	const TArray<FRDGBufferRef>& InRootUVBuffers_B,
	const TArray<FRDGBufferRef>& InRootUVBuffers_A,
	UTexture2D*	OutTexture)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;

	if (OutTexture == nullptr || !OutTexture->GetResource() || !OutTexture->GetResource()->GetTexture2DRHI() ||
		(InRootUVBuffers_R.Num() == 0 &&
		 InRootUVBuffers_G.Num() == 0 &&
		 InRootUVBuffers_B.Num() == 0 &&
		 InRootUVBuffers_A.Num() == 0))
	{
		return;
	}
	check(Resolution.X == Resolution.Y);

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

	// Select if the generated texture should be copy back to a CPU texture for being saved, or directly used
	AddReadbackTexturePass(
		GraphBuilder,
		RDG_EVENT_NAME("CopyRDGToTexture2D"),
		FollicleMaskTexture,
		[FollicleMaskTexture, MipCount, OutTexture](FRHICommandListImmediate& RHICmdList)
	{
		if (OutTexture && OutTexture->GetResource() && OutTexture->GetResource()->GetTexture2DRHI())
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.NumMips = MipCount;
			RHICmdList.CopyTexture(
				FollicleMaskTexture->GetRHI(),
				OutTexture->GetResource()->GetTexture2DRHI(),
				CopyInfo);
		}
	});
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

		uint32 GroupIndex = 0;
		for (const FHairGroupData& HairGroupData : Info.GroomAsset->HairGroupsData)
		{
			FHairStrandsDatas StrandsData;
			FHairStrandsDatas GuidesData;
			Info.GroomAsset->GetHairStrandsDatas(GroupIndex, StrandsData, GuidesData);

			RasterToTexture(Resolution.X, Info.KernelSizeInPixels, Channel, StrandsData, Pixels);
			++GroupIndex;
		}
	}
	OutTexture->Source.UnlockMip(0);
	OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
	OutTexture->MarkPackageDirty();
#endif // #if WITH_EDITORONLY_DATA
}

struct FFollicleInfoGPU
{
	FHairStrandsDatas*		StrandsData = nullptr;
	FFollicleInfo::EChannel	Channel = FFollicleInfo::R;
	uint32					KernelSizeInPixels = 0;
	bool					bGPUOnly = false;
};

// GPU path
static void InternalBuildFollicleTexture_GPU(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const TArray<FFollicleInfoGPU>& InInfos,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const EPixelFormat InFormat,
	UTexture2D*	OutTexture)
{
	uint32 KernelSizeInPixels = ~0;
	TArray<FRDGBufferRef> RootUVBuffers[4];
	bool bCopyDataBackToCPU = false;

	for (const FFollicleInfoGPU& Info : InInfos)
	{
		if (KernelSizeInPixels == ~0)
		{
			KernelSizeInPixels = Info.KernelSizeInPixels;
			bCopyDataBackToCPU = !Info.bGPUOnly;
		}

		// Create root UVs buffer
		if (Info.StrandsData)
		{
			const uint32 DataCount = Info.StrandsData->StrandsCurves.CurvesRootUV.Num();
			const uint32 DataSizeInBytes = sizeof(FVector2f) * DataCount;
			check(DataSizeInBytes != 0);

			const FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), DataCount);
			FRDGBufferRef RootBuffer = CreateVertexBuffer(
				GraphBuilder,
				TEXT("RootUVBuffer"),
				Desc,
				Info.StrandsData->StrandsCurves.CurvesRootUV.GetData(),
				DataSizeInBytes,
				ERDGInitialDataFlags::None);
			RootUVBuffers[Info.Channel].Add(RootBuffer);
		}
	}

	const EPixelFormat Format = bCopyDataBackToCPU ? PF_B8G8R8A8 : PF_R8G8B8A8;		 
	InternalGenerateFollicleTexture_GPU(GraphBuilder, ShaderMap, bCopyDataBackToCPU, Resolution, MipCount, Format, KernelSizeInPixels, RootUVBuffers[0], RootUVBuffers[1], RootUVBuffers[2], RootUVBuffers[3], OutTexture);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Asynchronous queuing for follicle texture mask generation
struct FFollicleQuery
{
	TArray<FFollicleInfoGPU> Infos;
	FIntPoint Resolution = 0;
	uint32 MipCount = 0;
	EPixelFormat Format = PF_R8G8B8A8;
	UTexture2D*	OutTexture = nullptr;

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
		if (Q.Infos.Num() > 0 && Q.MipCount>0 && Q.Resolution.X > 0 && Q.Resolution.Y > 0)
		{
			InternalBuildFollicleTexture_GPU(GraphBuilder, ShaderMap, Q.Infos, Q.Resolution, Q.MipCount, Q.Format, Q.OutTexture);

			// Release all strands data
			for (FFollicleInfoGPU& Info : Q.Infos)
			{
				if (Info.StrandsData)
				{
					delete Info.StrandsData;
					Info.StrandsData = nullptr;
				}
			}
		}
	}
}

void FGroomTextureBuilder::BuildFollicleTexture(const TArray<FFollicleInfo>& InCPUInfos, UTexture2D* OutTexture, bool bUseGPU)
{
	if (!OutTexture || InCPUInfos.Num() == 0)
	{
		UE_LOG(LogGroomTextureBuilder, Warning, TEXT("[Groom] Error - Follicle texture can't be created/rebuilt."));
		return;
	}

	if (bUseGPU)
	{
		// Asynchronous (GPU)
		const FIntPoint Resolution(OutTexture->GetSizeX(), OutTexture->GetSizeY());
		const uint32 MipCount = OutTexture->GetNumMips();
		if (MipCount > 0 && Resolution.X > 0 && Resolution.Y > 0)
		{
			TArray<FFollicleInfoGPU> GPUInfos;
			for (const FFollicleInfo& CPUInfo : InCPUInfos)
			{
				if (!CPUInfo.GroomAsset || CPUInfo.GroomAsset->GetNumHairGroups() == 0)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] Error - Groom follicle texture can be entirely created/rebuilt as some groom assets seems invalid."));
					continue;
				}

				for (uint32 GroupIndex = 0, GroupCount = CPUInfo.GroomAsset->GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
				{
					FHairStrandsDatas* StrandsData = new FHairStrandsDatas();
					FHairStrandsDatas GuidesData;
				#if WITH_EDITORONLY_DATA
					CPUInfo.GroomAsset->GetHairStrandsDatas(GroupIndex, *StrandsData, GuidesData);
				#endif

					if (StrandsData->StrandsCurves.CurvesRootUV.Num())
					{
						FFollicleInfoGPU& GPUInfo = GPUInfos.AddDefaulted_GetRef();
						GPUInfo.Channel = CPUInfo.Channel;
						GPUInfo.KernelSizeInPixels = CPUInfo.KernelSizeInPixels;
						GPUInfo.StrandsData = StrandsData;
						GPUInfo.bGPUOnly = CPUInfo.bGPUOnly;
					}
					else
					{
						delete StrandsData;
					}
				}
			}

			const EPixelFormat Format = OutTexture->GetPixelFormat(0);
			ENQUEUE_RENDER_COMMAND(FFollicleTextureQuery)(
			[Resolution, MipCount, Format, GPUInfos, OutTexture](FRHICommandListImmediate& RHICmdList)
			{
				if (OutTexture->GetResource())
				{
					GFollicleQueries.Enqueue({ GPUInfos, Resolution, MipCount, Format, OutTexture });
				}
			});
		}
	}
	else
	{
		// Synchronous (CPU)
		InternalBuildFollicleTexture_CPU(InCPUInfos, OutTexture);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Strands texture generation


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairStrandTextureDilationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandTextureDilationCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandTextureDilationCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, Resolution)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source_DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source_CoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source_TangentTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source_AttributeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source_MaterialTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TriangleMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Target_DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Target_CoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Target_TangentTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Target_AttributeTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Target_MaterialTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TEXTURE_DILATION"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandTextureDilationCS, "/Engine/Private/HairStrands/HairStrandsTexturesGeneration.usf", "MainCS", SF_Compute);

static void AddTextureDilationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntPoint& Resolution,
	FRDGTextureRef TriangleMaskTexture,

	FRDGTextureRef Source_DepthTexture,
	FRDGTextureRef Source_CoverageTexture,
	FRDGTextureRef Source_TangentTexture,
	FRDGTextureRef Source_AttributeTexture,
	FRDGTextureRef Source_MaterialTexture,

	FRDGTextureRef Target_DepthTexture,
	FRDGTextureRef Target_CoverageTexture,
	FRDGTextureRef Target_TangentTexture,
	FRDGTextureRef Target_AttributeTexture,
	FRDGTextureRef Target_MaterialTexture)
{
	FHairStrandTextureDilationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandTextureDilationCS::FParameters>();
	Parameters->Resolution				= Resolution;
	Parameters->TriangleMaskTexture		= GraphBuilder.CreateUAV(TriangleMaskTexture);
	Parameters->Source_DepthTexture		= Source_DepthTexture;
	Parameters->Source_CoverageTexture	= Source_CoverageTexture;
	Parameters->Source_TangentTexture	= Source_TangentTexture;
	Parameters->Source_AttributeTexture = Source_AttributeTexture;
	Parameters->Source_MaterialTexture  = Source_MaterialTexture;
	Parameters->Target_DepthTexture		= GraphBuilder.CreateUAV(Target_DepthTexture);
	Parameters->Target_CoverageTexture	= GraphBuilder.CreateUAV(Target_CoverageTexture);
	Parameters->Target_TangentTexture	= GraphBuilder.CreateUAV(Target_TangentTexture);
	Parameters->Target_AttributeTexture = GraphBuilder.CreateUAV(Target_AttributeTexture);
	Parameters->Target_MaterialTexture  = GraphBuilder.CreateUAV(Target_MaterialTexture);

	TShaderMapRef<FHairStrandTextureDilationCS> ComputeShader(ShaderMap);
	FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(Resolution, FIntPoint(8, 4));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTetureDilation"),
		ComputeShader,
		Parameters,
		DispatchCount);
}


static void InternalAllocateStrandsTexture(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount, EPixelFormat Format, ETextureSourceFormat SourceFormat)
{
	FTextureFormatSettings FormatSettings;
	FormatSettings.SRGB = false;
	FormatSettings.CompressionNone = true;
	FormatSettings.CompressionSettings = TC_Masks;
#if WITH_EDITORONLY_DATA
	Out->Source.Init(Resolution.X, Resolution.Y, 1, MipCount, SourceFormat, nullptr);
	Out->SetLayerFormatSettings(0, FormatSettings);
#endif // #if WITH_EDITORONLY_DATA

	Out->SetPlatformData(new FTexturePlatformData());
	Out->GetPlatformData()->SizeX = Resolution.X;
	Out->GetPlatformData()->SizeY = Resolution.Y;
	Out->GetPlatformData()->PixelFormat = Format;

	//Out->UpdateResource();
}

static void InternalAllocateStrandsTexture_Depth(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_G16, ETextureSourceFormat::TSF_G16);
}

static void InternalAllocateStrandsTexture_Coverage(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

static void InternalAllocateStrandsTexture_Tangent(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

static void InternalAllocateStrandsTexture_Attribute(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

static void InternalAllocateStrandsTexture_Material(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount)
{
	InternalAllocateStrandsTexture(Out, Resolution, 1, PF_B8G8R8A8, ETextureSourceFormat::TSF_BGRA8);
}

#if WITH_EDITOR
FStrandsTexturesOutput FGroomTextureBuilder::CreateGroomStrandsTexturesTexture(const UGroomAsset* GroomAsset, uint32 InResolution)
{
	FStrandsTexturesOutput Output;

	if (!GroomAsset)
	{
		return Output;
	}

	const FString PackageName = GroomAsset->GetOutermost()->GetName();
	const FIntPoint Resolution(InResolution, InResolution);
	Output.Depth = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_Depth"), InternalAllocateStrandsTexture_Depth);
	Output.Coverage = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_Opacity"), InternalAllocateStrandsTexture_Coverage);
	Output.Tangent = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_Tangent"), InternalAllocateStrandsTexture_Tangent);
	Output.Attribute = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_Attribute"), InternalAllocateStrandsTexture_Attribute);
	Output.Material = FHairStrandsCore::CreateTexture(PackageName, Resolution, TEXT("_Material"), InternalAllocateStrandsTexture_Material);
	return Output;
}
#endif

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

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
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
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderParameters, ShaderDrawParameters)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(float, MaxDistance)
		SHADER_PARAMETER(int32, TracingDirection)
		SHADER_PARAMETER(uint32, bHasMaterialData)

		SHADER_PARAMETER(uint32, UVsChannelIndex)
		SHADER_PARAMETER(uint32, UVsChannelCount)

		SHADER_PARAMETER(float, InVF_Radius)
		SHADER_PARAMETER(float, InVF_Length)
		SHADER_PARAMETER(FVector3f, InVF_PositionOffset)
		SHADER_PARAMETER_SRV(Buffer, InVF_PositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, InVF_Attribute0Buffer)
		SHADER_PARAMETER_SRV(Buffer, InVF_MaterialBuffer)
		SHADER_PARAMETER(uint32, InVF_ControlPointCount)
		SHADER_PARAMETER(uint32, InVF_GroupIndex)

		SHADER_PARAMETER(FVector3f, Voxel_MinBound)
		SHADER_PARAMETER(FVector3f, Voxel_MaxBound)
		SHADER_PARAMETER(FIntVector, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_Size)
		SHADER_PARAMETER(uint32, Voxel_MaxSegmentPerVoxel)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Voxel_OffsetAndCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Voxel_Data)

		SHADER_PARAMETER_SRV(Buffer, VertexBuffer)
		SHADER_PARAMETER_SRV(Buffer, UVsBuffer)
		SHADER_PARAMETER_SRV(Buffer, NormalsBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
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
	FGlobalShaderMap* ShaderMap,
	const FShaderDrawDebugData* ShaderDrawData,
	const bool bClear,
	const float InMaxDistance, 
	const int32 InTracingDirection,

	const uint32 VertexCount,
	const uint32 PrimitiveCount,

	const uint32 VertexBaseIndex,
	const uint32 IndexBaseIndex,

	const uint32 UVsChannelIndex,
	const uint32 UVsChannelCount,

	FRHIBuffer* InMeshIndexBuffer,
	FRHIShaderResourceView* InMeshVertexBuffer,
	FRHIShaderResourceView* InMeshUVsBuffer,
	FRHIShaderResourceView* InMeshNormalsBuffer,

	const FVector& VoxelMinBound,
	const FVector& VoxelMaxBound,
	const FIntVector& VoxelResolution,
	float VoxelSize,
	uint32 VoxelMaxSegmentPerVoxel,
	FRDGBufferRef VoxelOffsetAndCount,
	FRDGBufferRef VoxelData,
	
	FRHIShaderResourceView* InHairStrands_PositionBuffer,
	FRHIShaderResourceView* InHairStrands_AttributeBuffer,
	FRHIShaderResourceView* InHairStrands_MaterialBuffer,
	const FVector& InHairStrands_PositionOffset,
	float InHairStrands_Radius,
	float InHairStrands_Length,
	uint32 InHairStrands_ControlPointCount,
	uint32 InHairStrands_GroupIndex,

	FRDGTextureRef OutDepthTestTexture,
	FRDGTextureRef OutDepthTexture,
	FRDGTextureRef OutTangentTexture,
	FRDGTextureRef OutCoverageTexture,
	FRDGTextureRef OutRootUVStrandsUSeedTexture,
	FRDGTextureRef OutMaterialTexture,
	FRDGTextureRef OutTriangleMask)
{
	const FIntPoint OutputResolution = OutDepthTexture->Desc.Extent;
	const bool bHasMaterialData = InHairStrands_MaterialBuffer != nullptr;

	FHairStrandsTexturePS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FHairStrandsTexturePS::FParameters>();
	ParametersPS->OutputResolution = OutputResolution;
	ParametersPS->VertexCount = VertexCount;
	ParametersPS->VertexBuffer = InMeshVertexBuffer;
	ParametersPS->UVsBuffer = InMeshUVsBuffer;
	ParametersPS->NormalsBuffer = InMeshNormalsBuffer;
	ParametersPS->MaxDistance = FMath::Abs(InMaxDistance);
	ParametersPS->TracingDirection = InTracingDirection;
	ParametersPS->bHasMaterialData = bHasMaterialData ? 1u : 0u;

	ParametersPS->UVsChannelIndex = UVsChannelIndex;
	ParametersPS->UVsChannelCount = UVsChannelCount;

	ParametersPS->InVF_PositionBuffer = InHairStrands_PositionBuffer;
	ParametersPS->InVF_PositionOffset = (FVector3f)InHairStrands_PositionOffset;
	ParametersPS->InVF_Attribute0Buffer = InHairStrands_AttributeBuffer;
	ParametersPS->InVF_MaterialBuffer = bHasMaterialData ? InHairStrands_MaterialBuffer : InHairStrands_AttributeBuffer;
	ParametersPS->InVF_Radius = InHairStrands_Radius;
	ParametersPS->InVF_Length = InHairStrands_Length;
	ParametersPS->InVF_ControlPointCount = InHairStrands_ControlPointCount;
	ParametersPS->InVF_GroupIndex = InHairStrands_GroupIndex;

	ParametersPS->Voxel_MinBound = (FVector3f)VoxelMinBound;
	ParametersPS->Voxel_MaxBound = (FVector3f)VoxelMaxBound;
	ParametersPS->Voxel_Resolution = VoxelResolution;
	ParametersPS->Voxel_Size = VoxelSize;
	ParametersPS->Voxel_MaxSegmentPerVoxel = VoxelMaxSegmentPerVoxel;
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
	ParametersPS->RenderTargets[4] = FRenderTargetBinding(OutMaterialTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
	ParametersPS->RenderTargets[5] = FRenderTargetBinding(OutTriangleMask, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
	ParametersPS->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTestTexture,
		bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

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
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero> ::GetRHI();

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_LessEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			
			// Divide the rendering work into small batches to reduce risk of TDR as the texture projection implies heavy works 
			// (i.e. long thread running due the the large amount of strands a groom can have)
			const int32 TileSize = 1024;
			if (OutputResolution.X < TileSize)
			{
				RHICmdList.DrawIndexedPrimitive(InMeshIndexBuffer, VertexBaseIndex, 0, VertexCount, IndexBaseIndex, PrimitiveCount, 1);

				// Flush, to ensure that all texture generation is done (TDR)
				#if 0
				GDynamicRHI->RHISubmitCommandsAndFlushGPU();
				GDynamicRHI->RHIBlockUntilGPUIdle();
				#endif
			}
			else
			{
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
					#if 0
					GDynamicRHI->RHISubmitCommandsAndFlushGPU();
					GDynamicRHI->RHIBlockUntilGPUIdle();
					#endif
				}
			}
		});
}

static void AddReadbackPass(
	FRDGBuilder& GraphBuilder,
	uint32 BytePerPixel,
	FRDGTextureRef InputTexture,
	UTexture2D* OutTexture)
{
	AddReadbackTexturePass(
		GraphBuilder,
		RDG_EVENT_NAME("CopyRDGToTexture2D"),
		InputTexture,
	[InputTexture, OutTexture, BytePerPixel](FRHICommandListImmediate& RHICmdList)
	{
		const FIntPoint Resolution = InputTexture->Desc.Extent;
		check(OutTexture);

		FRHIResourceCreateInfo CreateInfo(TEXT("ReadbackPass_StagingTexture"));
		FTexture2DRHIRef StagingTexture = RHICreateTexture2D(
			InputTexture->Desc.Extent.X,
			InputTexture->Desc.Extent.Y,
			InputTexture->Desc.Format,
			InputTexture->Desc.NumMips,
			1, TexCreate_CPUReadback, CreateInfo);

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.NumMips = InputTexture->Desc.NumMips;
		RHICmdList.CopyTexture(
			InputTexture->GetRHI(),
			StagingTexture->GetTexture2D(),
			CopyInfo);

		// Flush, to ensure that all texture generation is done
		GDynamicRHI->RHISubmitCommandsAndFlushGPU();
		GDynamicRHI->RHIBlockUntilGPUIdle();

		// don't think we can mark package as a dirty in the package build
	#if WITH_EDITORONLY_DATA
		check(OutTexture->Source.GetSizeX() == Resolution.X);
		check(OutTexture->Source.GetSizeY() == Resolution.Y);

		void* InData = nullptr;
		int32 Width = 0, Height = 0;
		RHICmdList.MapStagingSurface(StagingTexture, InData, Width, Height);
		uint8* InDataRGBA8 = (uint8*)InData;
		ETextureSourceFormat SrcFormat = OutTexture->Source.GetFormat();
		OutTexture->Source.Init(Width, Height, 1, 1, SrcFormat, InDataRGBA8);
		RHICmdList.UnmapStagingSurface(StagingTexture);

		OutTexture->DeferCompression = true; // This forces reloading data when the asset is saved
		OutTexture->MarkPackageDirty();
	#endif // WITH_EDITORONLY_DATA
	});
}

static void InternalBuildStrandsTextures_GPU(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
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

	const FIntPoint OutputResolution(
		FMath::Clamp(InInfo.Resolution, 512u, 16384u),
		FMath::Clamp(InInfo.Resolution, 512u, 16384u));

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(OutputResolution, PF_A8R8G8B8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource);

	Desc.Format = PF_G16;
	Desc.ClearValue = FClearValueBinding::White;
	FRDGTextureRef DepthTexture[2];
	DepthTexture[0] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.DepthTexture"));
	DepthTexture[1] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.DepthTexture"));

	Desc.Format = PF_B8G8R8A8;
	Desc.ClearValue = FClearValueBinding::Transparent;
	FRDGTextureRef CoverageTexture[2];
	CoverageTexture[0] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.CoverageTexture"));
	CoverageTexture[1] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.CoverageTexture"));

	Desc.Format = PF_B8G8R8A8;
	Desc.ClearValue = FClearValueBinding::Transparent;
	FRDGTextureRef TangentTexture[2];
	TangentTexture[0] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.TangentTexture"));
	TangentTexture[1] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.TangentTexture"));

	Desc.Format = PF_B8G8R8A8;
	Desc.ClearValue = FClearValueBinding::Transparent;
	FRDGTextureRef AttributeTexture[2];
	AttributeTexture[0] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.AttributeTexture"));
	AttributeTexture[1] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.AttributeTexture"));

	Desc.Format = PF_B8G8R8A8;
	Desc.ClearValue = FClearValueBinding::Transparent;
	FRDGTextureRef MaterialTexture[2];
	MaterialTexture[0] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.MaterialTexture"));
	MaterialTexture[1] = GraphBuilder.CreateTexture(Desc, TEXT("Hair.MaterialTexture"));

	Desc.Format = PF_R32_UINT;
	Desc.ClearValue = FClearValueBinding::Black;
	FRDGTextureRef TriangleMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.TriangleMaskTexture"));
	
	Desc.ClearValue = FClearValueBinding(1);
	Desc.Format = PF_DepthStencil;
	Desc.Flags = TexCreate_DepthStencilTargetable;
	FRDGTextureRef DepthTestTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.DepthTestTexture"));

	bool bClear = true;
	const uint32 GroupCount = InInfo.GroomAsset->GetNumHairGroups();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		if (!InInfo.GroupIndices.Contains(GroupIndex))
		{
			continue;
		}

		FHairGroupData& GroupData = InInfo.GroomAsset->HairGroupsData[GroupIndex];
		FHairGroupsRendering& RenderingData = InInfo.GroomAsset->HairGroupsRendering[GroupIndex];

		FRDGBufferRef VoxelOffsetAndCount = GraphBuilder.RegisterExternalBuffer(GroupData.Debug.Resource->VoxelOffsetAndCount);
		FRDGBufferRef VoxelData = GraphBuilder.RegisterExternalBuffer(GroupData.Debug.Resource->VoxelData);

		{
			FRHIShaderResourceView* PositionBuffer = nullptr;
			FRHIShaderResourceView* UVsBuffer = nullptr;
			FRHIShaderResourceView* TangentBuffer = nullptr;
			FBufferRHIRef IndexBuffer = nullptr;
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
				const uint32 MeshLODIndex = FMath::Clamp(InInfo.LODIndex, 0u, uint32(RenderData->LODRenderData.Num()));

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
				const uint32 MeshLODIndex = FMath::Clamp(InInfo.LODIndex, 0u, uint32(StaticMesh->GetNumLODs()));

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

			// Ensure the rest resources are loaded when rendering the strands textures
			GroupData.Strands.RestResource->Allocate(GraphBuilder, EHairResourceLoadingType::Sync);

			InternalGenerateHairStrandsTextures(
				GraphBuilder,
				ShaderMap,
				DebugShaderData,
				bClear,
				InInfo.MaxTracingDistance,
				InInfo.TracingDirection,

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
				GroupData.Debug.Resource->VoxelDescription.MaxSegmentPerVoxel,
				VoxelOffsetAndCount,
				VoxelData,

				GroupData.Strands.RestResource->PositionBuffer.SRV,
				GroupData.Strands.RestResource->Attribute0Buffer.SRV,
				GroupData.Strands.RestResource->MaterialBuffer.SRV,
				GroupData.Strands.RestResource->GetPositionOffset(),
				RenderingData.GeometrySettings.HairWidth * 0.5f,
				GroupData.Strands.RestResource->BulkData.MaxLength,
				GroupData.Strands.RestResource->BulkData.PointCount,
				GroupIndex,

				DepthTestTexture,
				DepthTexture[0],
				TangentTexture[0],
				CoverageTexture[0],
				AttributeTexture[0],
				MaterialTexture[0],
				TriangleMaskTexture);

			bClear = false;
		}
	}

	uint32 SourceIndex = 0;
	uint32 TargetIndex = 0;
	const uint32 DilationPassCount = FMath::Max(0, GHairStrandsTextureDilationPassCount);
	for (uint32 DilationIt=0; DilationIt< DilationPassCount; ++DilationIt)
	{
		TargetIndex = (SourceIndex + 1) % 2;

		AddTextureDilationPass(
			GraphBuilder,
			ShaderMap,
			OutputResolution,
			TriangleMaskTexture,

			DepthTexture[SourceIndex],
			CoverageTexture[SourceIndex],
			TangentTexture[SourceIndex],
			AttributeTexture[SourceIndex],
			MaterialTexture[SourceIndex],

			DepthTexture[TargetIndex],
			CoverageTexture[TargetIndex],
			TangentTexture[TargetIndex],
			AttributeTexture[TargetIndex],
			MaterialTexture[TargetIndex]);

		SourceIndex = TargetIndex;
	}

	AddReadbackPass(GraphBuilder, 2, DepthTexture[TargetIndex]		, Output.Depth);
	AddReadbackPass(GraphBuilder, 4, CoverageTexture[TargetIndex]	, Output.Coverage);
	AddReadbackPass(GraphBuilder, 4, TangentTexture[TargetIndex]	, Output.Tangent);
	AddReadbackPass(GraphBuilder, 4, AttributeTexture[TargetIndex]	, Output.Attribute);
	AddReadbackPass(GraphBuilder, 4, MaterialTexture[TargetIndex]	, Output.Material);
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
		InternalBuildStrandsTextures_GPU(GraphBuilder, ShaderMap, Q.Info, Q.Output, DebugShaderData);
	}
}

#if WITH_EDITOR
void FGroomTextureBuilder::BuildStrandsTextures(const FStrandsTexturesInfo& InInfo, const FStrandsTexturesOutput& Output)
{
	GStrandsTexturesQueries.Enqueue({ InInfo, Output });
}
#endif

#undef LOCTEXT_NAMESPACE

