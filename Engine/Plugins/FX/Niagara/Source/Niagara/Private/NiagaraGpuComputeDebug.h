// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help realtime debug Gpu Compute simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"

class FNiagaraGpuComputeDebug
{
public:
	struct FNiagaraVisualizeTexture
	{
		FNiagaraSystemInstanceID	SystemInstanceID;
		FName						SourceName;
		FTextureRHIRef				Texture;
		FIntPoint					NumTextureAttributes = FIntPoint::ZeroValue;
		FIntVector4					AttributesToVisualize = FIntVector4(-1, -1, -1, -1);
	};

	FNiagaraGpuComputeDebug(ERHIFeatureLevel::Type InFeatureLevel)
		: FeatureLevel(InFeatureLevel)
	{
	}

	// Enables providing debug information for the system instance
	void AddSystemInstance(FNiagaraSystemInstanceID SystemInstanceID, FString SystemName);
	// Disabled providing debug information for the system instance
	void RemoveSystemInstance(FNiagaraSystemInstanceID SystemInstanceID);

	// Notification from the batcher when a system instance has been removed, i.e. the system was reset
	void OnSystemDeallocated(FNiagaraSystemInstanceID SystemInstanceID);

	// Add a texture to visualize
	void AddTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture);

	// Add a texture to visualize that contains a number of attributes and select which attributes to push into RGBA where -1 means ignore that channel
	// The first -1 in the attribute indices list will also limit the number of attributes we attempt to read
	void AddAttributeTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture, FIntPoint NumTextureAttributes, FIntVector4 AttributeIndices);

	// Do we need DrawDebug to be called?
	bool ShouldDrawDebug() const;

	// Draw all the debug information for the system
	void DrawDebug(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, const struct FScreenPassRenderTarget& Output);

private:
	ERHIFeatureLevel::Type FeatureLevel;
	uint32 TickCounter = 0;
	TArray<FNiagaraVisualizeTexture> VisualizeTextures;
	TMap<FNiagaraSystemInstanceID, FString> SystemInstancesToWatch;
};
