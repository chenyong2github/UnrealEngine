// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensFile.h"

#include "Algo/MaxElement.h"
#include "CalibratedMapProcessor.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LensDistortionLog.h"
#include "LensDistortionSubsystem.h"
#include "LensFileRendering.h"
#include "LensInterpolationUtils.h"


namespace LensFileUtils
{
	UTextureRenderTarget2D* CreateDisplacementMapRenderTarget(UObject* Outer)
	{
		check(Outer);

		//Will be good using a project settings or global resolution that user can change
		const FIntPoint Size{256,256};

		UTextureRenderTarget2D* NewRenderTarget2D = NewObject<UTextureRenderTarget2D>(Outer, MakeUniqueObjectName(Outer, UTextureRenderTarget2D::StaticClass(), TEXT("LensDisplacementMap")), RF_Public);
		NewRenderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
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
}

ULensFile::ULensFile()
{
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
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FCalibratedMapPoint, DistortionMap))
		{
			//When the distortion map (stmap) changes, flag associated derived data as dirty to update it
			check(PropertyChangedEvent.PropertyChain.GetActiveMemberNode() && PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue());

			const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
			const int32 ModifiedIndex = PropertyChangedEvent.GetArrayIndex(MemberPropertyName.ToString());
			check(CalibratedMapPoints.IsValidIndex(ModifiedIndex));

			FCalibratedMapPoint& MapPoint = CalibratedMapPoints[ModifiedIndex];
			MapPoint.DerivedDistortionData.bIsDirty = true;
		}
	}
	
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif //WITH_EDITOR

