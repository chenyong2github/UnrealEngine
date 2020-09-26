// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <tchar.h>
#include <string>
#include <d3d11.h>

#include "TextureShareInterface.h"

class FTextureShareD3D11Helper
{
public:
	// Compare two textures size+format
	static bool IsTexturesEqual(ID3D11Texture2D* Texture1, ID3D11Texture2D* Texture2);

	// Create texture and create SRV
	static bool CreateSRVTexture(ID3D11Device* pD3D11Device, ID3D11Texture2D* InSharedTexture, ID3D11Texture2D** OutTexture, ID3D11ShaderResourceView** OutShaderResourceView);

	// Copy image between two textures
	static bool CopyResource(ID3D11Device* pD3D11Device, ID3D11Resource* SourceTexture2D, ID3D11Resource* DestTexture2D);
};

