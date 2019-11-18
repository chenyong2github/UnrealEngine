// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMeshComponent;
class IMeshPaintGeometryAdapterFactory;
class FReferenceCollector;

class MESHPAINTINGTOOLSET_API FMeshPaintAdapterFactory
{
public:
	static TArray<TSharedPtr<IMeshPaintGeometryAdapterFactory>> FactoryList;

public:
	static TSharedPtr<class IMeshPaintGeometryAdapter> CreateAdapterForMesh(UMeshComponent* InComponent, int32 InPaintingMeshLODIndex);
	static void InitializeAdapterGlobals();
	static void AddReferencedObjectsGlobals(FReferenceCollector& Collector);
	static void CleanupGlobals();
};