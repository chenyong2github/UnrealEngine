// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RHI.h"

// @todo: we need builds for OpenVDB for platforms other than windows
#if PLATFORM_WINDOWS
#include "NiagaraOpenVDB.h"
#endif

#include "VolumeCache.generated.h"

class FVolumeCacheData;

UENUM()
enum class EVolumeCacheType : uint8
{
	OpenVDB
};

UCLASS(Experimental)
class NIAGARA_API UVolumeCache : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** File path to load */
	UPROPERTY(EditAnywhere, Category=File, meta=(DisplayName="File Path"))
	FString FilePath;
	
	UPROPERTY(EditAnywhere, Category = File, meta = (DisplayName = "Cache Type"))
	EVolumeCacheType CacheType;

	UPROPERTY(EditAnywhere, Category = File, meta = (DisplayName = "Resolution"))
	FIntVector Resolution;

	UPROPERTY(EditAnywhere, Category = File, meta = (DisplayName = "Frame Range Start"))
	int32 FrameRangeStart;	

	UPROPERTY(EditAnywhere, Category = File, meta = (DisplayName = "Frame Range End"))
	int32 FrameRangeEnd;
		
	void InitData();

	bool LoadFile(int frame);
	bool UnloadFile(int frame);		
	bool LoadRange();
	void UnloadAll();	
	
	TSharedPtr<FVolumeCacheData> GetData() { return CachedVolumeFiles;  }

	// @todo: high level fill volume texture method that works on GT calls
	// bool Fill3DTexture(int frame, FTextureRHIRef TextureToFill, FRHICommandListImmediate& RHICmdList);
	
private:
	TSharedPtr<FVolumeCacheData> CachedVolumeFiles;
};

class NIAGARA_API FVolumeCacheData
{
public:		
	FVolumeCacheData() : DenseResolution(-1, -1, -1) {}
	virtual ~FVolumeCacheData() {}

	FString GetAssetPath(FString PathFormat, int32 FrameIndex) const;

	FIntVector GetDenseResolution() { return DenseResolution;  }

	virtual void Init(FIntVector Resolution) = 0;
	virtual bool LoadFile(FString Path, int frame) = 0;
	virtual bool UnloadFile(int frame) = 0;
	virtual bool LoadRange(FString Path, int Start, int End) = 0;
	virtual void UnloadAll() = 0;
	virtual bool Fill3DTexture_RenderThread(int frame, FTextureRHIRef TextureToFill, FRHICommandListImmediate& RHICmdList) = 0;

protected:
	FIntVector DenseResolution;
};

// @todo: we need builds for OpenVDB for platforms other than windows
#if PLATFORM_WINDOWS
class NIAGARA_API FOpenVDBCacheData : public FVolumeCacheData
{
public:
	FOpenVDBCacheData() {}
	
	virtual ~FOpenVDBCacheData() 
	{		
		OpenVDBGrids.Reset();
		DenseGridPtr = nullptr;
	}

	virtual void Init(FIntVector Resolution);
	virtual bool LoadFile(FString Path, int frame);
	virtual bool UnloadFile(int frame);
	virtual bool LoadRange(FString Path, int Start, int End);
	virtual void UnloadAll();
	virtual bool Fill3DTexture_RenderThread(int frame, FTextureRHIRef TextureToFill, FRHICommandListImmediate& RHICmdList);

	static bool WriteImageDataToOpenVDBFile(FStringView FilePath, FIntVector ImageSize, TArrayView<FFloat16Color> ImageData, bool UseFloatGrids = false);

private:	
	TMap<int32, Vec4SGrid::Ptr> OpenVDBGrids;
	openvdb::tools::Dense<openvdb::Vec4s, openvdb::tools::MemoryLayout::LayoutXYZ>::Ptr DenseGridPtr;	
};
#endif