// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/*=============================================================================
	Texture.h: Unreal texture related classes.
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/IndirectArray.h"
#include "Stats/Stats.h"
#include "Containers/List.h"
#include "Async/AsyncWork.h"
#include "Async/AsyncFileHandle.h"
#include "RHI.h"
#include "RenderResource.h"
#include "Serialization/BulkData.h"
#include "Engine/TextureDefines.h"
#include "UnrealClient.h"
#include "Templates/UniquePtr.h"
#include "VirtualTexturing.h"

class FTexture2DResourceMem;
class UTexture;
class UTexture2D;
class UTexture2DArray;
class IVirtualTexture;
struct FTexturePlatformData;
class FStreamableTextureResource;
class FTexture2DResource;
class FTexture3DResource;
class FTexture2DArrayResource;

/** Maximum number of slices in texture source art. */
#define MAX_TEXTURE_SOURCE_SLICES 6

/**
 * A 2D texture mip-map.
 */
struct FTexture2DMipMap
{
	/** Width of the mip-map. */
	int32 SizeX;
	/** Height of the mip-map. */
	int32 SizeY;
	/** Depth of the mip-map. */
	int32 SizeZ;

	/** Bulk data if stored in the package. */
	FByteBulkData BulkData;

	/** Default constructor. */
	FTexture2DMipMap()
		: SizeX(0)
		, SizeY(0)
		, SizeZ(0)
	{
	}

	/** Serialization. */
	ENGINE_API void Serialize(FArchive& Ar, UObject* Owner, int32 MipIndex);

#if WITH_EDITORONLY_DATA
	/** Key if stored in the derived data cache. */
	FString DerivedDataKey;

	/** The file region type appropriate for this mip's pixel format. */
	EFileRegionType FileRegionType = EFileRegionType::None;

	/**
	 * Place mip-map data in the derived data cache associated with the provided
	 * key.
	 */
	uint32 StoreInDerivedDataCache(const FString& InDerivedDataKey, const FStringView& TextureName, bool bReplaceExistingDDC);
#endif // #if WITH_EDITORONLY_DATA
};

/** 
 * The rendering resource which represents a texture.
 */
class FTextureResource : public FTexture
{
public:

	FTextureResource() {}
	virtual ~FTextureResource() {}

	/**
	* Returns true if the resource is proxying another one.
	*/
	virtual bool IsProxy() const { return false; }

	// Dynamic cast methods.
	ENGINE_API virtual FTexture2DResource* GetTexture2DResource() { return nullptr; }
	ENGINE_API virtual FTexture3DResource* GetTexture3DResource() { return nullptr; }
	ENGINE_API virtual FTexture2DArrayResource* GetTexture2DArrayResource() { return nullptr; }
	ENGINE_API virtual FStreamableTextureResource* GetStreamableTextureResource() { return nullptr; }
	// Dynamic cast methods (const).
	ENGINE_API virtual const FTexture2DResource* GetTexture2DResource() const { return nullptr; }
	ENGINE_API virtual const FTexture3DResource* GetTexture3DResource() const { return nullptr; }
	ENGINE_API virtual const FTexture2DArrayResource* GetTexture2DArrayResource() const { return nullptr; }
	ENGINE_API virtual const FStreamableTextureResource* GetStreamableTextureResource() const { return nullptr; }

	// Current mip count. We use "current" to specify that it is not computed from SizeX() which is the size when fully streamed in.
	FORCEINLINE int32 GetCurrentMipCount() const
	{
		return TextureRHI.IsValid() ? TextureRHI->GetNumMips() : 0;
	}

	FORCEINLINE bool IsTextureRHIPartiallyResident() const
	{
		return TextureRHI.IsValid() && !!(TextureRHI->GetFlags() & TexCreate_Virtual);
	}

	FORCEINLINE FRHITexture2D* GetTexture2DRHI() const
	{
		return TextureRHI.IsValid() ? TextureRHI->GetTexture2D() : nullptr;
	}

	FORCEINLINE FRHITexture3D* GetTexture3DRHI() const
	{
		return TextureRHI.IsValid() ? TextureRHI->GetTexture3D() : nullptr;
	}

	FORCEINLINE FRHITexture2DArray* GetTexture2DArrayRHI() const
	{
		return TextureRHI.IsValid() ? TextureRHI->GetTexture2DArray() : nullptr;
	}

	void SetTextureReference(FRHITextureReference* TextureReference)
	{
		TextureReferenceRHI = TextureReference;
	}

#if STATS
	/* The Stat_ FName corresponding to each TEXTUREGROUP */
	static FName TextureGroupStatFNames[TEXTUREGROUP_MAX];
#endif

protected :
	// A FRHITextureReference to update whenever the FTexture::TextureRHI changes.
	// It allows to prevent dereferencing the UAsset pointers when updating a texture resource.
	FTextureReferenceRHIRef TextureReferenceRHI;
};

