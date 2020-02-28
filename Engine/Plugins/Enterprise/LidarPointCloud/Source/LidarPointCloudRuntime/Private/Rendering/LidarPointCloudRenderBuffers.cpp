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

void FLidarPointCloudBuffer::Resize(const uint32& RequestedCapacity)
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	if (Capacity < RequestedCapacity)
	{
		// Apply some slack to limit number of rebuilds
		uint32 NewCapacity = Slack > 0 ? RequestedCapacity * (1 + Slack) : RequestedCapacity;

		if (GetDefault<ULidarPointCloudSettings>()->bLogBufferExpansion)
		{
			PC_LOG("Resizing %s: %u => %u", *GetFriendlyName(), Capacity, NewCapacity);
		}

		Release();
		Capacity = NewCapacity;
		Initialize();
	}
}

//////////////////////////////////////////////////////////// Index Buffer

FLidarPointCloudIndexBuffer::~FLidarPointCloudIndexBuffer()
{
	FRenderCommandFence Fence;

	ENQUEUE_RENDER_COMMAND(ReleasePointCloudIndexBuffer)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Release();
		});

	Fence.BeginFence();
	Fence.Wait();
}

void FLidarPointCloudIndexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	uint32 Size = Capacity * 7 * sizeof(uint32);
	PointOffset = Capacity * 6;

	IndexBufferRHI = RHICreateAndLockIndexBuffer(sizeof(uint32), Size, BUF_Dynamic, CreateInfo, Buffer);

	uint32* Data = (uint32*)Buffer;
	for (uint32 i = 0; i < Capacity; i++)
	{
		// Full quads
		{
			uint32 idx = i * 6;
			uint32 v = i * 4;

			Data[idx] = v;
			Data[idx + 1] = v + 1;
			Data[idx + 2] = v + 2;
			Data[idx + 3] = v;
			Data[idx + 4] = v + 2;
			Data[idx + 5] = v + 3;
		}

		// Points
		Data[PointOffset + i] = i;
	}

	RHIUnlockIndexBuffer(IndexBufferRHI);
	Buffer = nullptr;
}

//////////////////////////////////////////////////////////// Structured Buffer

void FLidarPointCloudStructuredBuffer::Initialize()
{
	// This must be called from Rendering thread
	check(IsInRenderingThread());

	FRHIResourceCreateInfo CreateInfo;
	Buffer = RHICreateStructuredBuffer(ElementSize, ElementSize * Capacity, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
	SRV = RHICreateShaderResourceView(Buffer);
}

void FLidarPointCloudStructuredBuffer::Release()
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

//////////////////////////////////////////////////////////// Instance Buffer

void FLidarPointCloudInstanceBuffer::Reset()
{
	NewNumInstances = 0;
	Nodes.Reset();
}

uint32 FLidarPointCloudInstanceBuffer::UpdateData(bool bUseClassification)
{
	// Populate buffer only if there is any data to populate with
	if (NewNumInstances)
	{
		// Rebuild resource if the new data size is greater than the current buffer can accommodate
		// 17 bytes per point, element size set to 4 bytes - estimated 4.3 elements per point
		StructuredBuffer.Resize(NewNumInstances * 4.3);

		FColor SelectionColor = FColor::White;

#if WITH_EDITOR
		SelectionColor = GetDefault<UEditorStyleSettings>()->SelectionColor.ToFColor(false);
#endif

		// Send new data
		uint8* BufferStart = (uint8*)RHILockStructuredBuffer(StructuredBuffer.Buffer, 0, StructuredBuffer.GetSize(), RLM_WriteOnly);
		uint8* BufferCurrent = BufferStart;
		const FLidarPointCloudPoint* DataPointer = nullptr;

		if (bOwnedByEditor)
		{
			if (bUseClassification)
			{
				for (auto Node : Nodes)
				{
					const auto DataNode = Node->DataNode;
					for (FLidarPointCloudPoint* Data = DataNode->AllocatedPoints.GetData(), *DataEnd = Data + DataNode->AllocatedPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 12);
						BufferCurrent += 12;

						if (Data->bSelected)
						{
							FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
						}
						else
						{
							FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
							FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
						}

						BufferCurrent += 4;
					}

					for (FLidarPointCloudPoint* Data = DataNode->PaddingPoints.GetData(), *DataEnd = Data + DataNode->PaddingPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 12);
						BufferCurrent += 12;

						if (Data->bSelected)
						{
							FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
						}
						else
						{
							FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
							FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
						}

						BufferCurrent += 4;
					}
				}
			}
			else
			{
				for (auto Node : Nodes)
				{
					const auto DataNode = Node->DataNode;
					for (FLidarPointCloudPoint* Data = DataNode->AllocatedPoints.GetData(), *DataEnd = Data + DataNode->AllocatedPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						if (Data->bSelected)
						{
							FMemory::Memcpy(BufferCurrent, Data, 12);
							BufferCurrent += 12;

							FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
							BufferCurrent += 4;
						}
						else
						{
							FMemory::Memcpy(BufferCurrent, Data, 16);
							BufferCurrent += 16;
						}
					}

					for (FLidarPointCloudPoint* Data = DataNode->PaddingPoints.GetData(), *DataEnd = Data + DataNode->PaddingPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						if (Data->bSelected)
						{
							FMemory::Memcpy(BufferCurrent, Data, 12);
							BufferCurrent += 12;

							FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
							BufferCurrent += 4;
						}
						else
						{
							FMemory::Memcpy(BufferCurrent, Data, 16);
							BufferCurrent += 16;
						}
					}
				}
			}
		}
		else
		{
			if (bUseClassification)
			{
				for (auto Node : Nodes)
				{
					const auto DataNode = Node->DataNode;
					for (FLidarPointCloudPoint* Data = DataNode->AllocatedPoints.GetData(), *DataEnd = Data + DataNode->AllocatedPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 12);
						BufferCurrent += 12;

						FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
						FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
						BufferCurrent += 4;
					}

					for (FLidarPointCloudPoint* Data = DataNode->PaddingPoints.GetData(), *DataEnd = Data + DataNode->PaddingPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 12);
						BufferCurrent += 12;

						FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
						FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
						BufferCurrent += 4;
					}
				}
			}
			else
			{
				for (auto Node : Nodes)
				{
					const auto DataNode = Node->DataNode;
					for (FLidarPointCloudPoint* Data = DataNode->AllocatedPoints.GetData(), *DataEnd = Data + DataNode->AllocatedPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 16);
						BufferCurrent += 16;
					}

					for (FLidarPointCloudPoint* Data = DataNode->PaddingPoints.GetData(), *DataEnd = Data + DataNode->PaddingPoints.Num(); Data != DataEnd; ++Data)
					{
						if (!Data->bVisible)
						{
							break;
						}

						FMemory::Memcpy(BufferCurrent, Data, 16);
						BufferCurrent += 16;
					}
				}
			}
		}

		// Calculates the actual number of instances copied to the buffer (accounts for the invisible points)
		NewNumInstances = (BufferCurrent - BufferStart) / 16;

		// Send Scale data
		for (auto Node : Nodes)
		{
			FMemory::Memset(BufferCurrent, Node->VirtualDepth, Node->DataNode->GetNumVisiblePoints());
			BufferCurrent += Node->DataNode->GetNumVisiblePoints();
		}
		RHIUnlockStructuredBuffer(StructuredBuffer.Buffer);
	}

	return NumInstances = NewNumInstances;
}

