// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"


/**
 * Implements math behind the native (manual) quad based projections
 */
class FDisplayClusterProjectionManualPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionManualPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionManualPolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::Manual; }

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
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& InViewOffset, const float InWorldToMeters, const float InNCP, const float InFCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return false; }

protected:
	struct FFrustumAngles
	{
		float Left   = -30.f;
		float Right  = 30.f;
		float Top    = 30.f;
		float Bottom = -30.f;
	};

	virtual bool ExtractAngles(const FString& InAngles, FFrustumAngles& OutAngles);
private:
	EManualDataType DataTypeFromString(const FString& DataTypeInString) const;
private:
	// Current data type (matrix, frustum angle, ...)
	EManualDataType DataType = EManualDataType::Matrix;
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
