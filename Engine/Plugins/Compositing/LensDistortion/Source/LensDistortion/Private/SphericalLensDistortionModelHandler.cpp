// Copyright Epic Games, Inc. All Rights Reserved.

#include "SphericalLensDistortionModelHandler.h"

#include "Engine/TextureRenderTarget2D.h"
#include "LensDistortionSettings.h"
#include "Math/NumericLimits.h"

void USphericalLensDistortionModelHandler::InitializeHandler()
{
	LensModelClass = USphericalLensModel::StaticClass();
}

FVector2D USphericalLensDistortionModelHandler::ComputeDistortedUV(const FVector2D& InUndistortedUV) const
{
	// These distances cannot be zero in real-life. If they are, the current distortion state must be bad
	if ((CurrentState.FocalLength == 0) || (CurrentState.SensorDimensions.X == 0) | (CurrentState.SensorDimensions.Y == 0))
	{
		return InUndistortedUV;
	}

	const FVector2D NormalizedFocalLength = FVector2D(CurrentState.FocalLength, CurrentState.FocalLength) / CurrentState.SensorDimensions;

	FVector2D NormalizedDistanceFromImageCenter = (InUndistortedUV - CurrentState.PrincipalPoint) / NormalizedFocalLength;

	// Iterative approach to distort an undistorted UV using coefficients that were designed to undistort
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector2D DistanceSquared = NormalizedDistanceFromImageCenter * NormalizedDistanceFromImageCenter;
		const float RSquared = DistanceSquared.X + DistanceSquared.Y;

		const float RadialDistortion = 1.0f + (SphericalParameters.K1 * RSquared) + (SphericalParameters.K2 * RSquared * RSquared) + (SphericalParameters.K3 * RSquared * RSquared * RSquared);

		const FVector2D TangentialDistortion = 
		{
			(SphericalParameters.P2 * (RSquared + (2.0f * DistanceSquared.X))) + (2.0f * SphericalParameters.P1 * NormalizedDistanceFromImageCenter.X * NormalizedDistanceFromImageCenter.Y),
			(SphericalParameters.P1 * (RSquared + (2.0f * DistanceSquared.Y))) + (2.0f * SphericalParameters.P2 * NormalizedDistanceFromImageCenter.X * NormalizedDistanceFromImageCenter.Y)
		};

		// Guard against divide-by-zero errors
		if (RadialDistortion == 0.0f)
		{
			NormalizedDistanceFromImageCenter = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
			break;
		}
		else
		{
			NormalizedDistanceFromImageCenter = (NormalizedDistanceFromImageCenter - TangentialDistortion) / RadialDistortion;
		}
	}

	const FVector2D DistortedUV = (NormalizedDistanceFromImageCenter * NormalizedFocalLength) + FVector2D(0.5f, 0.5f);
	return DistortedUV;
}

void USphericalLensDistortionModelHandler::InitDistortionMaterials()
{
	if (DistortionPostProcessMID == nullptr)
	{
		UMaterialInterface* DistortionMaterialParent = GetDefault<ULensDistortionSettings>()->GetDefaultDistortionMaterial(this->StaticClass());
		DistortionPostProcessMID = UMaterialInstanceDynamic::Create(DistortionMaterialParent, this);
	}

	if (DisplacementMapMID == nullptr)
	{
		UMaterialInterface* DisplacementMapMaterialParent = GetDefault<ULensDistortionSettings>()->GetDefaultDisplacementMaterial(this->StaticClass());
		DisplacementMapMID = UMaterialInstanceDynamic::Create(DisplacementMapMaterialParent, this);
	}

	DistortionPostProcessMID->SetTextureParameterValue("UVDisplacementMap", DisplacementMapRT);

	UpdateInternal(CurrentState);
}

void USphericalLensDistortionModelHandler::UpdateMaterialParameters()
{
	// Update the material parameters
	if (DisplacementMapMID)
	{
		DisplacementMapMID->SetScalarParameterValue("sensor_w_mm", CurrentState.SensorDimensions.X);
		DisplacementMapMID->SetScalarParameterValue("sensor_h_mm", CurrentState.SensorDimensions.Y);
		DisplacementMapMID->SetScalarParameterValue("fl_mm", CurrentState.FocalLength);

		DisplacementMapMID->SetScalarParameterValue("k1", SphericalParameters.K1);
		DisplacementMapMID->SetScalarParameterValue("k2", SphericalParameters.K2);
		DisplacementMapMID->SetScalarParameterValue("k3", SphericalParameters.K3);
		DisplacementMapMID->SetScalarParameterValue("p1", SphericalParameters.P1);
		DisplacementMapMID->SetScalarParameterValue("p2", SphericalParameters.P2);

		DisplacementMapMID->SetScalarParameterValue("cx", CurrentState.PrincipalPoint.X);
		DisplacementMapMID->SetScalarParameterValue("cy", CurrentState.PrincipalPoint.Y);

		DisplacementMapMID->SetScalarParameterValue("overscan_factor", OverscanFactor);
	}

	if (DistortionPostProcessMID)
	{
		DistortionPostProcessMID->SetScalarParameterValue("overscan_factor", OverscanFactor);
	}
}

void USphericalLensDistortionModelHandler::InterpretDistortionParameters()
{
	LensModelClass->GetDefaultObject<ULensModel>()->FromArray<FSphericalDistortionParameters>(CurrentState.DistortionInfo.Parameters, SphericalParameters);
}
