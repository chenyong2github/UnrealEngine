// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDITexture.h"


namespace mpcdi
{
	struct DataMap;
};

namespace MPCDI
{

	class FMPCDIBlendTexture
		: public FMPCDITexture
	{
	public:
		FMPCDIBlendTexture()
			: FMPCDITexture()
			, EmbeddedGamma(1.0)
		{ 
		}

	public:
		void LoadBlendMap(mpcdi::DataMap* SourceDataMap, float InEmbeddedGamma);
		void CreateDummyAlphaMap();

		inline float GetEmbeddedGamma() const
		{ 
			return EmbeddedGamma; 
		};

	private:
		void LoadCustomMap(EPixelFormat PixelFormat, int Width, int Height, int BufferSize, void *TextureData);

	private:
		float EmbeddedGamma;
	};
};
