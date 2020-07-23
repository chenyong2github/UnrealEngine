// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalDistanceField.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "Delegates/DelegateCombinations.h"
#include "Async/TaskGraphInterfaces.h"

extern int32 GAOGlobalDistanceField;

inline bool UseGlobalDistanceField()
{
	return GAOGlobalDistanceField != 0;
}

inline bool UseGlobalDistanceField(const FDistanceFieldAOParameters& Parameters)
{
	return UseGlobalDistanceField() && Parameters.GlobalMaxOcclusionDistance > 0;
}

/** 
 * Updates the global distance field for a view.  
 * Typically issues updates for just the newly exposed regions of the volume due to camera movement.
 * In the worst case of a camera cut or large distance field scene changes, a full update of the global distance field will be done.
 **/
extern void UpdateGlobalDistanceFieldVolume(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, 
	FScene* Scene, 
	float MaxOcclusionDistance, 
	FGlobalDistanceFieldInfo& Info);

class FGlobalDistanceFieldReadback
{
public:
	DECLARE_DELEGATE(FCompleteDelegate);

	FBox Bounds;
	FIntVector Size;
	TArray<FFloat16Color> ReadbackData;	
	FCompleteDelegate ReadbackComplete;
	ENamedThreads::Type CallbackThread = ENamedThreads::UnusedAnchor;
};

/**
 * Retrieves the GPU data of a global distance field clipmap for access by the CPU
 *
 * @note: Currently only works with the highest res clipmap on the first updated view in the frame
 **/
void RENDERER_API RequestGlobalDistanceFieldReadback(FGlobalDistanceFieldReadback* Readback);

inline void RequestGlobalDistanceFieldReadback_GameThread(FGlobalDistanceFieldReadback* Readback)
{
	ENQUEUE_RENDER_COMMAND(RequestGlobalDistanceFieldReadback)(
		[Readback](FRHICommandListImmediate& RHICmdList) {
		RequestGlobalDistanceFieldReadback(Readback);
	});
}