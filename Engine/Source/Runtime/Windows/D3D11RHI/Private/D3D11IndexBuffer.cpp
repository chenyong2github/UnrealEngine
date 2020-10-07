// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11IndexBuffer.cpp: D3D Index buffer RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"

FIndexBufferRHIRef FD3D11DynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	InUsage |= BUF_IndexBuffer;

	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D11Buffer();
	}

	// Explicitly check that the size is nonzero before allowing CreateIndexBuffer to opaquely fail.
	check(Size > 0);

	// Describe the index buffer.
	D3D11_BUFFER_DESC Desc;
	ZeroMemory( &Desc, sizeof( D3D11_BUFFER_DESC ) );
	Desc.ByteWidth = Size;
	Desc.Usage = (InUsage & BUF_AnyDynamic) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	Desc.CPUAccessFlags = (InUsage & BUF_AnyDynamic) ? D3D11_CPU_ACCESS_WRITE : 0;
	Desc.MiscFlags = 0;

	if (InUsage & BUF_UnorderedAccess)
	{
		Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	if(InUsage & BUF_DrawIndirect)
	{
		Desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	}

	if (InUsage & BUF_ShaderResource)
	{
		Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	}

	if (InUsage & BUF_Shared)
	{
		if (GCVarUseSharedKeyedMutex->GetInt() != 0)
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
		}
		else
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
		}
	}

	// If a resource array was provided for the resource, create the resource pre-populated
	D3D11_SUBRESOURCE_DATA InitData;
	D3D11_SUBRESOURCE_DATA* pInitData = NULL;
	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
		InitData.pSysMem = CreateInfo.ResourceArray->GetResourceData();
		InitData.SysMemPitch = Size;
		InitData.SysMemSlicePitch = 0;
		pInitData = &InitData;
	}

	TRefCountPtr<ID3D11Buffer> IndexBufferResource;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateBuffer(&Desc,pInitData,IndexBufferResource.GetInitReference()), Direct3DDevice);

	UpdateBufferStats(IndexBufferResource, true);

	if(CreateInfo.ResourceArray)
	{
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	return new FD3D11Buffer(IndexBufferResource, Size, InUsage, Stride);
}

FIndexBufferRHIRef FD3D11DynamicRHI::CreateIndexBuffer_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 Stride,
	uint32 Size,
	uint32 InUsage,
	ERHIAccess InResourceState,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateIndexBuffer(Stride, Size, InUsage, InResourceState, CreateInfo);
}