bool ULensFile::EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionParameters& OutEvaluatedValue)
{
	if (DistortionMapping.Num() <= 0)
	{
		return false;
	}

	if (DistortionMapping.Num() == 1)
	{
		OutEvaluatedValue = DistortionMapping[0].Parameters;
		return true;
	}

	FDistortionMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(InFocus, InZoom, DistortionMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.Parameters);
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

bool ULensFile::EvaluateDistortionData(float InFocus, float InZoom, UTextureRenderTarget2D* OutDisplacementMap,	FDistortionData& OutDistortionData) const
{
	if (OutDisplacementMap == nullptr)
	{
		UE_LOG(LogLensDistortion, Warning, TEXT("Can't evaluate LensFile '%s' - Invalid output RenderTarget"), *GetName());
		return false;
	}

	if(DerivedDataInFlightCount > 0)
	{
		UE_LOG(LogLensDistortion, Verbose, TEXT("Can't evaluate LensFile '%s' - %d data points still being computed"), *GetName(), DerivedDataInFlightCount);
		return false;
	}

	if(DataMode == ELensDataMode::Coefficients)
	{
		if(DistortionMapping.Num() <= 0)
		{
			UE_LOG(LogLensDistortion, Warning, TEXT("Can't evaluate LensFile '%s' - No distortion coefficients"), *GetName());
			return false;
		}
	}
	else
	{
		check(DataMode == ELensDataMode::STMap);

		if(CalibratedMapPoints.Num() <= 0)
		{
			UE_LOG(LogLensDistortion, Warning, TEXT("Can't evaluate LensFile '%s' - No calibrated maps"), *GetName());
			return false;
		}	
	}
	
	bool bSuccess = false;
	if (CalibratedMapPoints.Num())
	{
		int32 MinMinIndex = 0;
		int32 MinMaxIndex = 0;
		int32 MaxMinIndex = 0;
		int32 MaxMaxIndex = 0;

		bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FCalibratedMapPoint>(InFocus, InZoom, CalibratedMapPoints, MinMinIndex, MinMaxIndex, MaxMinIndex, MaxMaxIndex);
		if (bSuccess)
		{
			check(CalibratedMapPoints.IsValidIndex(MinMinIndex));
			check(CalibratedMapPoints.IsValidIndex(MinMaxIndex));
			check(CalibratedMapPoints.IsValidIndex(MaxMinIndex));
			check(CalibratedMapPoints.IsValidIndex(MaxMaxIndex));

			const FCalibratedMapPoint& MinMinPoint = CalibratedMapPoints[MinMinIndex];
			const FCalibratedMapPoint& MinMaxPoint = CalibratedMapPoints[MinMaxIndex];
			const FCalibratedMapPoint& MaxMinPoint = CalibratedMapPoints[MaxMinIndex];
			const FCalibratedMapPoint& MaxMaxPoint = CalibratedMapPoints[MaxMaxIndex];

			FDistortionData BlendedData;
			BlendedData.DistortedUVs.SetNumZeroed(8);
			
			FDisplacementMapBlendingParams Params;
			UTextureRenderTarget2D* TextureOne = nullptr;
			UTextureRenderTarget2D* TextureTwo = nullptr;
			UTextureRenderTarget2D* TextureThree = nullptr;
			UTextureRenderTarget2D* TextureFour = nullptr;

			//Single point case
			if (MinMinIndex == MaxMinIndex
				&& MaxMinIndex == MinMaxIndex
				&& MinMaxIndex == MaxMaxIndex)
			{
				Params.BlendType = EDisplacementMapBlendType::Passthrough;
				TextureOne = MinMinPoint.DerivedDistortionData.DisplacementMap;
				BlendedData = MinMinPoint.DerivedDistortionData.DistortionData;
			}
			else if (MinMinIndex == MaxMinIndex && MinMaxIndex == MaxMaxIndex)
			{
				//Fixed focus 
				Params.BlendType = EDisplacementMapBlendType::Linear;
				Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InZoom, MinMinPoint.Zoom, MaxMaxPoint.Zoom);
				TextureOne = MinMinPoint.DerivedDistortionData.DisplacementMap;
				TextureTwo = MaxMaxPoint.DerivedDistortionData.DisplacementMap;
				LensInterpolationUtils::Interpolate<FDistortionData>(Params.LinearBlendFactor, &MinMinPoint.DerivedDistortionData.DistortionData, &MaxMaxPoint.DerivedDistortionData.DistortionData, &BlendedData);
			}
			else if (MinMinIndex == MinMaxIndex && MaxMinIndex == MaxMaxIndex)
			{
				//Fixed zoom	
				Params.BlendType = EDisplacementMapBlendType::Linear;
				Params.LinearBlendFactor = LensInterpolationUtils::GetBlendFactor(InFocus, MinMinPoint.Focus, MaxMaxPoint.Focus);
				TextureOne = MinMinPoint.DerivedDistortionData.DisplacementMap;
				TextureTwo = MaxMaxPoint.DerivedDistortionData.DisplacementMap;
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
					TextureOne = MinMinPoint.DerivedDistortionData.DisplacementMap;
					TextureTwo = MinMaxPoint.DerivedDistortionData.DisplacementMap;
					TextureThree = MaxMinPoint.DerivedDistortionData.DisplacementMap;
					TextureFour = MaxMaxPoint.DerivedDistortionData.DisplacementMap;
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
					UE_LOG(LogLensDistortion, Warning, TEXT("Could not interpolate data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
					return false;
				}
			}

			//Compute blended center shift to apply on top of blended displacement map
			FIntrinsicMapPoint InterpPoint;
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FIntrinsicMapPoint>(InFocus, InZoom, IntrinsicMapping, InterpPoint);

			//Draw resulting displacement map for evaluation point
			Params.CenterShift = InterpPoint.Parameters.CenterShift;
			bSuccess = LensFileRendering::DrawBlendedDisplacementMap(OutDisplacementMap
				, Params
				, TextureOne
				, TextureTwo
				, TextureThree
				, TextureFour);

			//If everything has went well, update output overscan
			if (bSuccess)
			{
				OutDistortionData.OverscanFactor = ComputeOverscan(BlendedData, Params.CenterShift);
			}
		}
		else
		{
			UE_LOG(LogLensDistortion, Warning, TEXT("Could not find distortion data for Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), InFocus, InZoom, *GetName());
		}
	}

	return bSuccess;
}

float ULensFile::ComputeOverscan(const FDistortionData& DerivedData, FVector2D CenterShift) const
{
	static const TArray<FVector2D> UndistortedUVs
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
		const FVector2D& DistortedUV = DerivedData.DistortedUVs[Index] + (CenterShift - FVector2D(0.5f, 0.5f)) * 2.0f;
		const float OverscanX = (UndistortedUV.X != 0.5f) ? (DistortedUV.X - 0.5f) / (UndistortedUV.X - 0.5f) : 1.0f;
		const float OverscanY = (UndistortedUV.Y != 0.5f) ? (DistortedUV.Y - 0.5f) / (UndistortedUV.Y - 0.5f) : 1.0f;
		OverscanFactors.Add(FMath::Max(OverscanX, OverscanY));
	}

	float* MaxOverscanFactor = Algo::MaxElement(OverscanFactors);
	float FoundOverscan = MaxOverscanFactor ? *MaxOverscanFactor : 1.0f;
	
	static float ForcedOverscan = 1.0f;
	if (ForcedOverscan > 1.0f)
	{
		FoundOverscan = ForcedOverscan;
	}

	return FoundOverscan;
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
			UE_LOG(LogLensDistortion, Warning, TEXT("Could not derive distortion data for calibrated map point with Focus = '%0.2f' and Zoom = '%0.2f' on LensFile '%s'"), AssociatedPoint->Focus, AssociatedPoint->Zoom, *GetName());
		}
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
		//Assert if new mode is added
		check(DataMode == ELensDataMode::STMap);

		for(FCalibratedMapPoint& MapPoint : CalibratedMapPoints)
		{
			if (MapPoint.DerivedDistortionData.bIsDirty)
			{
				//Create required texture for newly added points
				if(MapPoint.DerivedDistortionData.DisplacementMap == nullptr)
				{
					MapPoint.DerivedDistortionData.DisplacementMap = LensFileUtils::CreateDisplacementMapRenderTarget(this);
				}

				check(MapPoint.DerivedDistortionData.DisplacementMap);

				FDerivedDistortionDataJobArgs JobArgs;
				JobArgs.Identifier = MapPoint.GetIdentifier();
				JobArgs.SourceDistortionMap = MapPoint.DistortionMap;
				JobArgs.OutputDisplacementMap = MapPoint.DerivedDistortionData.DisplacementMap;
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
		ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
		ReturnedLens = SubSystem->GetDefaultLensFile();
	}

	return ReturnedLens;
}
