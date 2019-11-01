// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"


/**
 * Implements math behind the native (manual) quad based projections
 */
class FDisplayClusterProjectionManualPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionManualPolicy(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionManualPolicy();

public:
	enum class EManualDataType
	{
		Matrix,
		FrustumAngles
	};

	EManualDataType GetDataType() const
	{ return DataType; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartScene(UWorld* World) override;
	virtual void EndScene() override;
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) override;
	virtual void HandleRemoveViewport() override;

	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& InViewOffset, const float InWorldToMeters, const float InNCP, const float InFCP) override;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return false; }

protected:
	struct FFrustumAngles
	{
		float Left   = 0.f;
		float Right  = 0.f;
		float Top    = 0.f;
		float Bottom = 0.f;
	};

	virtual bool ExtractAngles(const FString& InAngles, FFrustumAngles& OutAngles);

private:
	// Current data type (matrix, frustum angle, ...)
	EManualDataType DataType;
	// View rotation
	FRotator ViewRotation = FRotator::ZeroRotator;
	// Projection matrix
	FMatrix  ProjectionMatrix[2] = { FMatrix::Identity };
	// Frustum angles
	FFrustumAngles FrustumAngles[2];
	// Near/far clip planes
	float NCP;
	float FCP;
};