void FLidarPointCloudInstanceBuffer::AddNode(const FLidarPointCloudTraversalOctreeNode* Node)
{
	Nodes.Add(Node);
	NewNumInstances += Node->DataNode->GetNumVisiblePoints();
}

void FLidarPointCloudInstanceBuffer::Release()
{
	Nodes.Empty();
	NumInstances = 0;
}

//////////////////////////////////////////////////////////// User Data

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
	BINDPARAM(DataBuffer);
	BINDPARAM(IndexDivisor);
	BINDPARAM(VDMultiplier);
	BINDPARAM(SizeOffset);
	BINDPARAM(RootCellSize);
	BINDPARAM(bUseLODColoration);
	BINDPARAM(SpriteSizeMultiplier);
	BINDPARAM(ViewRightVector);
	BINDPARAM(ViewUpVector);
	BINDPARAM(BoundsSize);
	BINDPARAM(BoundsOffset);
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
}

void FLidarPointCloudVertexFactoryShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement, class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
{
	FLidarPointCloudBatchElementUserData* UserData = (FLidarPointCloudBatchElementUserData*)BatchElement.UserData;

	SETSRVPARAM(DataBuffer);
	SETPARAM(IndexDivisor);
	SETPARAM(VDMultiplier);
	SETPARAM(SizeOffset);
	SETPARAM(RootCellSize);
	SETPARAM(bUseLODColoration);
	SETPARAM(SpriteSizeMultiplier);
	SETPARAM(ViewRightVector);
	SETPARAM(ViewUpVector);
	SETPARAM(BoundsSize);
	SETPARAM(BoundsOffset);
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
}

FLidarPointCloudVertexFactory::~FLidarPointCloudVertexFactory()
{
	FRenderCommandFence Fence;

	FVertexFactory* VertexFactory = this;

	ENQUEUE_RENDER_COMMAND(ReleasePointCloudIndexBuffer)(
		[VertexFactory](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->ReleaseResource();
		});

	Fence.BeginFence();
	Fence.Wait();
}

bool FLidarPointCloudVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (IsPCPlatform(Parameters.Platform) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && 
		Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithLidarPointCloud) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FLidarPointCloudVertexFactory::InitRHI()
{
	VertexBuffer.InitResource();

	FVertexDeclarationElementList Elements;
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&VertexBuffer, 0, 0, VET_Float3), 0));
	InitDeclaration(Elements);
}

void FLidarPointCloudVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
	VertexBuffer.ReleaseResource();
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLidarPointCloudVertexFactory, SF_Vertex, FLidarPointCloudVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE(FLidarPointCloudVertexFactory, "/Plugin/LidarPointCloud/Private/LidarPointCloudVertexFactory.ush", /* bUsedWithMaterials */ true, /* bSupportsStaticLighting */ false, /* bSupportsDynamicLighting */ true, /* bPrecisePrevWorldPos */ false, /* bSupportsPositionOnly */ true);

#undef BINDPARAM
#undef SETPARAM
#undef SETSRVPARAM