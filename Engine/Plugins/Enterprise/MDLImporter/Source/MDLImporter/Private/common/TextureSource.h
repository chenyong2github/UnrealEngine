// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Texture.h"
#include "Math/NumericLimits.h"
#include "common/Utility.h"

namespace Common
{
	FORCEINLINE bool IsValueInRange(float Value)
	{
		return Value >= 0.f && Value <= 1.f;
	}

	// Converts a float texture to a corresponding texture source.
	inline FTextureSource* CreateTextureSource(const float* InData, int InWidth, int InHeight, int InChannels, bool bFlipY)
	{
		// use 16 bpp for linear textures support(i.e. if more than 1 channel)
		const ETextureSourceFormat Format = InChannels == 1 ? TSF_G8 : TSF_RGBA16;
		const int                  Size   = InWidth * InHeight * InChannels;

		FTextureSource* Source = new FTextureSource();
		Source->Init(InWidth, InHeight, 1, 1, Format);

		uint8*      DstBuf = Source->LockMip(0);
		const float Max8   = TNumericLimits<uint8>::Max();
		const float Max16  = TNumericLimits<uint16>::Max();
		switch (InChannels)
		{
			case 1:
			{
				for (int Y = 0; Y < InHeight; ++Y)
				{
					const int SrcY = bFlipY ? (InHeight - 1 - Y) : Y;
					for (int X = 0; X < InWidth; ++X)
					{
						int DstOffset = Y * InWidth + X;
						int SrcOffset = SrcY * InWidth + X;

						DstBuf[DstOffset] = Saturate(InData[SrcOffset]) * Max8;
					}
				}
				break;
			}
			case 3:
			{
				uint16* Dst = (uint16*)DstBuf;
				for (int Y = 0; Y < InHeight; ++Y)
				{
					const int SrcY = bFlipY ? (InHeight - 1 - Y) : Y;
					for (int X = 0; X < InWidth; ++X)
					{
						int DstOffset = Y * InWidth * 4 + X * 4;
						int SrcOffset = SrcY * InWidth * 3 + X * 3;

						// check(IsValueInRange(InData[SrcOffset]));
						// check(IsValueInRange(InData[SrcOffset + 1]));
						// check(IsValueInRange(InData[SrcOffset + 2]));
						Dst[DstOffset]     = Saturate(InData[SrcOffset]) * Max16;
						Dst[DstOffset + 1] = Saturate(InData[SrcOffset + 1]) * Max16;
						Dst[DstOffset + 2] = Saturate(InData[SrcOffset + 2]) * Max16;
						Dst[DstOffset + 3] = Max16;
					}
				}
				break;
			}
			case 4:
			{
				uint16* Dst = (uint16*)DstBuf;
				for (int Y = 0; Y < InHeight; ++Y)
				{
					const int SrcY = bFlipY ? (InHeight - 1 - Y) : Y;
					for (int X = 0; X < InWidth; ++X)
					{
						int DstOffset = Y * InWidth * 4 + X * 4;
						int SrcOffset = SrcY * InWidth * 4 + X * 4;

						check(IsValueInRange(InData[SrcOffset]));
						check(IsValueInRange(InData[SrcOffset + 1]));
						check(IsValueInRange(InData[SrcOffset + 2]));
						check(IsValueInRange(InData[SrcOffset + 3]));
						Dst[DstOffset]     = Saturate(InData[SrcOffset]) * Max16;
						Dst[DstOffset + 1] = Saturate(InData[SrcOffset + 1]) * Max16;
						Dst[DstOffset + 2] = Saturate(InData[SrcOffset + 2]) * Max16;
						Dst[DstOffset + 3] = Saturate(InData[SrcOffset + 3]) * Max16;
					}
				}
				break;
			}
			default:
				check(false);
				break;
		}
		Source->UnlockMip(0);
		return Source;
	}
}
