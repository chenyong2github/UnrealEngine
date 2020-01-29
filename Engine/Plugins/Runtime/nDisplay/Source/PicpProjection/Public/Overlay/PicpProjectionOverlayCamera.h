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
	FRHITexture* CameraTexture; // Texture to render
	FMatrix Prj;                // Projection matrix

	FPicpProjectionCameraChromakey Chromakey;

	FRotator ViewRot;
	FVector  ViewLoc;

public:
	FPicpProjectionOverlayCamera(const FRotator& CameraRotation, const FVector& CameraLocation, const FMatrix& CameraPrj, FRHITexture* CameraTextureRef, const FString& ViewportId)
		: SoftEdge(0.1f, 0.1f, 0.1f)
		, RTTViewportId(ViewportId)
		, CameraTexture(CameraTextureRef)
		, Prj(CameraPrj)
		, ViewRot(CameraRotation)
		, ViewLoc(CameraLocation)
	{ }

	inline bool IsCameraUsed() const
	{
		return (CameraTexture != nullptr) && CameraTexture->IsValid();
	}

	void Empty()
	{ }
};
