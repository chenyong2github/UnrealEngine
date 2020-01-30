// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIBlendTexture.h"

THIRD_PARTY_INCLUDES_START

#include "mpcdiAlphaMap.h"
#include "mpcdiBetaMap.h"
#include "mpcdiBuffer.h"
#include "mpcdiDataMap.h"
#include "mpcdiDisplay.h"
#include "mpcdiDistortionMap.h"
#include "mpcdiGeometryWarpFile.h"
#include "mpcdiProfile.h"
#include "mpcdiReader.h"

THIRD_PARTY_INCLUDES_END

namespace MPCDI
{
	void FMPCDIBlendTexture::LoadBlendMap(mpcdi::DataMap* SourceData, float InEmbeddedGamma)
	{
		static const EPixelFormat format[4][4] =
		{
			{ PF_G8, PF_G16, PF_Unknown, PF_Unknown },
			{ PF_R8G8, PF_G16R16, PF_Unknown, PF_Unknown },
			{ PF_Unknown, PF_Unknown, PF_Unknown, PF_Unknown },
			{ PF_R8G8B8A8, PF_Unknown, PF_Unknown, PF_Unknown },
		};

		EmbeddedGamma = InEmbeddedGamma;

		const EPixelFormat pixelFormat = format[SourceData->GetComponentDepth() - 1][(SourceData->GetBitDepth() >> 3) - 1];
		check(pixelFormat != PF_Unknown);

		const int BufferSize = SourceData->GetComponentDepth()*(SourceData->GetBitDepth() >> 3)*SourceData->GetSizeX()*SourceData->GetSizeY();

		LoadCustomMap(pixelFormat, SourceData->GetSizeX(), SourceData->GetSizeY(), BufferSize, reinterpret_cast<void*>(SourceData->GetData()->data()));
	}

	void FMPCDIBlendTexture::LoadCustomMap(EPixelFormat InPixelFormat, int InWidth, int InHeight, int BufferSize, void *InTextureData)
	{
		ReleaseTextureData();

		void *TextureData = FMemory::Malloc(BufferSize);
		memcpy(TextureData, InTextureData, BufferSize);

		SetTextureData(TextureData, InWidth, InHeight, InPixelFormat);

		if (IsInitialized()) 
		{
			BeginUpdateResourceRHI(this);
		}
		BeginInitResource(this);
	}

	void FMPCDIBlendTexture::CreateDummyAlphaMap()
	{
		unsigned char White = 255;
		LoadCustomMap(PF_G8, 1, 1, 1, reinterpret_cast<void*>(&White));
	}
}
