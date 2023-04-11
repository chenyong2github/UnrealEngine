// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpImageSwizzle.h"

#include "MuR/ImagePrivate.h"
#include "Async/ParallelFor.h"

namespace mu
{
	
	Ptr<Image> ImageSwizzle
		(
			EImageFormat format,
			const Ptr<const Image> pSources[],
            const uint8 channels[]
		)
	{
		MUTABLE_CPUPROFILER_SCOPE(ImageSwizzle);

		if (!pSources[0])
		{
			return nullptr;
		}

        ImagePtr pDest = new Image( pSources[0]->GetSizeX(), pSources[0]->GetSizeY(),
                                    pSources[0]->GetLODCount(),
                                    format );

		// Very slow generic implementations
        int32 PixelCount = pDest->CalculatePixelCount();

        // Pixelcount should already match, but due to bugs it may not be the case. Try to detect it,
        // but avoid crashing below:
        uint16 numChannels = GetImageFormatData(format).m_channels;
        for (uint16 c=0;c<numChannels; ++c)
        {
            if (pSources[c])
            {
                size_t sourcePixelCount = pSources[c]->CalculatePixelCount();
                if (PixelCount>sourcePixelCount)
                {
                    check(false);

                    // Something went wrong
					PixelCount = sourcePixelCount;
                }
            }
        }

		int NumDestChannels = 0;

		switch ( format )
		{
		case EImageFormat::IF_L_UBYTE:
			NumDestChannels = 1;
			break;

		case EImageFormat::IF_RGB_UBYTE:
			NumDestChannels = 3;
			break;

        case EImageFormat::IF_RGBA_UBYTE:
        case EImageFormat::IF_BGRA_UBYTE:
			NumDestChannels = 4;
			break;

        default:
			check(false);
		}

		for (int i = 0; i < NumDestChannels; ++i)
		{
			uint8* pDestBuf = pDest->GetData() + i;

			if (format == EImageFormat::IF_BGRA_UBYTE)
			{
				if (i == 0)
				{
					pDestBuf = pDest->GetData() + 2;
				}
				else if (i == 2)
				{
					pDestBuf = pDest->GetData() + 0;
				}
			}

			bool filled = false;

			constexpr int32 NumBatchElems = 4096*2;
			const int32 NumBatches = FMath::DivideAndRoundUp(PixelCount, NumBatchElems);

			if (pSources[i])
			{
				const uint8* pSourceBuf = pSources[i]->GetData() + channels[i];

				switch (pSources[i]->GetFormat())
				{
				case EImageFormat::IF_L_UBYTE:
					if (channels[i] < 1)
					{
						auto ProcessBatch = [pDestBuf, pSourceBuf, NumDestChannels, PixelCount, NumBatchElems ](int32 BatchId)
						{
							const int32 BatchBegin = BatchId * NumBatchElems;
							const int32 BatchEnd = FMath::Min(BatchBegin + NumBatchElems, PixelCount);

							for (int32 p = BatchBegin; p < BatchEnd; ++p)
							{
								pDestBuf[p * NumDestChannels] = pSourceBuf[p];
							}
						};

						if (NumBatches == 1)
						{
							ProcessBatch(0);
						}
						else if (NumBatches > 1)
						{
							ParallelFor(NumBatches, ProcessBatch);
						}

						filled = true;
					}
					break;

				case EImageFormat::IF_RGB_UBYTE:
					if (channels[i] < 3)
					{
						auto ProcessBatch = [pDestBuf, pSourceBuf, NumDestChannels, PixelCount, NumBatchElems](int32 BatchId)
						{
							const int32 BatchBegin = BatchId * NumBatchElems;
							const int32 BatchEnd = FMath::Min(BatchBegin + NumBatchElems, PixelCount);

							for (int32 p = BatchBegin; p < BatchEnd; ++p)
							{
								pDestBuf[p * NumDestChannels] = pSourceBuf[p * 3];
							}
						};

						if (NumBatches == 1)
						{
							ProcessBatch(0);
						}
						else if (NumBatches > 1)
						{
							ParallelFor(NumBatches, ProcessBatch);
						}

						filled = true;
					}
					break;

				case EImageFormat::IF_RGBA_UBYTE:
					if (channels[i] < 4)
					{	
						auto ProcessBatch = [pDestBuf, pSourceBuf, NumDestChannels, PixelCount, NumBatchElems](int32 BatchId)
						{
							const int32 BatchBegin = BatchId * NumBatchElems;
							const int32 BatchEnd = FMath::Min(BatchBegin + NumBatchElems, PixelCount);

							for (int32 p = BatchBegin; p < BatchEnd; ++p)
							{
								pDestBuf[p * NumDestChannels] = pSourceBuf[p * 4];
							}
						};

						if (NumBatches == 1)
						{
							ProcessBatch(0);
						}
						else if (NumBatches > 1)
						{
							ParallelFor(NumBatches, ProcessBatch);
						}

						filled = true;
					}
					break;

				case EImageFormat::IF_BGRA_UBYTE:
					if (channels[i] == 0)
					{
						pSourceBuf = pSources[i]->GetData() + 2;
					}
					else if (channels[i] == 2)
					{
						pSourceBuf = pSources[i]->GetData() + 0;
					}
					if (channels[i] < 4)
					{
						auto ProcessBatch = [pDestBuf, pSourceBuf, NumDestChannels, PixelCount, NumBatchElems](int32 BatchId)
						{
							const int32 BatchBegin = BatchId * NumBatchElems;
							const int32 BatchEnd = FMath::Min(BatchBegin + NumBatchElems, PixelCount);
							for (int32 p = BatchBegin; p < BatchEnd; ++p)
							{
								pDestBuf[p * NumDestChannels] = pSourceBuf[p * 4];
							}
						};

						if (NumBatches == 1)
						{
							ProcessBatch(0);
						}
						else if (NumBatches > 1)
						{
							ParallelFor(NumBatches, ProcessBatch);
						}

						filled = true;
					}
					break;

				default:
					check(false);
				}
			}

			if (!filled)
			{
				// Source not set. Clear to 0
				auto ProcessBatch = [pDestBuf, NumDestChannels, PixelCount, NumBatchElems](int32 BatchId)
				{
					const int32 BatchBegin = BatchId * NumBatchElems;
					const int32 BatchEnd = FMath::Min(BatchBegin + NumBatchElems, PixelCount);
					for (int32 p = BatchBegin; p < BatchEnd; ++p)
					{
						pDestBuf[p * NumDestChannels] = 0;
					}
				};

				if (NumBatches == 1)
				{
					ProcessBatch(0);
				}
				else if (NumBatches > 1)
				{
					ParallelFor(NumBatches, ProcessBatch);
				}
			}
		}

		return pDest;
	}
	
}
