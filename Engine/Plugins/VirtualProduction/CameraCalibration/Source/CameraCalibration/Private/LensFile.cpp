// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensFile.h"

#include "Algo/MaxElement.h"
#include "CalibratedMapProcessor.h"
#include "CameraCalibrationLog.h"
#include "CameraCalibrationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFileRendering.h"
#include "LensInterpolationUtils.h"
#include "Models/SphericalLensModel.h"


namespace LensFileUtils
{
	UTextureRenderTarget2D* CreateDisplacementMapRenderTarget(UObject* Outer)
	{
		check(Outer);

		//Will be good using a project settings or global resolution that user can change
		const FIntPoint Size{256,256};

		UTextureRenderTarget2D* NewRenderTarget2D = NewObject<UTextureRenderTarget2D>(Outer, MakeUniqueObjectName(Outer, UTextureRenderTarget2D::StaticClass(), TEXT("LensDisplacementMap")), RF_Public);
		NewRenderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RG16f;
		NewRenderTarget2D->ClearColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.5f);
		NewRenderTarget2D->bAutoGenerateMips = false;
		NewRenderTarget2D->bCanCreateUAV = true;
		NewRenderTarget2D->InitAutoFormat(Size.X, Size.Y);
		NewRenderTarget2D->UpdateResourceImmediate(true);

		// Flush RHI thread after creating texture render target to make sure that RHIUpdateTextureReference is executed before doing any rendering with it
		// This makes sure that Value->TextureReference.TextureReferenceRHI->GetReferencedTexture() is valid so that FUniformExpressionSet::FillUniformBuffer properly uses the texture for rendering, instead of using a fallback texture
		ENQUEUE_RENDER_COMMAND(FlushRHIThreadToUpdateTextureRenderTargetReference)(
			[](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});

		return NewRenderTarget2D;
	}	


	int32 GDistortionParametersBlendMode = 0;
	static FAutoConsoleVariableRef CVarDistortionParametersBlendMode(
		TEXT("Lens.DistortionParametersBlendMode"),
		GDistortionParametersBlendMode,
		TEXT("Integer value specifying how to blend distortion parameters: (0 - Default) Blend parameters based on blend type, (1 - Blend DisplacementMap) Run distortion equation and blend the resulting maps"),
		ECVF_Default
	);
}

const TArray<FVector2D> ULensFile::UndistortedUVs =
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

ULensFile::ULensFile()
{
	LensInfo.LensModel = USphericalLensModel::StaticClass();
	
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		CalibratedMapProcessor = MakeUnique<FCalibratedMapProcessor>();
	}
}

#if WITH_EDITOR

void ULensFile::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if(PropertyChangedEvent.Property != nullptr)
	{
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FCalibratedMapPoint, DistortionMap))
		{
			//When the distortion map (stmap) changes, flag associated derived data as dirty to update it
			check(PropertyChangedEvent.PropertyChain.GetActiveMemberNode() && PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue());

			const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
			const int32 ModifiedIndex = PropertyChangedEvent.GetArrayIndex(MemberPropertyName.ToString());
			check(CalibratedMapPoints.IsValidIndex(ModifiedIndex));

			FCalibratedMapPoint& MapPoint = CalibratedMapPoints[ModifiedIndex];
			MapPoint.DerivedDistortionData.bIsDirty = true;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FLensInfo, LensModel))
		{
			uint32 NumDistortionParameters = 0;
			if (LensInfo.LensModel)
			{
				NumDistortionParameters = LensInfo.LensModel.GetDefaultObject()->GetNumParameters();
			}

			for (FDistortionMapPoint& MapPoint : DistortionMapping)
			{
				MapPoint.DistortionInfo.Parameters.Empty();
				MapPoint.DistortionInfo.Parameters.Init(0.0f, NumDistortionParameters);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensFile, DistortionMapping))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
			{
				uint32 NumDistortionParameters = 0;
				if (LensInfo.LensModel)
				{
					NumDistortionParameters = LensInfo.LensModel.GetDefaultObject()->GetNumParameters();
				}

				FDistortionMapPoint& MapPoint = DistortionMapping.Last();
				MapPoint.DistortionInfo.Parameters.Init(0.0f, NumDistortionParameters);
			}
		}
	}
	
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif //WITH_EDITOR

