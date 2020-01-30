// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDITexture.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"


namespace
{
	class TextureData : public FResourceBulkDataInterface
	{
	public:
		TextureData(const void* InData, uint32_t InDataSize)
			: Data(InData)
			, DataSize(InDataSize)
		{ 
		}

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
		{ 
		}

	private:
		const void* Data;
		uint32_t    DataSize;
	};

	FTexture2DRHIRef CreateTexture2D(void* InData, int InWidth, int InHeight, EPixelFormat InPixelFormat)
	{
		const uint32 DataSize = CalculateImageBytes(InWidth, InHeight, 1, InPixelFormat);
		TextureData BulkDataInterface(InData, DataSize);
		FRHIResourceCreateInfo CreateInfo(&BulkDataInterface);
		return RHICreateTexture2D(InWidth, InHeight, InPixelFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);
	}
}

//---------------------------------------------
// FMPCDITexture
//---------------------------------------------
void MPCDI::FMPCDITexture::ReleaseTextureData()
{
	if (Data != nullptr)
	{
		FMemory::Free(Data);
		Data = nullptr;
	}
}

void MPCDI::FMPCDITexture::InitRHI()
{
	FTexture2DRHIRef Texture2D = CreateTexture2D(Data, Width, Height, PixelFormat);
	TextureRHI = Texture2D;
	
	if (bReleaseData && Data)
	{
		ReleaseTextureData();
	}
}
