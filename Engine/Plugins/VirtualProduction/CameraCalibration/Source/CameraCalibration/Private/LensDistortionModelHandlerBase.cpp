// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionModelHandlerBase.h"

#include "Algo/MaxElement.h"
#include "Kismet/KismetRenderingLibrary.h"

bool FLensDistortionState::operator==(const FLensDistortionState& Other) const
{
	return ((DistortionInfo.Parameters == Other.DistortionInfo.Parameters)
		&& (PrincipalPoint == Other.PrincipalPoint)
 		&& (FxFy == Other.FxFy));
	return false;
}

bool ULensDistortionModelHandlerBase::IsModelSupported(const TSubclassOf<ULensModel>& ModelToSupport) const
{
	return (LensModelClass == ModelToSupport);
}

void ULensDistortionModelHandlerBase::SetDistortionState(const FLensDistortionState& InNewState)
{
	// If the new state is equivalent to the current state, there is nothing to update. 
	if (CurrentState != InNewState)
	{
		CurrentState = InNewState;
		InterpretDistortionParameters();

		bIsDirty = true;
	}	
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
			const uint32 NumDistortionParameters = LensModelClass->GetDefaultObject<ULensModel>()->GetNumParameters();
			CurrentState.DistortionInfo.Parameters.Init(0.0f, NumDistortionParameters);
		}

		// TODO: Delay this to an initialization step where the user can specify whether to create one or both render targets
		UndistortionDisplacementMapRT = NewObject<UTextureRenderTarget2D>(this, MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("UndistortionDisplacementMapRT")));
		UndistortionDisplacementMapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RG16f;
		UndistortionDisplacementMapRT->ClearColor = FLinearColor::Black;
		UndistortionDisplacementMapRT->bAutoGenerateMips = false;
		UndistortionDisplacementMapRT->InitAutoFormat(DisplacementMapWidth, DisplacementMapHeight);
		UndistortionDisplacementMapRT->UpdateResourceImmediate(true);

		DistortionDisplacementMapRT = NewObject<UTextureRenderTarget2D>(this, MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("DistortionDisplacementMapRT")));
		DistortionDisplacementMapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RG16f;
		DistortionDisplacementMapRT->ClearColor = FLinearColor::Black;
		DistortionDisplacementMapRT->bAutoGenerateMips = false;
		DistortionDisplacementMapRT->InitAutoFormat(DisplacementMapWidth, DisplacementMapHeight);
		DistortionDisplacementMapRT->UpdateResourceImmediate(true);
	}
}

#if WITH_EDITOR
void ULensDistortionModelHandlerBase::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULensDistortionModelHandlerBase, CurrentState))
	{
		// Will need to revisit this init logic once we move to arbitrary lens model support 
		if (!DistortionPostProcessMID || !UndistortionDisplacementMapMID || !DistortionDisplacementMapMID)
		{
			InitDistortionMaterials();
		}

		bIsDirty = true;
		InterpretDistortionParameters();
		SetOverscanFactor(ComputeOverscanFactor());
		ProcessCurrentDistortion();
	}
}
#endif	

void ULensDistortionModelHandlerBase::SetOverscanFactor(float InOverscanFactor)
{
	if (!DistortionPostProcessMID || !UndistortionDisplacementMapMID || !DistortionDisplacementMapMID)
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

	float NewOverscan = 1.0f;
	float* MaxOverscanFactor = Algo::MaxElement(OverscanFactors);
	if (MaxOverscanFactor != nullptr)
	{
		NewOverscan = FMath::Max(*MaxOverscanFactor, 1.0f);
	}

	return NewOverscan;
}

TArray<FVector2D> ULensDistortionModelHandlerBase::GetDistortedUVs(TConstArrayView<FVector2D> UndistortedUVs) const
{
	TArray<FVector2D> DistortedUVs;
	DistortedUVs.Reserve(UndistortedUVs.Num());
	for (const FVector2D& UndistortedUV : UndistortedUVs)
	{
		const FVector2D DistortedUV = ComputeDistortedUV(UndistortedUV);
		DistortedUVs.Add(DistortedUV);
	}
	return DistortedUVs;
}

bool ULensDistortionModelHandlerBase::DrawUndistortionDisplacementMap(UTextureRenderTarget2D* DestinationTexture)
{
	if(DestinationTexture == nullptr)
	{
		return false;
	}
	
	if (!DistortionPostProcessMID || !UndistortionDisplacementMapMID)
	{
		InitDistortionMaterials();
	}
	
	UpdateMaterialParameters();

	// Draw the updated displacement map render target 
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, DestinationTexture, UndistortionDisplacementMapMID);

	return true;
}

bool ULensDistortionModelHandlerBase::DrawDistortionDisplacementMap(UTextureRenderTarget2D* DestinationTexture)
{
	if (DestinationTexture == nullptr)
	{
		return false;
	}

	if (!DistortionPostProcessMID || !DistortionDisplacementMapMID)
	{
		InitDistortionMaterials();
	}

	UpdateMaterialParameters();

	// Draw the updated displacement map render target 
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, DestinationTexture, DistortionDisplacementMapMID);

	return true;
}

void ULensDistortionModelHandlerBase::ProcessCurrentDistortion()
{
	if(bIsDirty)
	{
		bIsDirty = false;

		InterpretDistortionParameters();

		UpdateMaterialParameters();

		// Draw the undistortion displacement map
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, UndistortionDisplacementMapRT, UndistortionDisplacementMapMID);

		// Draw the distortion displacement map
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, DistortionDisplacementMapRT, DistortionDisplacementMapMID);
	}
}
