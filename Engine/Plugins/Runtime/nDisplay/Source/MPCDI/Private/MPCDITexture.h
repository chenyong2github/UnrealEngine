// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"


namespace MPCDI
{
	class FMPCDITexture : public FTexture
	{
	public:
		FMPCDITexture()
			: Data(NULL)
			, Width(0)
			, Height(0)
			, PixelFormat(PF_Unknown)
			, bReleaseData(false)
		{ 
		}
		virtual ~FMPCDITexture()
		{
		}

	public:
		virtual void InitRHI() override;

		void SetTextureData(void *InData, uint32_t InWidth, uint32_t InHeight, EPixelFormat InPixelFormat, bool releaseData = true)
		{
			Data = InData;
			Width = InWidth;
			Height = InHeight;
			PixelFormat = InPixelFormat;
			bReleaseData = releaseData;
		}

		inline void* GetData() const
		{ 
			return Data; 
		}

		inline uint32_t GetWidth() const
		{ 
			return Width; 
		}

		inline uint32_t GetHeight() const
		{ 
			return Height; 
		}

		inline EPixelFormat GetPixelFormat() const
		{ 
			return PixelFormat; 
		}

		inline bool IsValid() const
		{
			return (Width > 0) && (Height > 0);
		}

		void ReleaseTextureData();

	private:
		void *Data;
		uint32_t Width, Height;
		EPixelFormat PixelFormat;
		bool bReleaseData;
	};
}
