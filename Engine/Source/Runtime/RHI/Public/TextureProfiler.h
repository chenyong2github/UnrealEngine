// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"
#include "Templates/Function.h"
#include "Misc/CoreMisc.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

#define TEXTURE_PROFILER_ENABLED WITH_ENGINE && !UE_BUILD_SHIPPING

#if TEXTURE_PROFILER_ENABLED

class FRHITexture;

/**
* FTextureProfiler class. This manages recording and reporting texture allocations in the RHI
*/
class RHI_API FTextureProfiler
{
	static FTextureProfiler* Instance;
public:
	// Singleton interface
	static FTextureProfiler* Get();

	void Init();

	void DumpTextures(bool RenderTargets, bool CombineTextureNames, bool AsCSV, FOutputDevice& OutputDevice);

	void AddTextureAllocation(FRHITexture* UniqueTexturePtr, size_t Size, uint32 Alignment, size_t AllocationWaste);
	void UpdateTextureAllocation(FRHITexture* UniqueTexturePtr, size_t Size, uint32 Alignment, size_t AllocationWaste);
	void RemoveTextureAllocation(FRHITexture* UniqueTexturePtr);
	void UpdateTextureName(FRHITexture* UniqueTexturePtr);
private:

	FTextureProfiler() = default;
	FTextureProfiler(const FTextureProfiler&) = delete;
	FTextureProfiler(const FTextureProfiler&&) = delete;

	void Update();

	FCriticalSection TextureMapCS;
	struct FTexureDetails
	{
		FTexureDetails() = default;
		FTexureDetails(FRHITexture* Texture, size_t InSize, uint32 InAlign, size_t InAllocationWaste);

		void SetName(FName InTextureName);
		void ResetPeakSize();

		FTexureDetails& operator+=(const FTexureDetails& Other);
		FTexureDetails& operator-=(const FTexureDetails& Other);

		FName TextureName;
		char TextureNameString[NAME_SIZE];
		size_t Size = 0;
		size_t PeakSize = 0;
		uint32 Align = 0;
		size_t AllocationWaste = 0;
		int Count = 0;
		bool IsRenderTarget = false;
	};

	TMap<void*, FTexureDetails> TexturesMap;

	// Keep track of the totals separately to reduce the cost of rounding error for sizes
	FTexureDetails TotalTextureSize;
	FTexureDetails TotalRenderTargetSize;
	TMap<FName, FTexureDetails> CombinedTextureSizes;
	TMap<FName, FTexureDetails> CombinedRenderTargetSizes;
};
#endif //TEXTURE_PROFILER_ENABLED
