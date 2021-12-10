// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterWarpEnums.h"

class IDisplayClusterWarpBlend;


struct FDisplayClusterWarpBlendConstruct
{
	struct FLoadMPCDIFile
	{
		FString MPCDIFileName;

		FString BufferId;
		FString RegionId;
	};

	struct FLoadPFMFile
	{
		EDisplayClusterWarpProfileType ProfileType;

		FString PFMFileName;
		float   PFMScale = 1.f;
		bool    bIsUnrealGameSpace = false;

		FString AlphaMapFileName;
		float   AlphaMapEmbeddedAlpha = 0.f;

		FString BetaMapFileName;
	};

	struct FAssignWarpMesh
	{
		class UStaticMeshComponent* MeshComponent   = nullptr;
		class USceneComponent*      OriginComponent = nullptr;
		int32 StaticMeshComponentLODIndex = 0;
	};

	struct FAssignWarpProceduralMesh
	{
		class UProceduralMeshComponent* ProceduralMeshComponent = nullptr;
		class USceneComponent* OriginComponent = nullptr;
		int32 ProceduralMeshComponentSectionIndex = 0;
	};
};


class IDisplayClusterWarpBlendManager
{
public:
	virtual ~IDisplayClusterWarpBlendManager() = default;

public:
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FLoadMPCDIFile&            InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const = 0;
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FLoadPFMFile&              InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const = 0;
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FAssignWarpMesh&           InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const = 0;
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FAssignWarpProceduralMesh& InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const = 0;
};