void* FD3D11DynamicRHI::LockBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FD3D11Buffer* Buffer = ResourceCast(BufferRHI);
	// If this resource is bound to the device, unbind it
	ConditionalClearShaderResource(Buffer, true);

	// Determine whether the buffer is dynamic or not.
	D3D11_BUFFER_DESC Desc;
	Buffer->Resource->GetDesc(&Desc);
	const bool bIsDynamic = (Desc.Usage == D3D11_USAGE_DYNAMIC);

	FD3D11LockedKey LockedKey(Buffer->Resource);
	FD3D11LockedData LockedData;

	if(bIsDynamic)
	{
		check(LockMode == RLM_WriteOnly || LockMode == RLM_WriteOnly_NoOverwrite);

		// If the buffer is dynamic, map its memory for writing.
		D3D11_MAPPED_SUBRESOURCE MappedSubresource;

		D3D11_MAP MapType = (LockMode == RLM_WriteOnly)? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
		VERIFYD3D11RESULT_EX(Direct3DDeviceIMContext->Map(Buffer->Resource, 0, MapType, 0, &MappedSubresource), Direct3DDevice);

		LockedData.SetData(MappedSubresource.pData);
		LockedData.Pitch = MappedSubresource.RowPitch;
	}
	else
	{
		if(LockMode == RLM_ReadOnly)
		{
			// If the static buffer is being locked for reading, create a staging buffer.
			D3D11_BUFFER_DESC StagingBufferDesc;
			ZeroMemory( &StagingBufferDesc, sizeof( D3D11_BUFFER_DESC ) );
			StagingBufferDesc.ByteWidth = Size;
			StagingBufferDesc.Usage = D3D11_USAGE_STAGING;
			StagingBufferDesc.BindFlags = 0;
			StagingBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			StagingBufferDesc.MiscFlags = 0;
			TRefCountPtr<ID3D11Buffer> StagingBuffer;
			VERIFYD3D11RESULT_EX(Direct3DDevice->CreateBuffer(&StagingBufferDesc, NULL, StagingBuffer.GetInitReference()), Direct3DDevice);
			LockedData.StagingResource = StagingBuffer;

			// Copy the contents of the buffer to the staging buffer.
			D3D11_BOX SourceBox;
			SourceBox.left = Offset;
			SourceBox.right = Offset + Size;
			SourceBox.top = SourceBox.front = 0;
			SourceBox.bottom = SourceBox.back = 1;
			Direct3DDeviceIMContext->CopySubresourceRegion(StagingBuffer, 0, 0, 0, 0, Buffer->Resource, 0, &SourceBox);

			// Map the staging buffer's memory for reading.
			D3D11_MAPPED_SUBRESOURCE MappedSubresource;
			VERIFYD3D11RESULT_EX(Direct3DDeviceIMContext->Map(StagingBuffer, 0, D3D11_MAP_READ, 0, &MappedSubresource), Direct3DDevice);
			LockedData.SetData(MappedSubresource.pData);
			LockedData.Pitch = MappedSubresource.RowPitch;
			Offset = 0;
		}
		else
		{
			// If the static buffer is being locked for writing, allocate memory for the contents to be written to.
			LockedData.AllocData(Desc.ByteWidth);
			LockedData.Pitch = Desc.ByteWidth;
		}
	}
	
	// Add the lock to the lock map.
	AddLockedData(LockedKey, LockedData);

	// Return the offset pointer
	return (void*)((uint8*)LockedData.GetData() + Offset);
}

void FD3D11DynamicRHI::UnlockBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIBuffer* BufferRHI)
{
	FD3D11Buffer* Buffer = ResourceCast(BufferRHI);

	// Determine whether the buffer is dynamic or not.
	D3D11_BUFFER_DESC Desc;
	Buffer->Resource->GetDesc(&Desc);
	const bool bIsDynamic = (Desc.Usage == D3D11_USAGE_DYNAMIC);

	// Find the outstanding lock for this buffer and remove it from the tracker.
	FD3D11LockedData LockedData;
	verifyf(RemoveLockedData(FD3D11LockedKey(Buffer->Resource), LockedData), TEXT("Buffer is not locked"));

	if(bIsDynamic)
	{
		// If the buffer is dynamic, its memory was mapped directly; unmap it.
		Direct3DDeviceIMContext->Unmap(Buffer->Resource, 0);
	}
	else
	{
		// If the static buffer lock involved a staging resource, it was locked for reading.
		if(LockedData.StagingResource)
		{
			// Unmap the staging buffer's memory.
			ID3D11Buffer* StagingBuffer = (ID3D11Buffer*)LockedData.StagingResource.GetReference();
			Direct3DDeviceIMContext->Unmap(StagingBuffer,0);
		}
		else 
		{
			// Copy the contents of the temporary memory buffer allocated for writing into the buffer.
			Direct3DDeviceIMContext->UpdateSubresource(Buffer->Resource, 0, NULL, LockedData.GetData(), LockedData.Pitch, 0);

			// Free the temporary memory buffer.
			LockedData.FreeData();
		}
	}
}

void FD3D11DynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	check(DestBuffer);
	FD3D11Buffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		Dest->ReleaseUnderlyingResource();
	}
	else
	{
		FD3D11Buffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
}
