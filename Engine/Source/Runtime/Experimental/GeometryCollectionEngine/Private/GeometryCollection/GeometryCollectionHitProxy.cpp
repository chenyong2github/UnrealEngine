// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionHitProxy.h"

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
IMPLEMENT_HIT_PROXY(HGeometryCollection, HActor);
IMPLEMENT_HIT_PROXY(HGeometryCollectionBone, HActor);
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