bool ULensFile::EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionInfo& OutEvaluatedValue)
{
	if (DistortionMapping.Num() <= 0)
	{
		return false;
	}

	if (DistortionMapping.Num() == 1)
	{
		OutEvaluatedValue = DistortionMapping[0].DistortionInfo;
		return true;
	}

	FDistortionMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(InFocus, InZoom, DistortionMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.DistortionInfo);
	}

	return bSuccess;
}

bool ULensFile::EvaluateIntrinsicParameters(float InFocus, float InZoom, FIntrinsicParameters& OutEvaluatedValue)
{
	if (IntrinsicMapping.Num() <= 0)
	{
		return false;
	}

	if (IntrinsicMapping.Num() == 1)
	{
		OutEvaluatedValue = IntrinsicMapping[0].Parameters;
		return true;
	}

	FIntrinsicMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FIntrinsicMapPoint>(InFocus, InZoom, IntrinsicMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.Parameters);
	}

	return bSuccess;
}

bool ULensFile::EvaluateDistortionData(float InFocus, float InZoom, ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData) 
{	
	if (LensHandler == nullptr)
	{
		UE_LOG(LogCameraCalibration, Warning, TEXT("Can't evaluate LensFile '%s' - Invalid Lens Handler"), *GetName());
		return false;
	}
	
	if (LensHandler->GetUndistortionDisplacementMap() == nullptr)
	{
		UE_LOG(LogCameraCalibration, Warning, TEXT("Can't evaluate LensFile '%s' - Invalid undistortion displacement map in LensHandler '%s'"), *GetName(), *LensHandler->GetName());
		return false;
	}

	if (LensHandler->GetDistortionDisplacementMap() == nullptr)
	{
		UE_LOG(LogCameraCalibration, Warning, TEXT("Can't evaluate LensFile '%s' - Invalid distortion displacement map in LensHandler '%s'"), *GetName(), *LensHandler->GetName());
		return false;
	}

	if (LensInfo.LensModel == nullptr)
	{
		UE_LOG(LogCameraCalibration, Warning, TEXT("Can't evaluate LensFile '%s' - Invalid Lens Model"), *GetName());
		SetupNoDistortionOutput(LensHandler, OutDistortionData);
		return false;
	}

	if (LensHandler->IsModelSupported(LensInfo.LensModel) == false)
	{
		UE_LOG(LogCameraCalibration, Warning, TEXT("Can't evaluate LensFile '%s' - LensHandler '%s' doesn't support lens model '%s'"), *GetName(), *LensHandler->GetName(), *LensInfo.LensModel.GetDefaultObject()->GetModelName().ToString());
		SetupNoDistortionOutput(LensHandler, OutDistortionData);
		return false;
	}
	
	if(DataMode == ELensDataMode::Parameters)
	{
		return EvaluateDistortionForParameters(InFocus, InZoom, LensHandler, OutDistortionData);
	}
	else
	{
		//Only other mode for now
		check(DataMode == ELensDataMode::STMap);

		return EvaluteDistortionForSTMaps(InFocus, InZoom, LensHandler, OutDistortionData);
	}
}

float ULensFile::ComputeOverscan(const FDistortionData& DerivedData, FVector2D PrincipalPoint) const
{
	//Edge case if computed data hasn't came back yet
	if (UndistortedUVs.Num() != DerivedData.DistortedUVs.Num())
	{
		return 1.0f;
	}

	TArray<float, TInlineAllocator<8>> OverscanFactors;
	OverscanFactors.Reserve(UndistortedUVs.Num());
	for (int32 Index = 0; Index < UndistortedUVs.Num(); ++Index)
	{
		const FVector2D& UndistortedUV = UndistortedUVs[Index];
		const FVector2D& DistortedUV = DerivedData.DistortedUVs[Index] + (PrincipalPoint - FVector2D(0.5f, 0.5f)) * 2.0f;
		const float OverscanX = (UndistortedUV.X != 0.5f) ? (DistortedUV.X - 0.5f) / (UndistortedUV.X - 0.5f) : 1.0f;
		const float OverscanY = (UndistortedUV.Y != 0.5f) ? (DistortedUV.Y - 0.5f) / (UndistortedUV.Y - 0.5f) : 1.0f;
		OverscanFactors.Add(FMath::Max(OverscanX, OverscanY));
	}

	float* MaxOverscanFactor = Algo::MaxElement(OverscanFactors);
	const float FoundOverscan = MaxOverscanFactor ? *MaxOverscanFactor : 1.0f;
	
	return FoundOverscan;
}

