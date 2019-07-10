// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PicpProjectionOverlayBase.h"
#include "CoreMinimal.h"
#include "RHI.h"

class FMPCDIData;


class FPicpProjectionOverlayCamera
	: public FPicpProjectionOverlayBase
{
public:
	FVector  SoftEdge;    // Basic soft edges values
	//@ Add more render options here
	
	//Camera setup
	FRHITexture* CameraTexture; // Texture to render
	FMatrix Prj; // Projection matrix

	FRotator ViewRot;
	FVector  ViewLoc;

public:
	FPicpProjectionOverlayCamera(const FRotator& CameraRotation, const FVector& CameraLocation, const FMatrix& CameraPrj, FRHITexture* CameraTextureRef)
		: FPicpProjectionOverlayBase()
		, SoftEdge(0.1f, 0.1f, 0.1f)
		, CameraTexture(CameraTextureRef)
		, Prj(CameraPrj)
		, ViewRot(CameraRotation)
		, ViewLoc(CameraLocation)
	{ 
		SetEnable(true);
	}

	virtual ~FPicpProjectionOverlayCamera()
	{ }

	const FMatrix GetRuntimeCameraProjection() const
	{ return RuntimeCameraProjection; }

public:
	FMatrix RuntimeCameraProjection;
};
