// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"

#include "MPCDITypes.h"
#include "MPCDIRegion.h"

#include "RendererInterface.h"
#include "UObject/GCObject.h"
#include "RenderResource.h"
#include "SceneTypes.h"


// Engine-side interface to FMPCDIData. We can only call into interfaces from the Engine module since Renderer is intentionally not a dependency of it.
class IMPCDIData
{
public:
	virtual ~IMPCDIData() = 0
	{ }

public:
	virtual bool IsCubeMapEnabled() const = 0;
};


class FMPCDIData
	: public FGCObject
	, public IMPCDIData
{
public:
	FMPCDIData();
	virtual ~FMPCDIData();

public:
	struct FMPCDIBuffer
	{
		virtual ~FMPCDIBuffer()
		{
			Regions.Empty();
		}

		FString ID;

		TArray<MPCDI::FMPCDIRegion*> Regions;

		FBox AABBox;

		// Shader Lamp view and projection matrices
		FVector Origin;
		FMatrix ProjectionMatrix;
		FMatrix Camera2World;

		bool FindRegion(const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) const
		{
			for (int RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
			{
				if (RegionName.Compare(Regions[RegionIndex]->ID, ESearchCase::IgnoreCase) == 0)
				{
					OutRegionLocator.RegionIndex = RegionIndex;
					return true;
				}
			}
			return false;
		}
	};

public:
	virtual bool IsCubeMapEnabled() const override;

public:
	bool Load(const FString& MPCIDIFile, bool useCubeMap = false, int cubeMapResolution = 2048, bool mpcdiColorAdjustment = false);
	void CleanupMPCDIData();

	MPCDI::FMPCDIRegion* GetRegion(const IMPCDI::FRegionLocator& RegionLocator) const
	{
		if (RegionLocator.BufferIndex < Buffers.Num())
		{
			FMPCDIBuffer* Buffer = Buffers[RegionLocator.BufferIndex];
			if (Buffer && (RegionLocator.RegionIndex < Buffer->Regions.Num()))
			{
				return Buffer->Regions[RegionLocator.RegionIndex];
			}
		}

		return nullptr;
	}

	inline bool FindRegion(const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) const
	{
		for (int BufferIndex = 0; BufferIndex < Buffers.Num(); ++BufferIndex)
		{
			if (BufferName.Compare(Buffers[BufferIndex]->ID, ESearchCase::IgnoreCase) == 0)
			{
				OutRegionLocator.BufferIndex = BufferIndex;
				return Buffers[BufferIndex]->FindRegion(RegionName, OutRegionLocator);
			}
		}

		return false;
	}

	inline const FMatrix &GetTextureMatrix() const
	{ return TextureMatrix; }

	inline bool IsValid() const
	{ return Profile != EMPCDIProfileType::Invalid; }

	inline EMPCDIProfileType GetProfileType() const
	{ return Profile; }
	
	bool ColorAdjustmentsEnabled() const
	{ return IsValid() && UseColorAdjustment; }

	inline const FString& GetFilePath() const
	{ return FilePath; }

	bool ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, IMPCDI::FFrustum &Frustum, float worldScale, float ZNear, float ZFar) const;

private:
	bool Load();
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	TArray<FMPCDIBuffer*> Buffers;
	EMPCDIProfileType     Profile;
	FString Version;

	FMatrix TextureMatrix;

	FString FilePath;
	bool    UseColorAdjustment;
};
