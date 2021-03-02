// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionDataHandler.h"

#include "Algo/MaxElement.h"
#include "Components/SceneComponent.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Math/NumericLimits.h"

bool FLensDistortionState::operator==(const FLensDistortionState& Other) const
{
	return ((LensModel == Other.LensModel)
		&& (DistortionParameters == Other.DistortionParameters)
		&& (PrincipalPoint == Other.PrincipalPoint)
		&& (SensorDimensions == Other.SensorDimensions)
		&& (FocalLength == Other.FocalLength));
}

ULensDistortionDataHandler* ULensDistortionDataHandler::GetLensDistortionDataHandler(UActorComponent* ComponentWithUserData)
{
	if (IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(ComponentWithUserData))
	{
		return Cast<ULensDistortionDataHandler>(AssetUserData->GetAssetUserDataOfClass(ULensDistortionDataHandler::StaticClass()));
	}

	return nullptr;
}

void ULensDistortionDataHandler::Update(const FLensDistortionState& InNewState)
{
	/** Check for duplicate updates. If the new CurrentState is equivalent to the current CurrentState, there is nothing to update. */
	if (CurrentState == InNewState)
	{
		return;
	}

	UpdateInternal(InNewState);
}

void ULensDistortionDataHandler::UpdateCameraSettings(FVector2D InSensorDimensions, float InFocalLength)
{
	/** Check for duplicate updates. If the new camera settings are equivalent to the current camera settings, there is nothing to update. */
	if ((CurrentState.SensorDimensions == InSensorDimensions) && (CurrentState.FocalLength == InFocalLength))
	{
		return;
	}

	CurrentState.SensorDimensions = InSensorDimensions;
	CurrentState.FocalLength = InFocalLength;

	UpdateInternal(CurrentState);
}

#if WITH_EDITOR
void ULensDistortionDataHandler::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionParameters, K1))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionParameters, K2))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionParameters, K3))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionParameters, P1))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionParameters, P2))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FVector2D, X))
		|| (PropertyName == GET_MEMBER_NAME_CHECKED(FVector2D, Y)))
	{
		UpdateInternal(CurrentState);
	}
}
#endif	

float ULensDistortionDataHandler::ComputeOverscanFactor() const
{
	/** Undistorted UV position in the view space:
	                   ^ View space's Y
	                   |
	          0        1        2
	       
	          7                 3 --> View space's X
	       
	          6        5        4 
	*/
	const TArray<FVector2D> UndistortedUVs =
	{
		FVector2D(0.0f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 0.5f),
		FVector2D(1.0f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, 0.5f)
	};

	TArray<float> OverscanFactors;
	OverscanFactors.Reserve(UndistortedUVs.Num());
	for (const FVector2D& UndistortedUV : UndistortedUVs)
	{
		const FVector2D DistortedUV = ComputeDistortedUV(UndistortedUV);
		const float OverscanX = (UndistortedUV.X != 0.5f) ? (DistortedUV.X - 0.5f) / (UndistortedUV.X - 0.5f) : 1.0f;
		const float OverscanY = (UndistortedUV.Y != 0.5f) ? (DistortedUV.Y - 0.5f) / (UndistortedUV.Y - 0.5f) : 1.0f;
		OverscanFactors.Add(FMath::Max(OverscanX, OverscanY));
	}

	float* MaxOverscanFactor = Algo::MaxElement(OverscanFactors);
	if (MaxOverscanFactor == nullptr)
	{
		return 1.0f;
	}
	else
	{
		return FMath::Max(*MaxOverscanFactor, 1.0f);
	}
}

FVector2D ULensDistortionDataHandler::ComputeDistortedUV(const FVector2D& InUndistortedUV) const
{
	/** These distances cannot be zero in real-life. If they are, the current distortion state must be bad */
	if ((CurrentState.FocalLength == 0) || (CurrentState.SensorDimensions.X == 0) | (CurrentState.SensorDimensions.Y == 0))
	{
		return InUndistortedUV;
	}

	const FVector2D NormalizedFocalLength = FVector2D(CurrentState.FocalLength, CurrentState.FocalLength) / CurrentState.SensorDimensions;

	FVector2D NormalizedDistanceFromImageCenter = (InUndistortedUV - CurrentState.PrincipalPoint) / NormalizedFocalLength;

	/** Iterative approach to distort an undistorted UV using coefficients that were designed to undistort */
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector2D DistanceSquared = NormalizedDistanceFromImageCenter * NormalizedDistanceFromImageCenter;
		const float RSquared = DistanceSquared.X + DistanceSquared.Y;

		const float RadialDistortion = 1.0f + (CurrentState.DistortionParameters.K1 * RSquared) + (CurrentState.DistortionParameters.K2 * RSquared * RSquared) + (CurrentState.DistortionParameters.K3 * RSquared * RSquared * RSquared);

		const FVector2D TangentialDistortion = 
		{
			(CurrentState.DistortionParameters.P2 * (RSquared + (2.0f * DistanceSquared.X))) + (2.0f * CurrentState.DistortionParameters.P1 * NormalizedDistanceFromImageCenter.X * NormalizedDistanceFromImageCenter.Y),
			(CurrentState.DistortionParameters.P1 * (RSquared + (2.0f * DistanceSquared.Y))) + (2.0f * CurrentState.DistortionParameters.P2 * NormalizedDistanceFromImageCenter.X * NormalizedDistanceFromImageCenter.Y)
		};

		/** Guard against divide-by-zero errors */
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

void ULensDistortionDataHandler::UpdateInternal(const FLensDistortionState& InNewState)
{
	/** If the lens model has changed, or the MID is null, a new MID needs to be created */
	if ((CurrentState.LensModel != InNewState.LensModel) || (!DistortionMID))
	{
		if (InNewState.LensModel == ELensModel::Spherical)
		{
			UMaterialInterface* DistortionMaterialParent = LoadObject<UMaterialInterface>(NULL, TEXT("/LensDistortion/Materials/MAT_brown3t2_overscan.MAT_brown3t2_overscan"), NULL, LOAD_None, NULL);
			DistortionMID = UMaterialInstanceDynamic::Create(DistortionMaterialParent, this);
		}
	}

	CurrentState = InNewState;

	/** Recompute the overscan factor using the new state */
	OverscanFactor = ComputeOverscanFactor();

	/** Update the material parameters */
	if (DistortionMID)
	{
		DistortionMID->SetScalarParameterValue("sensor_w_mm", CurrentState.SensorDimensions.X);
		DistortionMID->SetScalarParameterValue("sensor_h_mm", CurrentState.SensorDimensions.Y);
		DistortionMID->SetScalarParameterValue("fl_mm", CurrentState.FocalLength);

		DistortionMID->SetScalarParameterValue("K1", CurrentState.DistortionParameters.K1);
		DistortionMID->SetScalarParameterValue("K2", CurrentState.DistortionParameters.K2);
		DistortionMID->SetScalarParameterValue("K3", CurrentState.DistortionParameters.K3);
		DistortionMID->SetScalarParameterValue("P1", CurrentState.DistortionParameters.P1);
		DistortionMID->SetScalarParameterValue("P2", CurrentState.DistortionParameters.P2);

		DistortionMID->SetScalarParameterValue("cx", CurrentState.PrincipalPoint.X);
		DistortionMID->SetScalarParameterValue("cy", CurrentState.PrincipalPoint.Y);

		DistortionMID->SetScalarParameterValue("OverscanFactor", OverscanFactor);
	}
}