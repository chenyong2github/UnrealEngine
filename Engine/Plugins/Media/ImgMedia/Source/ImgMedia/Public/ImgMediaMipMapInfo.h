// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMediaOptions.h"
#include "Tickable.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UMeshComponent;
class UMediaTexture;

/**
 * Holds size, tiling and mips sequence information.
 */
struct FSequenceInfo
{
	/** Name of this sequence. */
	FName Name;
	/** Pixel dimensions of this sequence. */
	FIntPoint Dim;
	/** Number of tiles in the X, Y directions. */
	FIntPoint NumTiles;
	/** Number of mip levels. */
	int32 NumMipLevels;
	/** 
	* Check if the sequence has tiles.
	* 
	* @return True if the tile counts are above one.
	*/
	FORCEINLINE bool IsTiled() const
	{
		return (NumTiles.X > 1) || (NumTiles.Y > 1);
	}
};

/**
 * Holds info on a camera which we can use for mipmap calculations.
 */
struct FImgMediaMipMapCameraInfo
{
	/** Position of camera. */
	FVector Location;
	/** View matrix of camera. */
	FMatrix ViewMatrix;
	/** View projection matrix of camera. */
	FMatrix ViewProjectionMatrix;
	/** Active viewport size. */
	FIntRect ViewportRect;
	/** View mip bias. */
	float MaterialTextureMipBias;
};

/**
 * Describes which tiles are visible.
 */
struct FImgMediaTileSelection
{
	/** X position of the most top left visibile tile. */
	uint16 TopLeftX;
	/** Y position of the most top left visible tile. */
	uint16 TopLeftY;
	/**
	 * X position of the most bottom right visible tile + 1.
	 * If this is equal to TopLeftX, then this implies that the tile is not visible.
	 */
	uint16 BottomRightX;
	/** Y position of the most bottom right visible tile + 1. */
	uint16 BottomRightY;

	FImgMediaTileSelection()
	{
		SetAllNotVisible();
	}

	/**
	 * Marks all tiles as visible.
	 */
	void SetAllVisible()
	{
		TopLeftX = 0;
		TopLeftY = 0;
		BottomRightX = 0xffff;
		BottomRightY = 0xffff;
	}

	/**
	 * Marks all tiles as not visible.
	 */
	void SetAllNotVisible()
	{
		TopLeftX = 0xffff;
		TopLeftY = 0xffff;
		BottomRightX = 0;
		BottomRightY = 0;
	}

	/**
	 * Marks visible tile region.
	 * 
	 * @param InTopLeftX Top-left coordinate horizontal component.
	 * @param InTopLeftY Top-left coordinate vertical component.
	 * @param InBottomRightX Bottom-right coordinate horizontal component.
	 * @param InBottomRightY Bottom-rightcoordinate vertical component.
	 */
	void SetVisibleRegion(uint16 InTopLeftX, uint16 InTopLeftY, uint16 InBottomRightX, uint16 InBottomRightY)
	{
		TopLeftX = InTopLeftX;
		TopLeftY = InTopLeftY;
		BottomRightX = InBottomRightX;
		BottomRightY = InBottomRightY;
	}

	/**
	* See if this selection is visible.
	*
	* @return True if so.
	*/
	bool IsVisible() const { return (TopLeftX < BottomRightX); };


	/**
	 * Include a given tile coordinate to the current section region.
	 *
	 * @param TileCoordX Horizontal tile coordinate.
	 * @param TileCoordY Vertical tile coordinate.
	 */
	void Include(uint16 TileCoordX, uint16 TileCoordY)
	{
		TopLeftX = FMath::Min(TopLeftX, TileCoordX);
		TopLeftY = FMath::Min(TopLeftY, TileCoordY);
		BottomRightX = FMath::Max(BottomRightX, uint16(TileCoordX + 1u));
		BottomRightY = FMath::Max(BottomRightY, uint16(TileCoordY + 1u));
	}

	/**
	 * Check if the current selection contains a tile.
	 *
	 * @param TileCoordX Horizontal tile coordinate.
	 * @param TileCoordY Vertical tile coordinate.
	 * @return True if the coordinate is contained withing bounds, false otherwise.
	 */
	bool Contains(uint16 TileCoordX, uint16 TileCoordY) const
	{
		return TopLeftX <= TileCoordX &&
			TopLeftY <= TileCoordY &&
			BottomRightX > TileCoordX &&
			BottomRightY > TileCoordY;
	}