class FVirtualTexture2DResource : public FTextureResource
{
public:
	FVirtualTexture2DResource(const UTexture2D* InOwner, struct FVirtualTextureBuiltData* InVTData, int32 FirstMipToUse);
	virtual ~FVirtualTexture2DResource();

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

#if WITH_EDITOR
	void InitializeEditorResources(class IVirtualTexture* InVirtualTexture);
#endif

	virtual uint32 GetSizeX() const override;
	virtual uint32 GetSizeY() const override;

	const FVirtualTextureProducerHandle& GetProducerHandle() const { return ProducerHandle; }

	/**
	 * FVirtualTexture2DResource may have an AllocatedVT, which represents a page table allocation for the virtual texture.
	 * VTs used by materials generally don't need their own allocation, since the material has its own page table allocation for each VT stack.
	 * VTs used as lightmaps need their own allocation.  Also VTs open in texture editor will have a temporary allocation.
	 * GetAllocatedVT() will return the current allocation if one exists.
	 * AcquireAllocatedVT() will make a new allocation if needed, and return it.
	 * ReleaseAllocatedVT() will free any current allocation.
	 */
	class IAllocatedVirtualTexture* GetAllocatedVT() const { return AllocatedVT; }
	ENGINE_API class IAllocatedVirtualTexture* AcquireAllocatedVT();
	ENGINE_API void ReleaseAllocatedVT();

	ENGINE_API EPixelFormat GetFormat(uint32 LayerIndex) const;
	ENGINE_API FIntPoint GetSizeInBlocks() const;
	ENGINE_API uint32 GetNumTilesX() const;
	ENGINE_API uint32 GetNumTilesY() const;
	ENGINE_API uint32 GetNumMips() const;
	ENGINE_API uint32 GetNumLayers() const;
	ENGINE_API uint32 GetTileSize() const; //no borders
	ENGINE_API uint32 GetBorderSize() const;
	uint32 GetAllocatedvAddress() const;

	ENGINE_API FIntPoint GetPhysicalTextureSize(uint32 LayerIndex) const;

private:
	class IAllocatedVirtualTexture* AllocatedVT;
	struct FVirtualTextureBuiltData* VTData;
	const UTexture2D* TextureOwner;
	FVirtualTextureProducerHandle ProducerHandle;
	int32 FirstMipToUse;
};

/** A dynamic 2D texture resource. */
class FTexture2DDynamicResource : public FTextureResource
{
public:
	/** Initialization constructor. */
	ENGINE_API FTexture2DDynamicResource(class UTexture2DDynamic* InOwner);

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override;

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override;

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	ENGINE_API virtual void InitRHI() override;

	/** Called when the resource is released. This is only called by the rendering thread. */
	ENGINE_API virtual void ReleaseRHI() override;

	/** Returns the Texture2DRHI, which can be used for locking/unlocking the mips. */
	ENGINE_API FTexture2DRHIRef GetTexture2DRHI();

private:
	/** The owner of this resource. */
	class UTexture2DDynamic* Owner;
	/** Texture2D reference, used for locking/unlocking the mips. */
	FTexture2DRHIRef Texture2DRHI;
};

/**
 * FDeferredUpdateResource for resources that need to be updated after scene rendering has begun
 * (should only be used on the rendering thread)
 */
class FDeferredUpdateResource
{
public:

	/**
	 * Constructor, initializing UpdateListLink.
	 */
	FDeferredUpdateResource()
		:	UpdateListLink(NULL)
		,	bOnlyUpdateOnce(false)
	{ }

public:

	/**
	 * Iterate over the global list of resources that need to
	 * be updated and call UpdateResource on each one.
	 */
	ENGINE_API static void UpdateResources( FRHICommandListImmediate& RHICmdList );

	/**
	 * Performs a deferred resource update on this resource if it exists in the UpdateList.
	 */
	ENGINE_API void FlushDeferredResourceUpdate( FRHICommandListImmediate& RHICmdList );

	/** 
	 * This is reset after all viewports have been rendered
	 */
	static void ResetNeedsUpdate()
	{
		bNeedsUpdate = true;
	}

protected:

	/**
	 * Updates (resolves) the render target texture.
	 * Optionally clears the contents of the render target to green.
	 * This is only called by the rendering thread.
	 */
	virtual void UpdateDeferredResource( FRHICommandListImmediate& RHICmdList, bool bClearRenderTarget=true ) = 0;

