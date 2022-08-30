// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureImportUtils.h"

#include "ImageCore.h"

namespace UE
{
	namespace TextureUtilitiesCommon
	{
		/**
		 * Detect the existence of gray scale image in some formats and convert those to a gray scale equivalent image
		 * 
		 * @return true if the image was converted
		 */
		bool AutoDetectAndChangeGrayScale(FImage& Image)
		{
			if (Image.Format != ERawImageFormat::BGRA8)
			{
				return false;
			}

			// auto-detect gray BGRA8 and change to G8

			const FColor* Colors = (const FColor*)Image.RawData.GetData();
			int64 NumPixels = Image.GetNumPixels();

			for (int64 i = 0; i < NumPixels; i++)
			{
				if (Colors[i].A != 255 ||
					Colors[i].R != Colors[i].B ||
					Colors[i].G != Colors[i].B)
				{
					return false ;
				}
			}

			// yes, it's gray, do it :
			Image.ChangeFormat(ERawImageFormat::G8, Image.GammaSpace);

			return true;
		}

		/**
		 * This fills any pixels of a texture with have an alpha value of zero,
		 * with an RGB from the nearest neighboring pixel which has non-zero alpha.
		 */
		template<typename PixelDataType, typename ColorDataType, int32 RIdx, int32 GIdx, int32 BIdx, int32 AIdx>
		class TPNGDataFill
		{
		public:

			explicit TPNGDataFill( int32 SizeX, int32 SizeY, uint8* SourceTextureData )
				: SourceData( reinterpret_cast<PixelDataType*>(SourceTextureData) )
				, TextureWidth(SizeX)
				, TextureHeight(SizeY)
			{
			}

			void ProcessData()
			{
				int64 NumZeroedTopRowsToProcess = 0;
				int64 FillColorRow = -1;
				for (int64 Y = 0; Y < TextureHeight; ++Y)
				{
					if (!ProcessHorizontalRow(Y))
					{
						if (FillColorRow != -1)
						{
							FillRowColorPixels(FillColorRow, Y);
						}
						else
						{
							NumZeroedTopRowsToProcess = Y;
						}
					}
					else
					{
						FillColorRow = Y;
					}
				}

				// Can only fill upwards if image not fully zeroed
				if (NumZeroedTopRowsToProcess > 0 && NumZeroedTopRowsToProcess + 1 < TextureHeight)
				{
					for (int64 Y = 0; Y <= NumZeroedTopRowsToProcess; ++Y)
					{
						FillRowColorPixels(NumZeroedTopRowsToProcess + 1, Y);
					}
				}
			}

			/* returns False if requires further processing because entire row is filled with zeroed alpha values */
			bool ProcessHorizontalRow(int64 Y)
			{
				// only wipe out colors that are affected by png turning valid colors white if alpha = 0
				ColorDataType WhiteWithZeroAlpha;
				if ( sizeof(ColorDataType) == 4 )
				{
					WhiteWithZeroAlpha = FColor(255, 255, 255, 0).DWColor();
				}
				else
				{
					check( sizeof(ColorDataType) == 8 );
					uint16 RGBA[4] = { 0xFFFF,0xFFFF,0xFFFF, 0 };
					check( sizeof(RGBA) == 8 );
					memcpy(&WhiteWithZeroAlpha,RGBA, sizeof(ColorDataType));
				}

				// Left -> Right
				int64 NumLeftmostZerosToProcess = 0;
				const PixelDataType* FillColor = nullptr;
				for (int64 X = 0; X < TextureWidth; ++X)
				{
					PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
					ColorDataType* ColorData = reinterpret_cast<ColorDataType*>(PixelData);

					if (*ColorData == WhiteWithZeroAlpha)
					{
						if (FillColor)
						{
							PixelData[RIdx] = FillColor[RIdx];
							PixelData[GIdx] = FillColor[GIdx];
							PixelData[BIdx] = FillColor[BIdx];
						}
						else
						{
							// Mark pixel as needing fill
							*ColorData = 0;

							// Keep track of how many pixels to fill starting at beginning of row
							NumLeftmostZerosToProcess = X;
						}
					}
					else
					{
						FillColor = PixelData;
					}
				}

				if (NumLeftmostZerosToProcess == 0)
				{
					// No pixels left that are zero
					return true;
				}

				if (NumLeftmostZerosToProcess + 1 >= TextureWidth)
				{
					// All pixels in this row are zero and must be filled using rows above or below
					return false;
				}

				// Fill using non zero pixel immediately to the right of the beginning series of zeros
				FillColor = SourceData + (Y * TextureWidth + NumLeftmostZerosToProcess + 1) * 4;

				// Fill zero pixels found at beginning of row that could not be filled during the Left to Right pass
				for (int64 X = 0; X <= NumLeftmostZerosToProcess; ++X)
				{
					PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
					PixelData[RIdx] = FillColor[RIdx];
					PixelData[GIdx] = FillColor[GIdx];
					PixelData[BIdx] = FillColor[BIdx];
				}

				return true;
			}

			void FillRowColorPixels(int64 FillColorRow, int64 Y)
			{
				for (int64 X = 0; X < TextureWidth; ++X)
				{
					const PixelDataType* FillColor = SourceData + (FillColorRow * TextureWidth + X) * 4;
					PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
					PixelData[RIdx] = FillColor[RIdx];
					PixelData[GIdx] = FillColor[GIdx];
					PixelData[BIdx] = FillColor[BIdx];
				}
			}

			PixelDataType* SourceData;
			int64 TextureWidth;
			int64 TextureHeight;
		};

		void FillZeroAlphaPNGData(int32 SizeX, int32 SizeY, ETextureSourceFormat SourceFormat, uint8* SourceData)
		{
			switch (SourceFormat)
			{
				case TSF_BGRA8:
				{
					TPNGDataFill<uint8, uint32, 2, 1, 0, 3> PNGFill(SizeX, SizeY, SourceData);
					PNGFill.ProcessData();
					break;
				}

				case TSF_RGBA16:
				{
					TPNGDataFill<uint16, uint64, 0, 1, 2, 3> PNGFill(SizeX, SizeY, SourceData);
					PNGFill.ProcessData();
					break;
				}

				default:
				{
					// G8, G16, no alpha to fill
					break;
				}
			}
		}
	}
}