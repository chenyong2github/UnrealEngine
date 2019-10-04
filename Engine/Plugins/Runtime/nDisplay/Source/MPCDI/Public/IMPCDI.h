// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

class FMPCDIData;

class IMPCDI : public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("MPCDI");

public:

	enum EMPCDIProfileType: uint8
	{
		mpcdi_2D = 0, // 2D mode
		mpcdi_3D,     // 3D mode
		mpcdi_A3D,    // Advanced 3D mode
		mpcdi_SL,     // Shader lamps
		Invalid,
	};

	struct ConfigParser
	{
		FString  ConfigLineStr;// Saved viewport config line string
		FString  MPCDIFileName; // Single mpcdi file name

		FString  BufferId;
		FString  RegionId;

		FString  OriginType;

		// Support external pfm (warp)  and png(blend) files
		EMPCDIProfileType  MPCDIType;

		FString  PFMFile;
		float    PFMFileScale;
		bool     bIsUnrealGameSpace;

		FString  AlphaFile;
		float AlphaGamma;

		FString  BetaFile;

		inline bool IsExtConfig() const
		{ 
			return !PFMFile.IsEmpty(); 
		}
	};

	struct FRegionLocator
	{
		// MPCDI data indices
		int FileIndex   = -1;
		int BufferIndex = -1;
		int RegionIndex = -1;

		inline bool isValid() const
		{ 
			return FileIndex >= 0 && BufferIndex >= 0 && RegionIndex >= 0; 
		}
	};

	struct FFrustum
	{
		// Frustum projection angles
		struct FAngles
		{
			float Left, Right, Top, Bottom;
			FAngles()
			{
			};

			FAngles(float top, float bottom, float left, float right) : Left(left), Right(right), Top(top), Bottom(bottom)
			{
			};
		};

		// Frustum origins Input data:
		FVector OriginLocation;  // Current camera Origin location
		FVector OriginEyeOffset; // Offset from OriginLocation to Eye view location

		// Output runtime calc data: 
		FAngles ProjectionAngles;

		// Warp mesh aabb points
		FVector AABBoxPts[8];

		// Frustum projection matrix
		FMatrix  ProjectionMatrix;

		// Viewport Size forthis view
		FIntPoint ViewportSize;

		// Local2World
		FMatrix  Local2WorldMatrix;

		// From the texture's perspective
		FMatrix  UVMatrix;

		float    WorldScale;
		bool     bIsValid;

		inline FVector GetEyeLocation() const
		{
			return OriginLocation + OriginEyeOffset;
		}

		inline bool IsEyeLocationEqual(const FFrustum& InFrustum, float Precision) const
		{
			return (GetEyeLocation() - InFrustum.GetEyeLocation()).Size() < Precision;
		}

		FFrustum()
			: FFrustum(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f))
		{ 
		}

		FFrustum(const FVector &InOriginLocation, const FVector& InOriginEyeOffset)
			: OriginLocation(InOriginLocation)
			, OriginEyeOffset(InOriginEyeOffset)
			, ProjectionMatrix(FMatrix::Identity)
			, UVMatrix(FMatrix::Identity)
			, WorldScale(1.f)
			, bIsValid(false)
		{ 
		}
	};

	struct FTextureWarpData
	{
		FRHITexture2D* SrcTexture; // Source texture (shader render)
		FRHITexture2D* DstTexture; // Render target destination

		FIntRect SrcRect; // Source texture region
		FIntRect DstRect; // Destination texture region
	};

	struct FShaderInputData
	{
		FRegionLocator RegionLocator;

		FFrustum Frustum;

		// Projection matrix from warp to screen space
		FMatrix UVMatrix;

		FVector EyePosition;
		FVector EyeLookAt;
		float   VignetteEV;

#if 0
		//Cubemape support
		FTextureRHIRef SceneCubeMap;
#endif
	};

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMPCDI& Get()
	{
		return FModuleManager::LoadModuleChecked<IMPCDI>(IMPCDI::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IMPCDI::ModuleName);
	}

