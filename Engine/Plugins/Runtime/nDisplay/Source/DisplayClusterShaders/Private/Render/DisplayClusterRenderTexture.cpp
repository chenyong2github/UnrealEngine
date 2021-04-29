// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/DisplayClusterRenderTexture.h"

FDisplayClusterRenderTexture::FDisplayClusterRenderTexture()
	: Data(NULL)
	, Width(0)
	, Height(0)
	, PixelFormat(PF_Unknown)
	, bHasCPUAccess(false)
{ }

FDisplayClusterRenderTexture::~FDisplayClusterRenderTexture()
{
	ReleaseTextureData();
}

void FDisplayClusterRenderTexture::CreateTexture(EPixelFormat InPixelFormat, uint32_t InWidth, uint32_t InHeight, void* InTextureData, bool bInHasCPUAccess)
{
	bHasCPUAccess = bInHasCPUAccess;

	const uint32 DataSize = CalculateImageBytes(Width, Height, 1, PixelFormat);
	Data = FMemory::Malloc(DataSize);
	memcpy(Data, InTextureData, DataSize);

	// Create texture from data:
	PixelFormat = InPixelFormat;
	Width = InWidth;
	Height = InHeight;

	if (IsInitialized())
	{
		BeginUpdateResourceRHI(this);
	}

	BeginInitResource(this);
}

void FDisplayClusterRenderTexture::ReleaseTextureData()
{
	if (Data != nullptr)
	{
		FMemory::Free(Data);
		Data = nullptr;
	}
}

class TextureData : public FResourceBulkDataInterface
{
public:
	TextureData(const void* InData, uint32_t InDataSize)
		: Data(InData)
		, DataSize(InDataSize)
	{ }

public:
	virtual const void* GetResourceBulkData() const
	{
		return Data;
	}

	virtual uint32 GetResourceBulkDataSize() const
	{
		return DataSize;
	}

	virtual void Discard()
	{ }

private:
	const void* Data;
	uint32_t    DataSize;
};

void FDisplayClusterRenderTexture::InitRHI()
{
	check(IsInRenderingThread());

	const uint32 DataSize = CalculateImageBytes(Width, Height, 1, PixelFormat);
	TextureData BulkDataInterface(Data, DataSize);

	// @todo: Changed for CIS but needs to be fixed.
	FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterRenderTexture"), &BulkDataInterface);

	TextureRHI = RHICreateTexture2D(Width, Height, PixelFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);

	// CPU access not required, release from memory
	if (bHasCPUAccess == false)
	{
		ReleaseTextureData();
	}
};

