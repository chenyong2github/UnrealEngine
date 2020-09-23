// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ARTypes.h"
#include "MRMeshBufferDefines.h"


struct FOpenXRMeshUpdate : public FNoncopyable
{
	FGuid Id;
	EARObjectClassification Type = EARObjectClassification::NotApplicable;
	EARTrackingState TrackingState = EARTrackingState::Unknown;

	FTransform LocalToTrackingTransform;

	TArray<FVector> Vertices;
	TArray<MRMESH_INDEX_TYPE> Indices;
};


class IOpenXRARTrackedMeshHolder
{
public:
	virtual ~IOpenXRARTrackedMeshHolder() {}

	virtual void StartMeshUpdates() = 0;
	virtual FOpenXRMeshUpdate* AllocateMeshUpdate(FGuid InGuidMeshUpdate) = 0;
	virtual void RemoveMesh(FGuid InGuidMeshUpdate) = 0;
	virtual void EndMeshUpdates() = 0;
};

