// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareD3D11Helper.h"

bool FTextureShareD3D11Helper::IsTexturesEqual(ID3D11Texture2D* Texture1, ID3D11Texture2D* Texture2)
{
	if (Texture1 && Texture2)
	{
		D3D11_TEXTURE2D_DESC Desc1, Desc2;
		Texture1->GetDesc(&Desc1);
		Texture2->GetDesc(&Desc2);

		return (Desc1.Width == Desc2.Width) && (Desc1.Height == Desc2.Height) && (Desc1.Format == Desc2.Format);
	}

	return false;
}

bool FTextureShareD3D11Helper::CreateSRVTexture(ID3D11Device* pD3D11Device, ID3D11Texture2D* InSharedTexture, ID3D11Texture2D** OutTexture, ID3D11ShaderResourceView** OutShaderResourceView)
{
	if (!InSharedTexture || !pD3D11Device)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC SharedTextureDesc;
	InSharedTexture->GetDesc(&SharedTextureDesc);

	D3D11_TEXTURE2D_DESC SRVTextureDesc = {};

	// Use size&format from shared texture
	SRVTextureDesc.Format = SharedTextureDesc.Format;
	SRVTextureDesc.Width  = SharedTextureDesc.Width;
	SRVTextureDesc.Height = SharedTextureDesc.Height;

	SRVTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	SRVTextureDesc.MipLevels = 1;
	SRVTextureDesc.ArraySize = 1;
	SRVTextureDesc.SampleDesc.Count = 1;
	SRVTextureDesc.SampleDesc.Quality = 0;
	SRVTextureDesc.CPUAccessFlags = 0;
	SRVTextureDesc.MiscFlags = 0;
	SRVTextureDesc.Usage = D3D11_USAGE_DEFAULT;

	// Create texture for SRV
	HRESULT hResult = pD3D11Device->CreateTexture2D(&SRVTextureDesc, nullptr, OutTexture);

	if (SUCCEEDED(hResult))
	{
		// Create SRV
		D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;

		srDesc.Format = SharedTextureDesc.Format;

		srDesc.Texture2D.MostDetailedMip = 0;
		srDesc.Texture2D.MipLevels = 1;
		srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

		hResult = pD3D11Device->CreateShaderResourceView(*OutTexture, &srDesc, OutShaderResourceView);
	}

	return SUCCEEDED(hResult);
}

bool FTextureShareD3D11Helper::CopyResource(ID3D11Device* pD3D11Device, ID3D11Resource* SourceTexture2D, ID3D11Resource* DestTexture2D)
{
	ID3D11DeviceContext* D3D11DeviceContext;
	pD3D11Device->GetImmediateContext(&D3D11DeviceContext);

	if (D3D11DeviceContext)
	{
		D3D11DeviceContext->CopyResource(DestTexture2D, SourceTexture2D);
		D3D11DeviceContext->Release();
		return true;
	}
	return false;
}
