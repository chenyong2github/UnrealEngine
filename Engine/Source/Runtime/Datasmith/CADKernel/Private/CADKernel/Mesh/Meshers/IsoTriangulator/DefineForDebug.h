// Copyright Epic Games, Inc. All Rights Reserved.
// Refactor in progress
#pragma once

#ifdef CADKERNEL_DEV

//#define DEBUG_CADKERNEL

#ifdef DEBUG_CADKERNEL
static int32 FaceToDebug = 202910;

#define ADD_TRIANGLE_2D
#define DEBUG_ISOTRIANGULATOR

// Loop cleaner
//#define DEBUG_CLEAN_LOOPS
//#define DEBUG_LOOP_INTERSECTION_AND_FIX_IT		
//#define DEBUG_REMOVE_LOOP_INTERSECTIONS
//#define DEBUG_REMOVE_UNIQUE_INTERSECTION
//#define DEBUG_SWAP_SEGMENTS_OR_REMOVE
//#define DEBUG_FIND_LOOP_INTERSECTIONS	
//#define DEBUG_REMOVE_INTERSECTIONS


#define DEBUG_ONLY_SURFACE_TO_DEBUG
static bool bDisplayDebugMeshStep=false;

#define DEBUG_HAS_CYCLE

#endif

#endif