void ULensFile::SetupNoDistortionOutput(ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData) const
{
	LensFileRendering::ClearDisplacementMap(LensHandler->GetUndistortionDisplacementMap());
	LensFileRendering::ClearDisplacementMap(LensHandler->GetDistortionDisplacementMap());
	OutDistortionData.DistortedUVs = UndistortedUVs;
	OutDistortionData.OverscanFactor = 1.0f;
	LensHandler->SetOverscanFactor(OutDistortionData.OverscanFactor);
}

bool ULensFile::EvaluateDistortionForParameters(float InFocus, float InZoom, ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData)
{
	//Parameter blending mode or no parameters to blend
	if (LensFileUtils::GDistortionParametersBlendMode == 0
		|| DistortionMapping.Num() <= 0)
	{
		FDistortionInfo DistortionPoint;
		DistortionPoint.Parameters.SetNumZeroed(LensInfo.LensModel.GetDefaultObject()->GetNumParameters());
		FIntrinsicParameters Intrinsic;
		EvaluateDistortionParameters(InFocus, InZoom, DistortionPoint);
		EvaluateIntrinsicParameters(InFocus, InZoom, Intrinsic);

		//Setup handler state based on evaluated parameters. If none were found, no distortion will be returned
		FLensDistortionState State;
		State.DistortionInfo = MoveTemp(DistortionPoint);
		State.PrincipalPoint = Intrinsic.PrincipalPoint;

		//@note Revisit once we moved FxFy mapping
		const float NormalizedFx = InZoom / LensInfo.SensorDimensions.X;
		const float AspectRatio = LensInfo.SensorDimensions.X / LensInfo.SensorDimensions.Y;
		State.FxFy = FVector2D(NormalizedFx, NormalizedFx * AspectRatio);
		
		LensHandler->SetDistortionState(State);

		OutDistortionData.OverscanFactor = LensHandler->ComputeOverscanFactor();
		LensHandler->SetOverscanFactor(OutDistortionData.OverscanFactor);

		//Draw displacement map associated with the new state
		LensHandler->ProcessCurrentDistortion();
	}
	else //DisplacementMap blending
	{
		int32 MinMinIndex = 0;
		int32 MinMaxIndex = 0;
		int32 MaxMinIndex = 0;
		int32 MaxMaxIndex = 0;

		if(LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(InFocus, InZoom, DistortionMapping, MinMinIndex, MinMaxIndex, MaxMinIndex, MaxMaxIndex))
		{
			if(!ensure(DistortionMapping.IsValidIndex(MinMinIndex)
			&& DistortionMapping.IsValidIndex(MinMaxIndex)
			&& DistortionMapping.IsValidIndex(MaxMinIndex)
			&& DistortionMapping.IsValidIndex(MaxMaxIndex)))
			{
				SetupNoDistortionOutput(LensHandler, OutDistortionData);
				return false;	
			}

			FDistortionMapPoint& MinMinPoint = DistortionMapping[MinMinIndex];
			FDistortionMapPoint& MinMaxPoint = DistortionMapping[MinMaxIndex];
			FDistortionMapPoint& MaxMinPoint = DistortionMapping[MaxMinIndex];
			FDistortionMapPoint& MaxMaxPoint = DistortionMapping[MaxMaxIndex];

			FDistortionData BlendedData;
			BlendedData.DistortedUVs.SetNumZeroed(UndistortedUVs.Num());

			FDisplacementMapBlendingParams Params;

			//Compute blended center shift 
			//This will be the value used for all possible distortion points to compute the resulting displacement map
			FIntrinsicMapPoint InterpPoint;
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FIntrinsicMapPoint>(InFocus, InZoom, IntrinsicMapping, InterpPoint);

			//Common info for distortion state
			FLensDistortionState State;
			State.PrincipalPoint = InterpPoint.Parameters.PrincipalPoint;

			//Helper function to compute the current distortion state
			const auto GetDistortionData = [this, &State, LensHandler](const FDistortionMapPoint& MapPoint, UTextureRenderTarget2D* UndistortionRenderTarget, UTextureRenderTarget2D* DistortionRenderTarget, FDistortionData& OutDistortionData)
			{
				State.DistortionInfo = MapPoint.DistortionInfo;

				//@note Revisit once we moved FxFy mapping
				const float NormalizedFx = MapPoint.Zoom / LensInfo.SensorDimensions.X;
				const float AspectRatio = LensInfo.SensorDimensions.X / LensInfo.SensorDimensions.Y;
				State.FxFy = FVector2D(NormalizedFx, NormalizedFx * AspectRatio);
				LensHandler->SetDistortionState(State);
				LensHandler->DrawUndistortionDisplacementMap(UndistortionRenderTarget);
				LensHandler->DrawDistortionDisplacementMap(DistortionRenderTarget);

				OutDistortionData.DistortedUVs = LensHandler->GetDistortedUVs(UndistortedUVs);
				OutDistortionData.OverscanFactor = LensHandler->ComputeOverscanFactor();	
			};

			//Single point case
			if (MinMinIndex == MaxMinIndex
				&& MaxMinIndex == MinMaxIndex
				&& MinMaxIndex == MaxMaxIndex)
			{
				Params.BlendType = EDisplacementMapBlendType::Passthrough;

				GetDistortionData(MinMinPoint, UndistortionDisplacementMapHolders[0], DistortionDisplacementMapHolders[0], BlendedData);
			}
			else if (MinMinIndex == MaxMinIndex && MinMaxIndex == MaxMaxIndex)
			{
				//Fixed focus 
				Params.BlendType = EDisplacementMapBlendType::Linear;
				Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InZoom, MinMinPoint.Zoom, MaxMaxPoint.Zoom);

				FDistortionData DistortionData[2];
				GetDistortionData(MinMinPoint, UndistortionDisplacementMapHolders[0], DistortionDisplacementMapHolders[0], DistortionData[0]);
				GetDistortionData(MaxMaxPoint, UndistortionDisplacementMapHolders[1], DistortionDisplacementMapHolders[1], DistortionData[1]);

				LensInterpolationUtils::Interpolate<FDistortionData>(Params.LinearBlendFactor, &DistortionData[0], &DistortionData[1], &BlendedData);
			}
			else if (MinMinIndex == MinMaxIndex && MaxMinIndex == MaxMaxIndex)
			{
				//Fixed zoom	
				Params.BlendType = EDisplacementMapBlendType::Linear;
				Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InFocus, MinMinPoint.Focus, MaxMaxPoint.Focus);

				FDistortionData DistortionData[2];
				GetDistortionData(MinMinPoint, UndistortionDisplacementMapHolders[0], DistortionDisplacementMapHolders[0], DistortionData[0]);
				GetDistortionData(MaxMaxPoint, UndistortionDisplacementMapHolders[1], DistortionDisplacementMapHolders[1], DistortionData[1]);
				
				LensInterpolationUtils::Interpolate<FDistortionData>(Params.LinearBlendFactor, &DistortionData[0], &DistortionData[1], &BlendedData);
			}
			else
			{
				//The current grid finder doesn't always yield points around the sample
				const float X2X1 = MaxMinPoint.Focus - MinMinPoint.Focus;
				const float Y2Y1 = MaxMaxPoint.Zoom - MinMinPoint.Zoom;
				const float Divider = X2X1 * Y2Y1;

				if (!FMath::IsNearlyZero(Divider))
				{
					Params.BlendType = EDisplacementMapBlendType::Bilinear;
					Params.DeltaMinX = InFocus - MinMinPoint.Focus;
					Params.DeltaMaxX = MaxMinPoint.Focus - InFocus;
					Params.DeltaMinY = InZoom - MinMinPoint.Zoom;
					Params.DeltaMaxY = MaxMaxPoint.Zoom - InZoom;
					Params.MainCoefficient = 1.0f / Divider;

					FDistortionData DistortionData[4];
					GetDistortionData(MinMinPoint, UndistortionDisplacementMapHolders[0], DistortionDisplacementMapHolders[0], DistortionData[0]);
					GetDistortionData(MinMaxPoint, UndistortionDisplacementMapHolders[1], DistortionDisplacementMapHolders[1], DistortionData[1]);
					GetDistortionData(MaxMinPoint, UndistortionDisplacementMapHolders[2], DistortionDisplacementMapHolders[2], DistortionData[2]);
					GetDistortionData(MaxMaxPoint, UndistortionDisplacementMapHolders[3], DistortionDisplacementMapHolders[3], DistortionData[3]);

					LensInterpolationUtils::BilinearInterpolate<FDistortionData>(Params.MainCoefficient
						, Params.DeltaMinX
						, Params.DeltaMaxX
						, Params.DeltaMinY
						, Params.DeltaMaxY
						, &DistortionData[0]
						, &DistortionData[1]
						, &DistortionData[2]
						, &DistortionData[3]
						, &BlendedData);
				}
				else
				{
					UE_LOG(LogCameraCalibration, Warning, TEXT("Could not interpolate data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
					return false;
				}
			}

			//Draw resulting undistortion displacement map for evaluation point
			LensFileRendering::DrawBlendedDisplacementMap(LensHandler->GetUndistortionDisplacementMap()
				, Params
				, UndistortionDisplacementMapHolders[0]
				, UndistortionDisplacementMapHolders[1]
				, UndistortionDisplacementMapHolders[2]
				, UndistortionDisplacementMapHolders[3]);

			//Draw resulting distortion displacement map for evaluation point
			LensFileRendering::DrawBlendedDisplacementMap(LensHandler->GetDistortionDisplacementMap()
				, Params
				, DistortionDisplacementMapHolders[0]
				, DistortionDisplacementMapHolders[1]
				, DistortionDisplacementMapHolders[2]
				, DistortionDisplacementMapHolders[3]);

			OutDistortionData = MoveTemp(BlendedData);
			LensHandler->SetOverscanFactor(OutDistortionData.OverscanFactor);
		}
		else
		{
			UE_LOG(LogCameraCalibration, Verbose, TEXT("Could not find distortion data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
			SetupNoDistortionOutput(LensHandler, OutDistortionData);
			return false;
		}
	}
	
	return true;
}

bool ULensFile::EvaluteDistortionForSTMaps(float InFocus, float InZoom, ULensDistortionModelHandlerBase* LensHandler, FDistortionData& OutDistortionData)
{
	if(DerivedDataInFlightCount > 0)
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Can't evaluate LensFile '%s' - %d data points still being computed. Clearing render target for no distortion"), *GetName(), DerivedDataInFlightCount);
		SetupNoDistortionOutput(LensHandler, OutDistortionData);
		return true;
	}

	if(CalibratedMapPoints.Num() <= 0)
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Can't evaluate LensFile '%s' - No calibrated maps"), *GetName());
		SetupNoDistortionOutput(LensHandler, OutDistortionData);
		return true;
	}

	int32 MinMinIndex = 0;
	int32 MinMaxIndex = 0;
	int32 MaxMinIndex = 0;
	int32 MaxMaxIndex = 0;

	if(LensInterpolationUtils::FindBilinearInterpIndices<FCalibratedMapPoint>(InFocus, InZoom, CalibratedMapPoints, MinMinIndex, MinMaxIndex, MaxMinIndex, MaxMaxIndex))
	{
		if(!ensure(CalibratedMapPoints.IsValidIndex(MinMinIndex)
		&& CalibratedMapPoints.IsValidIndex(MinMaxIndex)
		&& CalibratedMapPoints.IsValidIndex(MaxMinIndex)
		&& CalibratedMapPoints.IsValidIndex(MaxMaxIndex)))
		{
			SetupNoDistortionOutput(LensHandler, OutDistortionData);
			return false;	
		}

		const FCalibratedMapPoint& MinMinPoint = CalibratedMapPoints[MinMinIndex];
		const FCalibratedMapPoint& MinMaxPoint = CalibratedMapPoints[MinMaxIndex];
		const FCalibratedMapPoint& MaxMinPoint = CalibratedMapPoints[MaxMinIndex];
		const FCalibratedMapPoint& MaxMaxPoint = CalibratedMapPoints[MaxMaxIndex];

		FDistortionData BlendedData;
		BlendedData.DistortedUVs.SetNumZeroed(UndistortedUVs.Num());
		
		FDisplacementMapBlendingParams Params;
		UTextureRenderTarget2D* UndistortionTextureOne = nullptr;
		UTextureRenderTarget2D* UndistortionTextureTwo = nullptr;
		UTextureRenderTarget2D* UndistortionTextureThree = nullptr;
		UTextureRenderTarget2D* UndistortionTextureFour = nullptr;
		UTextureRenderTarget2D* DistortionTextureOne = nullptr;
		UTextureRenderTarget2D* DistortionTextureTwo = nullptr;
		UTextureRenderTarget2D* DistortionTextureThree = nullptr;
		UTextureRenderTarget2D* DistortionTextureFour = nullptr;

		//Single point case
		if (MinMinIndex == MaxMinIndex
			&& MaxMinIndex == MinMaxIndex
			&& MinMaxIndex == MaxMaxIndex)
		{
			Params.BlendType = EDisplacementMapBlendType::Passthrough;
			UndistortionTextureOne = MinMinPoint.DerivedDistortionData.UndistortionDisplacementMap;
			DistortionTextureOne = MinMinPoint.DerivedDistortionData.DistortionDisplacementMap;
			BlendedData = MinMinPoint.DerivedDistortionData.DistortionData;
		}
		else if (MinMinIndex == MaxMinIndex && MinMaxIndex == MaxMaxIndex)
		{
			//Fixed focus 
			Params.BlendType = EDisplacementMapBlendType::Linear;
			Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InZoom, MinMinPoint.Zoom, MaxMaxPoint.Zoom);
			UndistortionTextureOne = MinMinPoint.DerivedDistortionData.UndistortionDisplacementMap;
			UndistortionTextureTwo = MaxMaxPoint.DerivedDistortionData.UndistortionDisplacementMap;
			DistortionTextureOne = MinMinPoint.DerivedDistortionData.DistortionDisplacementMap;
			DistortionTextureTwo = MaxMaxPoint.DerivedDistortionData.DistortionDisplacementMap;
			LensInterpolationUtils::Interpolate<FDistortionData>(Params.LinearBlendFactor, &MinMinPoint.DerivedDistortionData.DistortionData, &MaxMaxPoint.DerivedDistortionData.DistortionData, &BlendedData);
		}
		else if (MinMinIndex == MinMaxIndex && MaxMinIndex == MaxMaxIndex)
		{
			//Fixed zoom	
			Params.BlendType = EDisplacementMapBlendType::Linear;
			Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InFocus, MinMinPoint.Focus, MaxMaxPoint.Focus);
			UndistortionTextureOne = MinMinPoint.DerivedDistortionData.UndistortionDisplacementMap;
			UndistortionTextureTwo = MaxMaxPoint.DerivedDistortionData.UndistortionDisplacementMap;
			DistortionTextureOne = MinMinPoint.DerivedDistortionData.DistortionDisplacementMap;
			DistortionTextureTwo = MaxMaxPoint.DerivedDistortionData.DistortionDisplacementMap;
			LensInterpolationUtils::Interpolate<FDistortionData>(Params.LinearBlendFactor, &MinMinPoint.DerivedDistortionData.DistortionData, &MaxMaxPoint.DerivedDistortionData.DistortionData, &BlendedData);
		}
		else
		{
			//The current grid finder doesn't always yield points around the sample
			const float X2X1 = MaxMinPoint.Focus - MinMinPoint.Focus;
			const float Y2Y1 = MaxMaxPoint.Zoom - MinMinPoint.Zoom;
			const float Divider = X2X1 * Y2Y1;

			if (!FMath::IsNearlyZero(Divider))
			{
				Params.BlendType = EDisplacementMapBlendType::Bilinear;
				Params.DeltaMinX = InFocus - MinMinPoint.Focus;
				Params.DeltaMaxX = MaxMinPoint.Focus - InFocus;
				Params.DeltaMinY = InZoom - MinMinPoint.Zoom;
				Params.DeltaMaxY = MaxMaxPoint.Zoom - InZoom;
				Params.MainCoefficient = 1.0f / Divider;
				UndistortionTextureOne = MinMinPoint.DerivedDistortionData.UndistortionDisplacementMap;
				UndistortionTextureTwo = MinMaxPoint.DerivedDistortionData.UndistortionDisplacementMap;
				UndistortionTextureThree = MaxMinPoint.DerivedDistortionData.UndistortionDisplacementMap;
				UndistortionTextureFour = MaxMaxPoint.DerivedDistortionData.UndistortionDisplacementMap;
				DistortionTextureOne = MinMinPoint.DerivedDistortionData.DistortionDisplacementMap;
				DistortionTextureTwo = MinMaxPoint.DerivedDistortionData.DistortionDisplacementMap;
				DistortionTextureThree = MaxMinPoint.DerivedDistortionData.DistortionDisplacementMap;
				DistortionTextureFour = MaxMaxPoint.DerivedDistortionData.DistortionDisplacementMap;
				LensInterpolationUtils::BilinearInterpolate<FDistortionData>(Params.MainCoefficient
				, Params.DeltaMinX
				, Params.DeltaMaxX
				, Params.DeltaMinY
				, Params.DeltaMaxY
				, &MinMinPoint.DerivedDistortionData.DistortionData
				, &MinMaxPoint.DerivedDistortionData.DistortionData
				, &MaxMaxPoint.DerivedDistortionData.DistortionData
				, &MaxMaxPoint.DerivedDistortionData.DistortionData
				, &BlendedData);
			}
			else
			{
				UE_LOG(LogCameraCalibration, Warning, TEXT("Could not interpolate data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
				return false;
			}
		}

		//Compute blended principal point to apply on top of blended displacement map
		FIntrinsicMapPoint InterpPoint;
		LensInterpolationUtils::FIZMappingBilinearInterpolation<FIntrinsicMapPoint>(InFocus, InZoom, IntrinsicMapping, InterpPoint);

		Params.PrincipalPoint = InterpPoint.Parameters.PrincipalPoint;

		//Draw resulting undistortion displacement map for evaluation point
		LensFileRendering::DrawBlendedDisplacementMap(LensHandler->GetUndistortionDisplacementMap()
			, Params
			, UndistortionTextureOne
			, UndistortionTextureTwo
			, UndistortionTextureThree
			, UndistortionTextureFour);

		//Draw resulting displacement map for evaluation point
		if(LensFileRendering::DrawBlendedDisplacementMap(LensHandler->GetDistortionDisplacementMap()
			, Params
			, DistortionTextureOne
			, DistortionTextureTwo
			, DistortionTextureThree
			, DistortionTextureFour))
		{
			OutDistortionData.OverscanFactor = ComputeOverscan(BlendedData, Params.PrincipalPoint);
			LensHandler->SetOverscanFactor(OutDistortionData.OverscanFactor);
		}
	}
	else
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Could not find distortion data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
		SetupNoDistortionOutput(LensHandler, OutDistortionData);
		return false;
	}

	return true;
}

