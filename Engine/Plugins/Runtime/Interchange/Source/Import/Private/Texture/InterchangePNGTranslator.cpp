// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangePNGTranslator.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/TextureTranslatorUtilities.h"


//////////////////////////////////////////////////////////////////////////
// PNG helper local function
namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			/**
			 * This fills any pixels of a texture with have an alpha value of zero,
			 * with an RGB from the nearest neighboring pixel which has non-zero alpha.
			 */
			template<typename PixelDataType, typename ColorDataType, int32 RIdx, int32 GIdx, int32 BIdx, int32 AIdx>
			class TPNGDataFill
			{
			public:

				TPNGDataFill(int32 SizeX, int32 SizeY, uint8* SourceTextureData)
					: SourceData(reinterpret_cast<PixelDataType*>(SourceTextureData))
					, TextureWidth(SizeX)
					, TextureHeight(SizeY)
				{
				}

				void ProcessData()
				{
					int32 NumZeroedTopRowsToProcess = 0;
					int32 FillColorRow = -1;
					for (int32 Y = 0; Y < TextureHeight; ++Y)
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
						for (int32 Y = 0; Y <= NumZeroedTopRowsToProcess; ++Y)
						{
							FillRowColorPixels(NumZeroedTopRowsToProcess + 1, Y);
						}
					}
				}

				/** returns False if requires further processing because entire row is filled with zeroed alpha values */
				bool ProcessHorizontalRow(int32 Y)
				{
					// only wipe out colors that are affected by png turning valid colors white if alpha = 0
					const uint32 WhiteWithZeroAlpha = FColor(255, 255, 255, 0).DWColor();

					// Left -> Right
					int32 NumLeftmostZerosToProcess = 0;
					const PixelDataType* FillColor = nullptr;
					for (int32 X = 0; X < TextureWidth; ++X)
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
					for (int32 X = 0; X <= NumLeftmostZerosToProcess; ++X)
					{
						PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
						PixelData[RIdx] = FillColor[RIdx];
						PixelData[GIdx] = FillColor[GIdx];
						PixelData[BIdx] = FillColor[BIdx];
					}

					return true;
				}

				void FillRowColorPixels(int32 FillColorRow, int32 Y)
				{
					for (int32 X = 0; X < TextureWidth; ++X)
					{
						const PixelDataType* FillColor = SourceData + (FillColorRow * TextureWidth + X) * 4;
						PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
						PixelData[RIdx] = FillColor[RIdx];
						PixelData[GIdx] = FillColor[GIdx];
						PixelData[BIdx] = FillColor[BIdx];
					}
				}

				PixelDataType* SourceData;
				int32 TextureWidth;
				int32 TextureHeight;
			};

			/**
			 * For PNG texture importing, this ensures that any pixels with an alpha value of zero have an RGB
			 * assigned to them from a neighboring pixel which has non-zero alpha.
			 * This is needed as PNG exporters tend to turn pixels that are RGBA = (x,x,x,0) to (1,1,1,0)
			 * and this produces artifacts when drawing the texture with bilinear filtering.
			 *
			 * @param TextureSource - The source texture
			 * @param SourceData - The source texture data
			 */
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
				}
			}

		}//ns Private
	}//ns Interchange
}//ns UE

bool UInterchangePNGTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString PNGExtension = (TEXT("png;Texture"));
	return PNGExtension.StartsWith(Extension);
}

bool UInterchangePNGTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportImage> UInterchangePNGTranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PNG, bad source data."));
		return TOptional<UE::Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = GetSourceData()->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PNG, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PNG, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PNG, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	// Validate it.
	const int32 Length = BufferEnd - Buffer;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	//
	// PNG
	//
	TSharedPtr<IImageWrapper> PngImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PngImageWrapper.IsValid() || !PngImageWrapper->SetCompressed(Buffer, Length))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to decode PNG. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}
	if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(PngImageWrapper->GetWidth(), PngImageWrapper->GetHeight(), bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PNG, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), PngImageWrapper->GetWidth(), PngImageWrapper->GetHeight(), bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	// Select the texture's source format
	ETextureSourceFormat TextureFormat = TSF_Invalid;
	int32 BitDepth = PngImageWrapper->GetBitDepth();
	ERGBFormat Format = PngImageWrapper->GetFormat();

	if (Format == ERGBFormat::Gray)
	{
		if (BitDepth <= 8)
		{
			TextureFormat = TSF_G8;
			Format = ERGBFormat::Gray;
			BitDepth = 8;
		}
		else if (BitDepth == 16)
		{
			// TODO: TSF_G16?
			TextureFormat = TSF_RGBA16;
			Format = ERGBFormat::RGBA;
			BitDepth = 16;
		}
	}
	else if (Format == ERGBFormat::RGBA || Format == ERGBFormat::BGRA)
	{
		if (BitDepth <= 8)
		{
			TextureFormat = TSF_BGRA8;
			Format = ERGBFormat::BGRA;
			BitDepth = 8;
		}
		else if (BitDepth == 16)
		{
			TextureFormat = TSF_RGBA16;
			Format = ERGBFormat::RGBA;
			BitDepth = 16;
		}
	}

	if (TextureFormat == TSF_Invalid)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("PNG file [%s] contains data in an unsupported format"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	UE::Interchange::FImportImage PayloadData;
	PayloadData.Init2DWithParams(
		PngImageWrapper->GetWidth(),
		PngImageWrapper->GetHeight(),
		TextureFormat,
		BitDepth < 16
	);

	if (PngImageWrapper->GetRaw(Format, BitDepth, PayloadData.GetArrayViewOfRawData()))
	{
		bool bFillPNGZeroAlpha = true;
		GConfig->GetBool(TEXT("TextureImporter"), TEXT("FillPNGZeroAlpha"), bFillPNGZeroAlpha, GEditorIni);

		if (bFillPNGZeroAlpha)
		{
			// Replace the pixels with 0.0 alpha with a color value from the nearest neighboring color which has a non-zero alpha
			UE::Interchange::Private::FillZeroAlphaPNGData(PayloadData.SizeX, PayloadData.SizeY, PayloadData.Format, static_cast<uint8*>(PayloadData.RawData.GetData()));
		}
	}
	else
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to decode PNG. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	return PayloadData;
}