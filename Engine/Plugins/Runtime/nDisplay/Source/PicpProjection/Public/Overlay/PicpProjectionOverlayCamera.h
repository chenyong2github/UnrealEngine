// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

#include "PicpProjectionOverlayChromakey.h"

class FMPCDIData;


class FPicpProjectionOverlayCamera
{
public:
	// Basic soft edges setup
	FVector  SoftEdge;
	//@todo: Add more render options here

	// Camera setup:
	FString RTTViewportId;      // The viewport name, used to capture camera frame
	FRHITexture* CameraTexture = nullptr; // Texture to render
	FMatrix Prj;                // Projection matrix

	FPicpProjectionCameraChromakey Chromakey;

	FRotator ViewRot;
	FVector  ViewLoc;

	uint32 NumMips;

	FRHITexture* CustomCameraTexture = nullptr; // override camera frame with this texture

public:
	FPicpProjectionOverlayCamera(const FRotator& CameraRotation, const FVector& CameraLocation, const FMatrix& CameraPrj, const FString& ViewportId)
		: SoftEdge(0.1f, 0.1f, 0.1f)
		, RTTViewportId(ViewportId)
		, Prj(CameraPrj)
		, ViewRot(CameraRotation)
		, ViewLoc(CameraLocation)
	{ }

	void Empty()
	{ }
};
