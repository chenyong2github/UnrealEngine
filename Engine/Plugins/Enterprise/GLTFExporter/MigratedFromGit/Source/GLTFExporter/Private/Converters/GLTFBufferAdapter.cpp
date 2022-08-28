// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFBufferAdapter.h"
#include "Converters/GLTFBufferUtility.h"

class FGLTFBufferAdapterCPU final : public IGLTFBufferAdapter
{
public:

	FGLTFBufferAdapterCPU(const void* Data)
		: Data(Data)
	{
	}

	virtual const uint8* GetData() override
	{
		return static_cast<const uint8*>(Data);
	}

	const void* Data;
};

class FGLTFBufferAdapterGPU final : public IGLTFBufferAdapter
{
public:

	FGLTFBufferAdapterGPU(FRHIIndexBuffer* RHIBuffer)
	{
		FGLTFBufferUtility::ReadRHIBuffer(RHIBuffer, DataBuffer);
	}

	FGLTFBufferAdapterGPU(FRHIVertexBuffer* RHIBuffer)
	{
		FGLTFBufferUtility::ReadRHIBuffer(RHIBuffer, DataBuffer);
	}

	virtual const uint8* GetData() override
	{
		return DataBuffer.Num() > 0 ? DataBuffer.GetData() : nullptr;
	}

	TArray<uint8> DataBuffer;
};

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetIndices(const FRawStaticIndexBuffer* IndexBuffer)
{
	const void* IndexData = IndexBuffer->Is32Bit() ? static_cast<const void*>(IndexBuffer->AccessStream32()) : static_cast<const void*>(IndexBuffer->AccessStream16());
	if (IndexData != nullptr && (WITH_EDITOR || FGLTFBufferUtility::GetAllowCPUAccess(IndexBuffer))) return MakeUnique<FGLTFBufferAdapterCPU>(IndexData);
	return MakeUnique<FGLTFBufferAdapterGPU>(IndexBuffer->IndexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetIndices(const FRawStaticIndexBuffer16or32Interface* IndexBuffer)
{
	const void* IndexData = IndexBuffer->GetResourceDataSize() > 0 ? const_cast<FRawStaticIndexBuffer16or32Interface*>(IndexBuffer)->GetPointerTo(0) : nullptr;
	if (IndexData != nullptr && (WITH_EDITOR || IndexBuffer->GetNeedsCPUAccess())) static_cast<TUniquePtr<IGLTFBufferAdapter>>(MakeUnique<FGLTFBufferAdapterCPU>(IndexData));
	return MakeUnique<FGLTFBufferAdapterGPU>(IndexBuffer->IndexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetPositions(const FPositionVertexBuffer* VertexBuffer)
{
	const void* PositionData = VertexBuffer->GetVertexData();
	if (PositionData != nullptr && (WITH_EDITOR || FGLTFBufferUtility::GetAllowCPUAccess(VertexBuffer))) MakeUnique<FGLTFBufferAdapterCPU>(PositionData);
	return MakeUnique<FGLTFBufferAdapterGPU>(VertexBuffer->VertexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetColors(const FColorVertexBuffer* VertexBuffer)
{
	const void* ColorData = VertexBuffer->GetVertexData();
	if (ColorData != nullptr && (WITH_EDITOR || FGLTFBufferUtility::GetAllowCPUAccess(VertexBuffer))) MakeUnique<FGLTFBufferAdapterCPU>(ColorData);
	return MakeUnique<FGLTFBufferAdapterGPU>(VertexBuffer->VertexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetTangents(const FStaticMeshVertexBuffer* VertexBuffer)
{
	const void* TangentData = VertexBuffer->GetTangentData();
	if (TangentData != nullptr && (WITH_EDITOR || const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetAllowCPUAccess())) MakeUnique<FGLTFBufferAdapterCPU>(TangentData);
	return MakeUnique<FGLTFBufferAdapterGPU>(VertexBuffer->TangentsVertexBuffer.VertexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetUVs(const FStaticMeshVertexBuffer* VertexBuffer)
{
	const void* UVData = VertexBuffer->GetTexCoordData();
	if (UVData != nullptr && (WITH_EDITOR || const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetAllowCPUAccess())) MakeUnique<FGLTFBufferAdapterCPU>(UVData);
	return MakeUnique<FGLTFBufferAdapterGPU>(VertexBuffer->TexCoordVertexBuffer.VertexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetInfluences(const FSkinWeightVertexBuffer* VertexBuffer)
{
	const auto* InfluenceBuffer = VertexBuffer->GetDataVertexBuffer();
	const void* InfluenceData = InfluenceBuffer->GetWeightData();
	if (InfluenceData != nullptr && (WITH_EDITOR || VertexBuffer->GetNeedsCPUAccess())) MakeUnique<FGLTFBufferAdapterCPU>(InfluenceData);
	return MakeUnique<FGLTFBufferAdapterGPU>(InfluenceBuffer->VertexBufferRHI);
}

TUniquePtr<IGLTFBufferAdapter> IGLTFBufferAdapter::GetLookups(const FSkinWeightVertexBuffer* VertexBuffer)
{
	const FSkinWeightLookupVertexBuffer* LookupBuffer = VertexBuffer->GetLookupVertexBuffer();
	const void* LookupData = FGLTFBufferUtility::GetBufferData(LookupBuffer);
	if (LookupData != nullptr && (WITH_EDITOR || VertexBuffer->GetNeedsCPUAccess())) MakeUnique<FGLTFBufferAdapterCPU>(LookupData);
	return MakeUnique<FGLTFBufferAdapterGPU>(LookupBuffer->VertexBufferRHI);
}
