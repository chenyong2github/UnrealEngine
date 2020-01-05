// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"

#include "MPCDIRegion.h"

#include "RendererInterface.h"
#include "UObject/GCObject.h"
#include "RenderResource.h"
#include "SceneTypes.h"

class FMPCDIData
	: public FGCObject
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

		FString                      ID;       // Unique buffer name
		TArray<MPCDI::FMPCDIRegion*> Regions;  // Asset of warp regions
		FBox                         AABBox;

		// Shader Lamp view and projection matrices
		FVector Origin;
		FMatrix ProjectionMatrix;
		FMatrix Camera2World;

		bool Initialize(const FString& BufferName);
		void AddRegion(MPCDI::FMPCDIRegion* MPCDIRegionPtr);

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
	bool Initialize(const FString& MPCIDIFile, IMPCDI::EMPCDIProfileType MPCDIProfileType);
	bool LoadFromFile(const FString& MPCIDIFile);

	void CleanupMPCDIData();


	void ReloadAll();
	void ReloadChangedExternalFiles_RenderThread();

	bool AddRegion(const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator);

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
	{ 
		return TextureMatrix; 
	}

	inline bool IsValid() const
	{ 
		return Profile != IMPCDI::EMPCDIProfileType::Invalid; 
	}

	inline IMPCDI::EMPCDIProfileType GetProfileType() const
	{ 
		return Profile; 
	}

	inline const FString& GetLocalMPCIDIFile() const
	{ 
		return LocalMPCIDIFile;
	}

	bool ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, IMPCDI::FFrustum &Frustum, float worldScale, float ZNear, float ZFar) const;

private:
#if 0
	//@todo Unsupported now
	bool ComputeFrustum_SL(const IMPCDI::FRegionLocator& RegionLocator, IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const;
#endif

	bool Load(const FString& MPCDIFile);
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	TArray<FMPCDIBuffer*> Buffers;
	IMPCDI::EMPCDIProfileType     Profile;
	FString Version;

	FMatrix TextureMatrix;

	FString    LocalMPCIDIFile;

	mutable bool       bForceExtFilesReload;
	FCriticalSection   ExtFilesReloadCS;
};
