// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionModelHandlerBase.h"

#include "Algo/MaxElement.h"
#include "Kismet/KismetRenderingLibrary.h"

bool FLensDistortionState::operator==(const FLensDistortionState& Other) const
{
	return ((DistortionInfo.Parameters == Other.DistortionInfo.Parameters)
		&& (PrincipalPoint == Other.PrincipalPoint)
 		&& (SensorDimensions == Other.SensorDimensions)
 		&& (FocalLength == Other.FocalLength));
	return false;
}

bool ULensDistortionModelHandlerBase::IsModelSupported(const TSubclassOf<ULensModel>& ModelToSupport) const
{
	return (LensModelClass == ModelToSupport);
}

void ULensDistortionModelHandlerBase::Update(const FLensDistortionState& InNewState)
{
	// Will need to revisit this init logic once we move to arbitrary lens model support 
	if (!DistortionPostProcessMID || !DisplacementMapMID)
	{
		InitDistortionMaterials();
	}

	// Check for duplicate updates. If the new CurrentState is equivalent to the current CurrentState, there is nothing to update. 
	if (CurrentState == InNewState)
	{
		return;
	}

	UpdateInternal(InNewState);
}

void ULensDistortionModelHandlerBase::UpdateCameraSettings(FVector2D InSensorDimensions, float InFocalLength)
{
	// Will need to revisit this init logic once we move to arbitrary lens model support 
	if (!DistortionPostProcessMID || !DisplacementMapMID)
	{
		InitDistortionMaterials();
	}

	// Check for duplicate updates. If the new camera settings are equivalent to the current camera settings, there is nothing to update. 
	if ((CurrentState.SensorDimensions == InSensorDimensions) && (CurrentState.FocalLength == InFocalLength))
	{
		return;
	}

	CurrentState.SensorDimensions = InSensorDimensions;
	CurrentState.FocalLength = InFocalLength;

	UpdateInternal(CurrentState);
}

void ULensDistortionModelHandlerBase::PostInitProperties()
{
	Super::PostInitProperties();

	// Perform handler initialization, only on derived classes
	if (!GetClass()->HasAnyClassFlags(CLASS_Abstract))
	{
		InitializeHandler();
	}

	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		if (LensModelClass)
		{
			uint32 NumDistortionParameters = LensModelClass->GetDefaultObject<ULensModel>()->GetNumParameters();
			CurrentState.DistortionInfo.Parameters.Init(0.0f, NumDistortionParameters);
		}

		DisplacementMapRT = NewObject<UTextureRenderTarget2D>(this, MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("DistortedUVDisplacementMap")));
		DisplacementMapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		DisplacementMapRT->ClearColor = FLinearColor::Gray;
		DisplacementMapRT->bAutoGenerateMips = false;
		DisplacementMapRT->InitAutoFormat(DisplacementMapWidth, DisplacementMapHeight);
		DisplacementMapRT->UpdateResourceImmediate(true);
	}
}

#if WITH_EDITOR
void ULensDistortionModelHandlerBase::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULensDistortionModelHandlerBase, CurrentState))
	{
		// Will need to revisit this init logic once we move to arbitrary lens model support 
		if (!DistortionPostProcessMID || !DisplacementMapMID)
		{
			InitDistortionMaterials();
		}

		UpdateInternal(CurrentState);
	}
}
#endif	

void ULensDistortionModelHandlerBase::UpdateOverscanFactor(float InOverscanFactor)
{
	if (!DistortionPostProcessMID || !DisplacementMapMID)
	{
		InitDistortionMaterials();
	}
	OverscanFactor = InOverscanFactor;
	if (DistortionPostProcessMID)
	{
		DistortionPostProcessMID->SetScalarParameterValue("overscan_factor", OverscanFactor);
	}
}

float ULensDistortionModelHandlerBase::ComputeOverscanFactor() const
{
	/* Undistorted UV position in the view space:
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

void ULensDistortionModelHandlerBase::UpdateInternal(const FLensDistortionState& InNewState)
{
	CurrentState = InNewState;

	InterpretDistortionParameters();

	// Recompute the overscan factor using the new state 
	OverscanFactor = ComputeOverscanFactor();

	UpdateMaterialParameters();

	// Draw the updated displacement map render target 
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, DisplacementMapRT, DisplacementMapMID);
}