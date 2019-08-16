// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

class FMPCDIData;


class FPicpProjectionOverlayCamera
{
public:
	FVector  SoftEdge;    // Basic soft edges values
	//@ Add more render options here
	
	//Camera setup
	FString RTTViewportId;
	FRHITexture* CameraTexture; // Texture to render
	FMatrix Prj; // Projection matrix

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
	{
	}

	const FMatrix GetRuntimeCameraProjection() const
	{ return RuntimeCameraProjection; }

	void Empty()
	{}

public:
	FMatrix RuntimeCameraProjection;
};