bool ULensFile::EvaluateNodalPointOffset(float InFocus, float InZoom, FNodalPointOffset& OutEvaluatedValue)
{
	if (NodalOffsetMapping.Num() <= 0)
	{
		return false;
	}

	if (NodalOffsetMapping.Num() == 1)
	{
		OutEvaluatedValue = NodalOffsetMapping[0].NodalOffset;
		return true;
	}

	FNodalOffsetMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FNodalOffsetMapPoint>(InFocus, InZoom, NodalOffsetMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.NodalOffset);
	}

	return bSuccess;
}

bool ULensFile::HasFocusEncoderMapping() const
{
	return EncoderMapping.Focus.Num() > 0;
}

bool ULensFile::EvaluateNormalizedFocus(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Focus;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue <= RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}

bool ULensFile::HasIrisEncoderMapping() const
{
	return EncoderMapping.Iris.Num() > 0;

}

float ULensFile::EvaluateNormalizedIris(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Iris;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue <= RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}

bool ULensFile::HasZoomEncoderMapping() const
{
	return EncoderMapping.Zoom.Num() > 0;
}

float ULensFile::EvaluateNormalizedZoom(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Zoom;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue <= RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}

void ULensFile::OnDistortionDerivedDataJobCompleted(const FDerivedDistortionDataJobOutput& JobOutput)
{
	//Keep track of jobs being processed
	--DerivedDataInFlightCount;
	
	const FGuid PointIdentifier = JobOutput.Identifier;
	if(FCalibratedMapPoint* AssociatedPoint = CalibratedMapPoints.FindByPredicate([PointIdentifier](const FCalibratedMapPoint& Other){ return Other.GetIdentifier() == PointIdentifier; }))
	{
		if (JobOutput.Result == EDerivedDistortionDataResult::Success)
		{
			AssociatedPoint->DerivedDistortionData.DistortionData.DistortedUVs = JobOutput.EdgePointsDistortedUVs;
		}
		else
		{
			UE_LOG(LogCameraCalibration, Warning, TEXT("Could not derive distortion data for calibrated map point with Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), AssociatedPoint->Focus, AssociatedPoint->Zoom, *GetName());
		}
	}
}

void ULensFile::PostInitProperties()
{
	Super::PostInitProperties();
	
	//Create displacement maps used when blending them together to get final distortion map
	UndistortionDisplacementMapHolders.Reserve(DisplacementMapHolderCount);
	DistortionDisplacementMapHolders.Reserve(DisplacementMapHolderCount);
	for (int32 Index = 0; Index < DisplacementMapHolderCount; ++Index)
	{
		UTextureRenderTarget2D* NewMap = LensFileUtils::CreateDisplacementMapRenderTarget(GetTransientPackage());
		UndistortionDisplacementMapHolders.Add(NewMap);
		DistortionDisplacementMapHolders.Add(NewMap);
	}
}

void ULensFile::Tick(float DeltaTime)
{
	if (CalibratedMapProcessor)
	{
		CalibratedMapProcessor->Update();
	}

	UpdateDerivedData();
}

TStatId ULensFile::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULensFile, STATGROUP_Tickables);
}

