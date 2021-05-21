// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudRenderBuffers.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "MeshBatch.h"
#include "RenderCommandFence.h"
#include "LidarPointCloudOctree.h"
#include "LidarPointCloudSettings.h"
#include "MeshMaterialShader.h"

#if WITH_EDITOR
#include "Classes/EditorStyleSettings.h"
#endif

#define BINDPARAM(Name) Name.Bind(ParameterMap, TEXT(#Name))
#define SETPARAM(Name) if (Name.IsBound()) { ShaderBindings.Add(Name, UserData->Name); }
#define SETSRVPARAM(Name) if(UserData->Name) { SETPARAM(Name) }

//////////////////////////////////////////////////////////// Base Buffer

TGlobalResource<FLidarPointCloudIndexBuffer> GLidarPointCloudIndexBuffer;
TGlobalResource<FLidarPointCloudSharedVertexFactory> GLidarPointCloudSharedVertexFactory;

//////////////////////////////////////////////////////////// Index Buffer

void FLidarPointCloudIndexBuffer::Resize(const uint32 & RequestedCapacity)
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	if (Capacity != RequestedCapacity)
	{
		ReleaseResource();
		Capacity = RequestedCapacity;
		InitResource();
	}
}

void FLidarPointCloudIndexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	const uint32 Size = Capacity * 7 * sizeof(uint32);
	PointOffset = Capacity * 6;

	IndexBufferRHI = RHICreateAndLockIndexBuffer(sizeof(uint32), Size, BUF_Dynamic, CreateInfo, Buffer);

	uint32* Data = (uint32*)Buffer;
	for (uint32 i = 0, idx = 0; i < Capacity; i++)
	{
		const uint32 v = i * 4;

		// Full quads
		Data[idx++] = v;
		Data[idx++] = v + 1;
		Data[idx++] = v + 2;
		Data[idx++] = v;
		Data[idx++] = v + 2;
		Data[idx++] = v + 3;

		// Points
		Data[PointOffset + i] = v;
	}

	RHIUnlockIndexBuffer(IndexBufferRHI);
	Buffer = nullptr;
}

//////////////////////////////////////////////////////////// Structured Buffer

void FLidarPointCloudRenderBuffer::Resize(const uint32& RequestedCapacity)
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	if (Capacity != RequestedCapacity)
	{
		ReleaseResource();
		Capacity = RequestedCapacity;
		InitResource();
	}
	else if (!IsInitialized())
	{
		InitResource();
	}
}

void FLidarPointCloudRenderBuffer::InitRHI()
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	FRHIResourceCreateInfo CreateInfo;
	Buffer = RHICreateVertexBuffer(sizeof(uint32) * Capacity, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
	SRV = RHICreateShaderResourceView(Buffer, sizeof(uint32), PF_R32_FLOAT);
}

void FLidarPointCloudRenderBuffer::ReleaseRHI()
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	if (Buffer)
	{
		RHIDiscardTransientResource(Buffer);
		Buffer.SafeRelease();
	}

	SRV.SafeRelease();
}

//////////////////////////////////////////////////////////// User Data

FLidarPointCloudBatchElementUserData::FLidarPointCloudBatchElementUserData()
	: SelectionColor(FVector::OneVector)
	, NumClippingVolumes(0)
	, bStartClipped(false)
{
	for (int32 i = 0; i < 16; ++i)
	{
		ClippingVolume[i] = FMatrix(FPlane(FVector::ZeroVector, 0),
									FPlane(FVector::ForwardVector, FLT_MAX),
									FPlane(FVector::RightVector, FLT_MAX),
									FPlane(FVector::UpVector, FLT_MAX));
	}

#if WITH_EDITOR
	SelectionColor = FVector(GetDefault<UEditorStyleSettings>()->SelectionColor.ToFColor(true));
#endif
}

void FLidarPointCloudBatchElementUserData::SetClassificationColors(const TMap<int32, FLinearColor>& InClassificationColors)
{
	for (int32 i = 0; i < 32; ++i)
	{
		const FLinearColor* Color = InClassificationColors.Find(i);
		ClassificationColors[i] = Color ? FVector4(*Color) : FVector4(1, 1, 1);
	}
}

//////////////////////////////////////////////////////////// Vertex Factory

void FLidarPointCloudVertexFactoryShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	BINDPARAM(TreeBuffer);
	BINDPARAM(DataBuffer);
	BINDPARAM(bEditorView);
	BINDPARAM(SelectionColor);
	BINDPARAM(LocationOffset);
	BINDPARAM(RootCellSize);
	BINDPARAM(RootExtent);
	BINDPARAM(bUsePerPointScaling);
	BINDPARAM(VirtualDepth);
	BINDPARAM(SpriteSizeMultiplier);
	BINDPARAM(ReversedVirtualDepthMultiplier);
	BINDPARAM(ViewRightVector);
	BINDPARAM(ViewUpVector);
	BINDPARAM(bUseCameraFacing);
	BINDPARAM(bUseScreenSizeScaling);
	BINDPARAM(bUseStaticBuffers);
	BINDPARAM(BoundsSize);
	BINDPARAM(ElevationColorBottom);
	BINDPARAM(ElevationColorTop);
	BINDPARAM(bUseCircle);
	BINDPARAM(bUseColorOverride);
	BINDPARAM(bUseElevationColor);
	BINDPARAM(Offset);
	BINDPARAM(Contrast);
	BINDPARAM(Saturation);
	BINDPARAM(Gamma);
	BINDPARAM(Tint);
	BINDPARAM(IntensityInfluence);
	BINDPARAM(bUseClassification);
	BINDPARAM(ClassificationColors);
	BINDPARAM(ClippingVolume);
	BINDPARAM(NumClippingVolumes);
	BINDPARAM(bStartClipped);
}

void FLidarPointCloudVertexFactoryShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement, class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
{
	FLidarPointCloudBatchElementUserData* UserData = (FLidarPointCloudBatchElementUserData*)BatchElement.UserData;

	SETSRVPARAM(TreeBuffer);
	SETSRVPARAM(DataBuffer);
	SETPARAM(bEditorView);
	SETPARAM(SelectionColor);
	SETPARAM(LocationOffset);
	SETPARAM(RootCellSize);
	SETPARAM(RootExtent);
	SETPARAM(bUsePerPointScaling);
	SETPARAM(VirtualDepth);
	SETPARAM(SpriteSizeMultiplier);
	SETPARAM(ReversedVirtualDepthMultiplier);
	SETPARAM(ViewRightVector);
	SETPARAM(ViewUpVector);
	SETPARAM(bUseCameraFacing);
	SETPARAM(bUseScreenSizeScaling);
	SETPARAM(bUseStaticBuffers);
	SETPARAM(BoundsSize);
	SETPARAM(ElevationColorBottom);
	SETPARAM(ElevationColorTop);
	SETPARAM(bUseCircle);
	SETPARAM(bUseColorOverride);
	SETPARAM(bUseElevationColor);
	SETPARAM(Offset);
	SETPARAM(Contrast);
	SETPARAM(Saturation);
	SETPARAM(Gamma);
	SETPARAM(Tint);
	SETPARAM(IntensityInfluence);
	SETPARAM(bUseClassification);
	SETPARAM(ClassificationColors);
	SETPARAM(ClippingVolume);
	SETPARAM(NumClippingVolumes);
	SETPARAM(bStartClipped);
}

bool FLidarPointCloudVertexFactoryBase::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (IsPCPlatform(Parameters.Platform) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
		Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithLidarPointCloud) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FLidarPointCloudVertexFactory::Initialize(FLidarPointCloudPoint* Data, int32 NumPoints)
{
	if (IsInitialized())
	{
		ReleaseResource();
	}

	VertexBuffer.Data = Data;
	VertexBuffer.NumPoints = NumPoints;

	InitResource();
}

void FLidarPointCloudVertexFactory::FPointCloudVertexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(NumPoints * 4 * sizeof(FLidarPointCloudPoint), BUF_Static, CreateInfo, Buffer);

	uint8* Dest = (uint8*)Buffer;
	for (int32 i = 0; i < NumPoints; ++i, ++Data)
	{
		FMemory::Memcpy(Dest, Data, sizeof(FLidarPointCloudPoint)); Dest += sizeof(FLidarPointCloudPoint);
		FMemory::Memcpy(Dest, Data, sizeof(FLidarPointCloudPoint)); Dest += sizeof(FLidarPointCloudPoint);
		FMemory::Memcpy(Dest, Data, sizeof(FLidarPointCloudPoint)); Dest += sizeof(FLidarPointCloudPoint);
		FMemory::Memcpy(Dest, Data, sizeof(FLidarPointCloudPoint)); Dest += sizeof(FLidarPointCloudPoint);
	}

	RHIUnlockVertexBuffer(VertexBufferRHI);
	Buffer = nullptr;
}

void FLidarPointCloudVertexFactory::InitRHI()
{
	VertexBuffer.InitResource();

	FVertexDeclarationElementList Elements;
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 0, sizeof(FLidarPointCloudPoint), VET_Float3), 0));
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 12, sizeof(FLidarPointCloudPoint), VET_Color), 1));
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 16, sizeof(FLidarPointCloudPoint), VET_UInt), 2));
	InitDeclaration(Elements);
}

void FLidarPointCloudVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
	VertexBuffer.ReleaseResource();
}

void FLidarPointCloudSharedVertexFactory::FPointCloudVertexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(sizeof(FVector), BUF_Static, CreateInfo, Buffer);
	FMemory::Memzero(Buffer, sizeof(FVector));
	RHIUnlockVertexBuffer(VertexBufferRHI);
	Buffer = nullptr;
}

void FLidarPointCloudSharedVertexFactory::InitRHI()
{
	VertexBuffer.InitResource();

	FVertexDeclarationElementList Elements;
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 0, 0, VET_Float3), 0));
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 0, 0, VET_Color), 1));
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 0, 0, VET_Color), 2));
	InitDeclaration(Elements);
}

void FLidarPointCloudSharedVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
	VertexBuffer.ReleaseResource();
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLidarPointCloudVertexFactoryBase, SF_Vertex, FLidarPointCloudVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FLidarPointCloudVertexFactoryBase, "/Plugin/LidarPointCloud/Private/LidarPointCloudVertexFactory.ush", /* bUsedWithMaterials */ true, /* bSupportsStaticLighting */ false, /* bSupportsDynamicLighting */ true, /* bPrecisePrevWorldPos */ false, /* bSupportsPositionOnly */ true);

#undef BINDPARAM
#undef SETPARAM
#undef SETSRVPARAM
