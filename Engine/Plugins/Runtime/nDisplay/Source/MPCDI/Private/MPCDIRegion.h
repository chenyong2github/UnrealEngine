// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDIBlendTexture.h"
#include "IMPCDI.h"

#include "RenderResource.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

class FMPCDIWarp;


namespace mpcdi
{
	struct Region;
};

struct FFrustumData // 3D Simulation and Shader Lamps Only
{
	double Yaw;       // Viewer Rotates head upwards. First Rotation.
	double Pitch;     // Viewer Rotates head clockwise. Second Rotation.
	double Roll;      // Viewer Rotates head rightwards. Third Rotation.

	double LeftAngle; // Field of View. Typically Negative (Degrees) 
	double RightAngle;// Field of View. Typically Positive (Degrees)
	double UpAngle;   // Field of View. Typically Positive (Degrees)
	double DownAngle; // Field of View. Typically Negative (Degrees)
};

struct FCoordinateFrameData // Shader Lamps Only
{
	double Posx,   Posy,   Posz;
	double Yawx,   Yawy,   Yawz;
	double Pitchx, Pitchy, Pitchz;
	double Rollx,  Rolly,  Rollz;
};

class FDebugMeshExporter;

struct FMPCDIRegion
{
	FString ID;

	float X;
	float Y;
	float W;
	float H;
		
	uint32_t ResX;
	uint32_t ResY;

	// SL data support
	//@ todo: add SL data support
	FFrustumData         Frustum;
	FCoordinateFrameData CoordinateFrame;

	// Position/Scale of the viewport in the output framebuffer - This is temp until we implement the divorce
	// Between input and output framebuffers
	//float OutX, OutY;
	//float OutW, OutH;

	bool isRuntimeData;

	FMPCDIBlendTexture  AlphaMap;
	FMPCDIBlendTexture  BetaMap;
	FMPCDIWarp*         WarpData;

	FMPCDIRegion()
		: isRuntimeData(false)
		, WarpData(nullptr)
	{ }

	FMPCDIRegion(const wchar_t* Name, int InW, int InH);

	~FMPCDIRegion();

	bool Load(mpcdi::Region* InMPCIDRegionData, const IMPCDI::EMPCDIProfileType ProfileType);

	void ReloadExternalFiles_RenderThread(bool bForceReload);

	bool SetStaticMeshWarp(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent);

	//Support ext loads:
	bool LoadExtGeometry(const TArray<FVector>& PFMPoints, int DimW, int DimH, const IMPCDI::EMPCDIProfileType ProfileType, const float WorldScale, bool bIsUnrealGameSpace);

	bool LoadExtPFMFile(const FString& LocalPFMFileName, const IMPCDI::EMPCDIProfileType ProfileType, const float PFMScale, bool bIsUnrealGameSpace);

	bool LoadExtAlphaMap(const FString& LocalPNGFileName, float GammaValue)
	{
		return LoadDataMap(LocalPNGFileName, GammaValue, EDataMapType::Alpha);  
	}

	bool LoadExtBetaMap(const FString& LocalPNGFileName)
	{
		return LoadDataMap(LocalPNGFileName, 1, EDataMapType::Beta); 
	}

	inline FMatrix GetRegionMatrix() const
	{
		FMatrix RegionMatrix = FMatrix::Identity;
		RegionMatrix.M[0][0] = W;
		RegionMatrix.M[1][1] = H;
		RegionMatrix.M[3][0] = X;
		RegionMatrix.M[3][1] = Y;
		return RegionMatrix;
	}

private:
	enum class EDataMapType : uint8
	{
		Alpha,
		Beta,
	};

	bool LoadDataMap(const FString& LocalPNGFileName, float GammaValue, EDataMapType DataType);

private:
	// Support runtime reload
	struct FExternalFileReloader
	{
		void      Initialize(const FString& FullPathFileName);
		void      SyncDateTime(); // Get current file modification date time
		void      Release();
		bool      IsChanged(bool bForceReload);

		const FString& operator()() const
		{
			return FileName;
		}

		FExternalFileReloader()
			: bIsEnabled(false)
			, FrameIndex(0)
		{ }

	private:
		FString   FileName;
		FDateTime DateTime;
		bool      bIsEnabled; // Enable reload
		int       FrameIndex;
	};

	struct FExtPFMFile {
		FExternalFileReloader     File;
		IMPCDI::EMPCDIProfileType ProfileType;
		float                     PFMScale;
		bool                      bIsUnrealGameSpace;
	};

	struct FExtBlendMapFile {
		FExternalFileReloader     File;
		float                     GammaValue;
		EDataMapType              DataType;
	};

private:
	bool LoadExtPFMFile(FExtPFMFile& PFMFile);
	bool LoadDataMap(FExtBlendMapFile& BlendMapFile);

private:
	FExtPFMFile      ExtPFMFile;
	FExtBlendMapFile ExtAlphaMap;
	FExtBlendMapFile ExtBetaMap;
};