void ULensFile::UpdateDerivedData()
{
	if(DataMode == ELensDataMode::STMap)
	{
		for(FCalibratedMapPoint& MapPoint : CalibratedMapPoints)
		{
			if (MapPoint.DerivedDistortionData.bIsDirty)
			{
				//Create required undistortion texture for newly added points
				if(MapPoint.DerivedDistortionData.UndistortionDisplacementMap == nullptr)
				{
					MapPoint.DerivedDistortionData.UndistortionDisplacementMap = LensFileUtils::CreateDisplacementMapRenderTarget(this);
				}

				//Create required distortion texture for newly added points
				if (MapPoint.DerivedDistortionData.DistortionDisplacementMap == nullptr)
				{
					MapPoint.DerivedDistortionData.DistortionDisplacementMap = LensFileUtils::CreateDisplacementMapRenderTarget(this);
				}

				check(MapPoint.DerivedDistortionData.UndistortionDisplacementMap);
				check(MapPoint.DerivedDistortionData.DistortionDisplacementMap);

				FDerivedDistortionDataJobArgs JobArgs;
				JobArgs.Identifier = MapPoint.GetIdentifier();
				JobArgs.SourceDistortionMap = MapPoint.DistortionMap;
				JobArgs.OutputUndistortionDisplacementMap = MapPoint.DerivedDistortionData.UndistortionDisplacementMap;
				JobArgs.OutputDistortionDisplacementMap = MapPoint.DerivedDistortionData.DistortionDisplacementMap;
				JobArgs.JobCompletedCallback.BindUObject(this, &ULensFile::OnDistortionDerivedDataJobCompleted);
				if (CalibratedMapProcessor->PushDerivedDistortionDataJob(MoveTemp(JobArgs)))
				{
					++DerivedDataInFlightCount;
					MapPoint.DerivedDistortionData.bIsDirty = false;
				}
			}
		}
	}
}

ULensFile* FLensFilePicker::GetLensFile() const
{
	ULensFile* ReturnedLens = nullptr;

	if (bOverrideDefaultLensFile)
	{
		ReturnedLens = LensFile;
	}
	else if(GEngine)
	{
		UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		ReturnedLens = SubSystem->GetDefaultLensFile();
	}

	return ReturnedLens;
}
