// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"

#include "TextureShareD3D12Helper.h"

bool FTextureShareD3D12Helper::IsTexturesEqual(ID3D12Resource* Texture1, ID3D12Resource* Texture2)
{
	if (Texture1 && Texture2)
	{
		D3D12_RESOURCE_DESC Desc1 = Texture1->GetDesc();
		D3D12_RESOURCE_DESC Desc2 = Texture2->GetDesc();

		return (Desc1.Width == Desc2.Width) && (Desc1.Height == Desc2.Height) && (Desc1.Format == Desc2.Format);
	}

	return false;
}

bool FTextureShareD3D12Helper::CreateSRVTexture(ID3D12Device* pD3D12Device, ID3D12DescriptorHeap* pD3D12HeapSRV, ID3D12Resource* InSharedTexture, ID3D12Resource** OutSRVTexture, int SRVIndex)
{
	D3D12_RESOURCE_DESC SharedTextureDesc = InSharedTexture->GetDesc();
	D3D12_RESOURCE_DESC SRVTextureDesc = {};
	{
		SRVTextureDesc.Format = SharedTextureDesc.Format;
		SRVTextureDesc.Width = SharedTextureDesc.Width;
		SRVTextureDesc.Height = SharedTextureDesc.Height;

		SRVTextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		SRVTextureDesc.MipLevels = 1;
		SRVTextureDesc.DepthOrArraySize = 1;
		SRVTextureDesc.SampleDesc.Count = 1;
		SRVTextureDesc.SampleDesc.Quality = 0;
		SRVTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	}

	HRESULT hResult = pD3D12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&SRVTextureDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		__uuidof(ID3D12Resource), (void**)(OutSRVTexture));

	if (SUCCEEDED(hResult))
	{
		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = SRVTextureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE handle = pD3D12HeapSRV->GetCPUDescriptorHandleForHeapStart();
		uint32 DescriptorSize = pD3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		handle.ptr += SRVIndex * DescriptorSize;

		pD3D12Device->CreateShaderResourceView(*OutSRVTexture, &srvDesc, handle);
	}

	return SUCCEEDED(hResult);
}

void FTextureShareD3D12Helper::CopyResource(ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* SourceTexture2D, ID3D12Resource* DestTexture2D)
{
	pCmdList->CopyResource(DestTexture2D, SourceTexture2D);
}
