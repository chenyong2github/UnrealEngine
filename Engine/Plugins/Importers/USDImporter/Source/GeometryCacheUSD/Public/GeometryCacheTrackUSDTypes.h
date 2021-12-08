// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

class UGeometryCacheTrackUsd;
struct FGeometryCacheMeshData;

typedef TFunction< void( const TWeakObjectPtr<UGeometryCacheTrackUsd>, float Time, FGeometryCacheMeshData& ) > FReadUsdMeshFunction;