	/**
	 * Add this resource to deferred update list
	 * @param OnlyUpdateOnce - flag this resource for a single update if true
	 */
	ENGINE_API void AddToDeferredUpdateList( bool OnlyUpdateOnce );

	/**
	 * Remove this resource from deferred update list
	 */
	ENGINE_API void RemoveFromDeferredUpdateList();

private:

	/** 
	 * Resources can be added to this list if they need a deferred update during scene rendering.
	 * @return global list of resource that need to be updated. 
	 */
	static TLinkedList<FDeferredUpdateResource*>*& GetUpdateList();
	/** This resource's link in the global list of resources needing clears. */
	TLinkedList<FDeferredUpdateResource*> UpdateListLink;
	/** if true then UpdateResources needs to be called */
	ENGINE_API static bool bNeedsUpdate;
	/** if true then remove this resource from the update list after a single update */
	bool bOnlyUpdateOnce;
};

/**
 * FTextureResource type for render target textures.
 */
class FTextureRenderTargetResource : public FTextureResource, public FRenderTarget, public FDeferredUpdateResource
{
public:
	/**
	 * Constructor, initializing ClearLink.
	 */
	FTextureRenderTargetResource()
	{}

	/** 
	 * Return true if a render target of the given format is allowed
	 * for creation
	 */
	ENGINE_API static bool IsSupportedFormat( EPixelFormat Format );

	// FTextureRenderTargetResource interface
	
	virtual class FTextureRenderTarget2DResource* GetTextureRenderTarget2DResource()
	{
		return NULL;
	}
	virtual void ClampSize(int32 SizeX,int32 SizeY) {}

	// FRenderTarget interface.
	virtual uint32 GetSizeX() const = 0;
	virtual uint32 GetSizeY() const = 0;
	virtual FIntPoint GetSizeXY() const = 0;

	/** 
	 * Render target resource should be sampled in linear color space
	 *
	 * @return display gamma expected for rendering to this render target 
	 */
	virtual float GetDisplayGamma() const;
};

/**
 * FTextureResource type for 2D render target textures.
 */
class FTextureRenderTarget2DResource : public FTextureRenderTargetResource
{
public:
	
	/** 
	 * Constructor
	 * @param InOwner - 2d texture object to create a resource for
	 */
	FTextureRenderTarget2DResource(const class UTextureRenderTarget2D* InOwner);

	FORCEINLINE FLinearColor GetClearColor()
	{
		return ClearColor;
	}

	// FTextureRenderTargetResource interface

	/** 
	 * 2D texture RT resource interface 
	 */
	virtual class FTextureRenderTarget2DResource* GetTextureRenderTarget2DResource() override
	{
		return this;
	}

	/**
	 * Clamp size of the render target resource to max values
	 *
	 * @param MaxSizeX max allowed width
	 * @param MaxSizeY max allowed height
	 */
	virtual void ClampSize(int32 SizeX,int32 SizeY) override;
	
	// FRenderResource interface.

	/**
	 * Initializes the dynamic RHI resource and/or RHI render target used by this resource.
	 * Called when the resource is initialized, or when reseting all RHI resources.
	 * Resources that need to initialize after a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void InitDynamicRHI() override;

	/**
	 * Releases the dynamic RHI resource and/or RHI render target resources used by this resource.
	 * Called when the resource is released, or when reseting all RHI resources.
	 * Resources that need to release before a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void ReleaseDynamicRHI() override;

	// FDeferredClearResource interface

	// FRenderTarget interface.
	/** 
	 * @return width of the target
	 */
	virtual uint32 GetSizeX() const override;

	/** 
	 * @return height of the target
	 */
	virtual uint32 GetSizeY() const override;

	/**
	 * @return dimensions of the target
	 */
	virtual FIntPoint GetSizeXY() const override;

	/** 
	 * Render target resource should be sampled in linear color space
	 *
	 * @return display gamma expected for rendering to this render target 
	 */
	virtual float GetDisplayGamma() const override;

	/** 
	 * @return TextureRHI for rendering 
	 */
	FTexture2DRHIRef GetTextureRHI() { return Texture2DRHI; }
protected:
	/**
	 * Updates (resolves) the render target texture.
	 * Optionally clears the contents of the render target to green.
	 * This is only called by the rendering thread.
	 */
	friend class UTextureRenderTarget2D;
	virtual void UpdateDeferredResource(FRHICommandListImmediate& RHICmdList, bool bClearRenderTarget=true) override;
	void Resize(int32 NewSizeX, int32 NewSizeY);

private:
	/** The UTextureRenderTarget2D which this resource represents. */
	const class UTextureRenderTarget2D* Owner;
	/** Texture resource used for rendering with and resolving to */
	FTexture2DRHIRef Texture2DRHI;
	/** the color the texture is cleared to */
	FLinearColor ClearColor;
	EPixelFormat Format;
	int32 TargetSizeX,TargetSizeY;
	TRefCountPtr<IPooledRenderTarget> MipGenerationCache;
};

