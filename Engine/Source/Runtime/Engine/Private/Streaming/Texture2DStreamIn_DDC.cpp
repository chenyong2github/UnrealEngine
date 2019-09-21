// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_DDC.cpp: Stream in helper for 2D textures loading DDC files.
=============================================================================*/

#include "Streaming/Texture2DStreamIn_DDC.h"
#include "RenderUtils.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA

static int32 GStreamingDDCPendingSleep = 5;
static FAutoConsoleVariableRef CVarStreamingDDCPendingSleep(
	TEXT("r.Streaming.DDCPendingSleep"),
	GStreamingDDCPendingSleep,
	TEXT("Sleep increment while waiting for ddc to be built in milliseconds. 0 to wait synchronously. Allows to abort streaming on cancel and suspend rendering thread. (default=5) \n"),
	ECVF_Default
);

void FTexture2DStreamIn_DDC::DoLoadNewMipsFromDDC(const FContext& Context)
{
	if (Context.Texture && Context.Resource)
	{
		const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
		const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();
		const FTexture2DRHIRef Texture2DRHI = Context.Resource->GetTexture2DRHI();

		for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip && !IsCancelled(); ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = OwnerMips[MipIndex];
			const int32 ExpectedMipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Texture2DRHI->GetFormat(), 0);

			check(MipData[MipIndex]);

			if (!MipMap.DerivedDataKey.IsEmpty())
			{
				// The overhead of doing 2 copy of each mip data (from GetSynchronous() and FMemoryReader) in hidden by other texture DDC ops happening at the same time.
				TArray<uint8> DerivedMipData;
				
				bool bDDCReady = false; // Whether the DDC is ready to be red.
				bool bDDCValid= false; // Whether the DDC is read data is good.

				if (GStreamingDDCPendingSleep > 0)
				{
					const uint32 AsyncHandle = GetDerivedDataCacheRef().GetAsynchronous(*MipMap.DerivedDataKey);
					while (!IsCancelled() && !IsAssetStreamingSuspended() && !bDDCReady)
					{
						bDDCReady = GetDerivedDataCacheRef().PollAsynchronousCompletion(AsyncHandle);
						if (!bDDCReady)
						{
							FPlatformProcess::Sleep(GStreamingDDCPendingSleep * 0.001f);
						}
					}
					if (bDDCReady)
					{
						bDDCValid = GetDerivedDataCacheRef().GetAsynchronousResults(AsyncHandle, DerivedMipData);
					}
				}
				else // sync mode (this can stall in suspend rendering thread and GC)
				{
					bDDCReady = true;
					bDDCValid = GetDerivedDataCacheRef().GetSynchronous(*MipMap.DerivedDataKey, DerivedMipData);
				}

				if (!bDDCReady)
				{
					MarkAsCancelled();
				}
				else if (bDDCValid)
				{
					FMemoryReader Ar(DerivedMipData, true);

					int32 MipSize = 0;
					Ar << MipSize;
					if (MipSize == ExpectedMipSize)
					{
						Ar.Serialize(MipData[MipIndex], MipSize);
					}
					else
					{
						UE_LOG(LogTexture, Error, TEXT("DDC mip size (%d) not as expected."), MipIndex);
						MarkAsCancelled();
						bDDCIsInvalid = true;
					}
				}
				else
				{
					// UE_LOG(LogTexture, Warning, TEXT("Failed to stream mip data from the derived data cache for %s. Streaming mips will be recached."), Context.Texture->GetPathName() );
					MarkAsCancelled();
					bDDCIsInvalid = true;
				}
			}
			else
			{
				UE_LOG(LogTexture, Error, TEXT("DDC key missing."));
				MarkAsCancelled();
			}
		}
		FPlatformMisc::MemoryBarrier();
	}
}

#endif