	/**
	 * Check if the current selection contains another selection within its bounds.
	 *
	 * @param Other Selection to compare.
	 * @return True if the other selection is contained withing bounds, false otherwise.
	 */
	bool Contains(const FImgMediaTileSelection& Other) const
	{
		return TopLeftX <= Other.TopLeftX &&
			TopLeftY <= Other.TopLeftY &&
			BottomRightX >= Other.BottomRightX &&
			BottomRightY >= Other.BottomRightY;
	}
};

/**
 * Describes a single object which is using our img sequence.
 * Base class for various objects such as planes or spheres.
 */
class FImgMediaMipMapObjectInfo
{
public:
	FImgMediaMipMapObjectInfo(UMeshComponent* InMeshComponent, float InLODBias = 0.0f);
	virtual ~FImgMediaMipMapObjectInfo() = default;

	/**
	 * Calculate visible tiles per mip level.
	 *
	 * @param InCameraInfos Active camera information
	 * @param InSequenceInfo Active image sequence information
	 * @param VisibleTiles Updated list of visible tiles per mip level
	 */
	virtual bool CalculateVisibleTiles(const TArray<FImgMediaMipMapCameraInfo>& InCameraInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const;

	/**
	 * Get the registered mesh component.
	 *
	 * @return Mesh component
	 */
	UMeshComponent* GetMeshComponent() const;
protected:
	/** Mesh component onto which the media is displayed. */
	TWeakObjectPtr<class UMeshComponent> MeshComponent;
	/** LOD bias for the mipmap level. */
	float LODBias;
};

/**
 * Contains information for working with mip maps.
 */
class FImgMediaMipMapInfo : public IMediaOptions::FDataContainer, public FTickableGameObject
{
public:
	FImgMediaMipMapInfo();
	virtual ~FImgMediaMipMapInfo();

	/**
	 * This object is using our img sequence.
	 *
	 * @param InActor Object using our img sequence.
	 * @param Width Width of the object. If < 0, then get the width automatically.
	 */
	void AddObject(AActor* InActor, float Width, float LODBias);

	/**
	 * This object is no longer using our img sequence.
	 *
	 * @param InActor Object no longer using our img sequence.
	 */
	void RemoveObject(AActor* InActor);

	/**
	 * All the objets that are using this media texture will be used in our mip map calculations.
	 *
	 * @param InMediaTexture Media texture to get objects from.
	 */
	void AddObjectsUsingThisMediaTexture(UMediaTexture* InMediaTexture);

	/**
	 * Remove all objects from consideration.
	 */
	void ClearAllObjects();

	/**
	 * Provide information on the texture needed for our image sequence.
	 *
	 * @param InSequenceName Name of this sequence.
	 * @param InNumMipMaps Number of mipmaps in our image sequence.
	 * @param InNumTiles Number of tiles in our image sequence.
	 * @param InSequenceDim Dimensions of the textures in our image sequence.
	 */
	void SetTextureInfo(FName InSequenceName, int32 InNumMipMaps, const FIntPoint& InNumTiles,
		const FIntPoint& InSequenceDim);

	/**
	 * Get what mipmap level should be used.
	 * Returns the lowest level (highest resolution) mipmap.
	 * Assumes all higher levels will also be used.
	 *
	 * @param TileSelection Will be set with which tiles are visible.
	 * @return Mipmap level.
	 */
	TMap<int32, FImgMediaTileSelection> GetVisibleTiles();
	
	/**
	 * Get information on all our cameras.
	 *
	 * @return Array of cameras.
	 */
	const TArray<FImgMediaMipMapCameraInfo>& GetCameraInfo() const { return CameraInfos; }

	/**
	 * Get information on objects that are using our textures.
	 * 
	 * @return Array of objects.
	 */
	const TArray<FImgMediaMipMapObjectInfo*>& GetObjects() const { return Objects; }

	/**
	 * Check if any scene objects are using our img sequence.
	 *
	 * @return True if any object is active.
	 */
	bool HasObjects() const { return Objects.Num() > 0; }

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FImgMediaMipMapInfo, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

protected:
	/**
	 * Performs mipmap calculations and caches the data.
	 */
	void UpdateMipLevelCache();

	/** Array of objects that are using our img sequence. */
	TArray<FImgMediaMipMapObjectInfo*> Objects;

	/** Info for each camera, used in mipmap calculations. */
	TArray<FImgMediaMipMapCameraInfo> CameraInfos;

	/** Size, tiling and mips sequence information. */
	FSequenceInfo SequenceInfo;
	
	/** Protects info variables. */
	FCriticalSection InfoCriticalSection;

	/** Desired mipmap level at this current time. */
	TMap<int32, FImgMediaTileSelection> CachedVisibleTiles;

	/** True if the cached mipmap data has been calculated this frame. */
	bool bIsCacheValid;
};
