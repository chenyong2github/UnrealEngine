// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprints/MPCDIContainers.h"

struct FMPCDIRegion;


bool ExportMeshData(FMPCDIRegion* Region, FMPCDIGeometryExportData& MeshData);
bool ImportMeshData(FMPCDIRegion* Region, const FMPCDIGeometryImportData& MeshData);