public:
	/**
	* Loads an MPCDI file to the cache
	*
	* @param MPCDIFile - file path
	*
	* @return - true if success
	*/
	virtual bool Load(const FString& MPCDIFile) = 0;

	/**
	* Checks if specified MPCDI file has been loaded already
	*
	* @param MPCDIFile - file path
	*
	* @return - true if loaded already
	*/
	virtual bool IsLoaded(const FString& MPCDIFile) = 0;

	/**
	* Returns region locator
	*
	* @param MPCDIFile  - file path
	* @param BufferName - buffer name within a specified file
	* @param RegionName - region name within a specified buffer
	* @param OutRegionLocator - (output) region locator
	*
	* @return - true if success
	*/
	virtual bool GetRegionLocator(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) = 0;

	/**
	* Computes view frustum
	*
	* @param RegionLocator - locator of an MPCDI region
	* @param WorldScale    - world scale
	* @param ZNear         - near culling plane
	* @param ZFar          - far culling plane
	* @param InOutFrustum  - (in/out) frustum
	*
	* @return - true if success
	*/
	virtual bool ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, float WorldScale, float ZNear, float ZFar, IMPCDI::FFrustum& InOutFrustum) = 0;

	/**
	* Applies warp&blend changes
	*
	* @param RHICmdList      - RHI command list
	* @param TextureWarpData - texture warp data
	* @param ShaderInputData - shader data
	*
	* @return - true if success
	*/
	virtual bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData) = 0;

	/**
	* Return MPCDI data for custum warpblend shader 
	*
	* @param ShaderInputData - shader data
	*
	* @return - ptr if success
	*/
	virtual TSharedPtr<FMPCDIData> GetMPCDIData(IMPCDI::FShaderInputData& ShaderInputData) = 0;

	//! Support ext warp&blend calibration files
	/**
	* Create new mpcdi file + buffer + region (region is empty, for custom setup)
	*
	* @param MPCDIFile  - Fullpath unique mpcdi file name
	* @param BufferName - Buffer unique name
	* @param RegionName - region unique name
	* @param OutRegionLocator - return unique region identifier
	*
	* @return - true if success
	*           false, if region already exist
	*/
	virtual bool CreateCustomRegion(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) = 0;

	/**
	* Change mpcdi profile type for custom warpblend (for owned mpcdi file)
	*
	* @param InRegionLocator - region locator
	*
	* @return - true if success
	*/
	virtual bool SetMPCDIProfileType(const IMPCDI::FRegionLocator& InRegionLocator, const EMPCDIProfileType ProfileType) = 0;

	/**
	* Load warp map data from external PFM file, used custom scale and axis orientation (by default asis in mpcdi orientation)
	*
	* @param InRegionLocator - region locator
	* @param PFMFile - full path to PFM file
	* @param WorldScale - world scale multiplier to UE4 game (value=100 from meters->sm)
	* @param bIsUnrealGameSpace - let=true, if you dont want to apply axis conversion from mpcdi space
	*
	* @return - true if success
	*/
	virtual bool LoadPFM(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PFMFile, const float WorldScale, bool bIsUnrealGameSpace=false) = 0;
	
	/**
	* Load warp map data from array, used custom scale and axis orientation (by default asis in mpcdi orientation)
	*
	* @param InRegionLocator - region locator
	* @param PFMPoints - PFM points, row-by-row
	* @param DimW - Row width
	* @param DimH - Rows count
	* @param WorldScale - world scale multiplier to UE4 game (value=100 from meters->sm)
	* @param bIsUnrealGameSpace - let=true, if you dont want to apply axis conversion from mpcdi space
	*
	* @return - true if success
	*/
	virtual bool LoadPFMGeometry(const IMPCDI::FRegionLocator& InRegionLocator, const TArray<FVector>& PFMPoints, int DimW, int DimH, const float WorldScale, bool bIsUnrealGameSpace = false) = 0;

	/**
	* Load alpha map from external PNG file
	*
	* @param InRegionLocator - region locator
	* @param PFMFile - full path to PNG file
	* @param GammaValue - alpha map gamme value
	*
	* @return - true if success
	*/
	virtual bool LoadAlphaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PNGFile, float GammaValue) = 0;

	/**
	* Load beta map from external PNG file
	*
	* @param InRegionLocator - region locator
	* @param PFMFile - full path to PNG file	
	*
	* @return - true if success
	*/
	virtual bool LoadBetaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PNGFile) = 0;


	/**
	* Helper. Load config data from string
	*
	* @param InConfigLineStr - config string
	* @param OutCfgData - result condig data 
	*
	* @return - true if success
	*/
	virtual bool LoadConfig(const FString& InConfigLineStr, ConfigParser& OutCfgData) = 0;

	/**
	* Helper. Load or create mpcdi data
	*
	* @param CfgData - Initialized config dta for viewport
	* @param OutRegionLocator - region locator
	*
	* @return - true if success
	*/
	virtual bool Load(const ConfigParser& CfgData, IMPCDI::FRegionLocator& OutRegionLocator) = 0;

	/**
	* Helper. Reload all data from changed external files
	*
	*/
	virtual void ReloadAll() = 0;
	virtual void ReloadAll_RenderThread() = 0;
};