/**
 * FTextureResource type for cube render target textures.
 */
class FTextureRenderTargetCubeResource : public FTextureRenderTargetResource
{
public:

	/** 
	 * Constructor
	 * @param InOwner - cube texture object to create a resource for
	 */
	FTextureRenderTargetCubeResource(const class UTextureRenderTargetCube* InOwner)
		:	Owner(InOwner)
	{
	}

	/** 
	 * Cube texture RT resource interface 
	 */
	virtual class FTextureRenderTargetCubeResource* GetTextureRenderTargetCubeResource()
	{
		return this;
	}

	/**
	 * Initializes the dynamic RHI resource and/or RHI render target used by this resource.
	 * Called when the resource is initialized, or when reseting all RHI resources.
	 * Resources that need to initialize after a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void InitDynamicRHI() override;

	/**
	 * Releases the dynamic RHI resource and/or RHI render target resources used by this resource.
	 * Called when the resource is released, or when reseting all RHI resources.
	 * Resources that need to release before a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void ReleaseDynamicRHI() override;

	// FRenderTarget interface.

	/** 
	 * @return width of the target
	 */
	virtual uint32 GetSizeX() const override;

	/** 
	 * @return height of the target
	 */
	virtual uint32 GetSizeY() const override;

	/**
	 * @return dimensions of the target
	 */
	virtual FIntPoint GetSizeXY() const override;

	/** 
	 * @return TextureRHI for rendering 
	 */
	FTextureCubeRHIRef GetTextureRHI() { return TextureCubeRHI; }

	/** 
	* Render target resource should be sampled in linear color space
	*
	* @return display gamma expected for rendering to this render target 
	*/
	float GetDisplayGamma() const override;

	/**
	* Copy the texels of a single face of the cube into an array.
	* @param OutImageData - float16 values will be stored in this array.
	* @param InFlags - read flags. ensure cubeface member has been set.
	* @param InRect - Rectangle of texels to copy.
	* @return true if the read succeeded.
	*/
	ENGINE_API bool ReadPixels(TArray< FColor >& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect = FIntRect(0, 0, 0, 0));

	/**
	* Copy the texels of a single face of the cube into an array.
	* @param OutImageData - float16 values will be stored in this array.
	* @param InFlags - read flags. ensure cubeface member has been set.
	* @param InRect - Rectangle of texels to copy.
	* @return true if the read succeeded.
	*/
	ENGINE_API bool ReadPixels(TArray<FFloat16Color>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect = FIntRect(0, 0, 0, 0));

protected:
	/**
	* Updates (resolves) the render target texture.
	* Optionally clears each face of the render target to green.
	* This is only called by the rendering thread.
	*/
	virtual void UpdateDeferredResource(FRHICommandListImmediate& RHICmdList, bool bClearRenderTarget=true) override;

private:
	/** The UTextureRenderTargetCube which this resource represents. */
	const class UTextureRenderTargetCube* Owner;
	/** Texture resource used for rendering with and resolving to */
	FTextureCubeRHIRef TextureCubeRHI;
	/** Target surfaces for each cube face */
	FTexture2DRHIRef CubeFaceSurfaceRHI;

	/** Represents the current render target (from one of the cube faces)*/
	FTextureCubeRHIRef RenderTargetCubeRHI;

	/** Face currently used for target surface */
	ECubeFace CurrentTargetFace;
};

/** Gets the name of a format for the given LayerIndex */
ENGINE_API FName GetDefaultTextureFormatName( const class ITargetPlatform* TargetPlatform, const class UTexture* Texture, int32 LayerIndex, bool bSupportDX11TextureFormats, bool bSupportCompressedVolumeTexture = false, int32 BlockSize = 4);

/** Gets an array of format names for each layer in the texture */
ENGINE_API void GetDefaultTextureFormatNamePerLayer(TArray<FName>& OutFormatNames, const class ITargetPlatform* TargetPlatform, const class UTexture* Texture, bool bSupportDX11TextureFormats, bool bSupportCompressedVolumeTexture = false, int32 BlockSize = 4);

// returns all the texture formats which can be returned by GetDefaultTextureFormatName
ENGINE_API void GetAllDefaultTextureFormats( const class ITargetPlatform* TargetPlatform, TArray<FName>& OutFormats, bool bSupportDX11TextureFormats);
