// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/SceneCapturePhotoSet.h"

using namespace UE::Geometry;

void FSceneCapturePhotoSet::SetCaptureSceneActors(UWorld* World, const TArray<AActor*>& Actors)
{
	this->TargetWorld = World;
	this->VisibleActors = Actors;
}



void FSceneCapturePhotoSet::SetCaptureTypeEnabled(ERenderCaptureType CaptureType, bool bEnabled)
{
	switch (CaptureType)
	{
	case ERenderCaptureType::BaseColor:
			bEnableBaseColor = bEnabled;
			break;
		case ERenderCaptureType::WorldNormal:
			bEnableWorldNormal = bEnabled;
			break;
		case ERenderCaptureType::Roughness:
			bEnableRoughness = bEnabled;
			break;
		case ERenderCaptureType::Metallic:
			bEnableMetallic = bEnabled;
			break;
		case ERenderCaptureType::Specular:
			bEnableSpecular = bEnabled;
			break;
		case ERenderCaptureType::Emissive:
			bEnableEmissive = bEnabled;
			break;
		default:
			check(false);
	}
}



void FSceneCapturePhotoSet::AddStandardExteriorCapturesFromBoundingBox(
	FImageDimensions PhotoDimensions,
	double HorizontalFOVDegrees,
	double NearPlaneDist,
	bool bFaces,
	bool bUpperCorners,
	bool bLowerCorners)
{
	TArray<FVector3d> Directions;

	if (bFaces)
	{
		Directions.Add(FVector3d::UnitX());
		Directions.Add(-FVector3d::UnitX());
		Directions.Add(FVector3d::UnitY());
		Directions.Add(-FVector3d::UnitY());
		Directions.Add(FVector3d::UnitZ());
		Directions.Add(-FVector3d::UnitZ());
	}
	if (bUpperCorners)
	{
		Directions.Add(Normalized(FVector3d(1, 1, -1)));
		Directions.Add(Normalized(FVector3d(-1, 1, -1)));
		Directions.Add(Normalized(FVector3d(1, -1, -1)));
		Directions.Add(Normalized(FVector3d(-1, -1, -1)));
	}
	if (bLowerCorners)
	{
		Directions.Add(Normalized(FVector3d(1, 1, 1)));
		Directions.Add(Normalized(FVector3d(-1, 1, 1)));
		Directions.Add(Normalized(FVector3d(1, -1, 1)));
		Directions.Add(Normalized(FVector3d(-1, -1, 1)));
	}
	AddExteriorCaptures(PhotoDimensions, HorizontalFOVDegrees, NearPlaneDist, Directions);
}



void FSceneCapturePhotoSet::AddExteriorCaptures(
	FImageDimensions PhotoDimensions,
	double HorizontalFOVDegrees,
	double NearPlaneDist,
	const TArray<FVector3d>& Directions)
{
	check(this->TargetWorld != nullptr);

	FWorldRenderCapture RenderCapture;
	RenderCapture.SetWorld(TargetWorld);
	RenderCapture.SetVisibleActors(VisibleActors);
	RenderCapture.SetDimensions(PhotoDimensions);

	// this tells us origin and radius - could be view-dependent...
	FSphere RenderSphere = RenderCapture.ComputeContainingRenderSphere(HorizontalFOVDegrees);

	int32 NumDirections = Directions.Num();
	for (int32 di = 0; di < NumDirections; ++di)
	{
		FVector3d ViewDirection = Directions[di];
		ViewDirection.Normalize();

		FFrame3d ViewFrame;
		ViewFrame.AlignAxis(0, ViewDirection);
		ViewFrame.ConstrainedAlignAxis(2, FVector3d::UnitZ(), ViewFrame.X());
		ViewFrame.Origin = (FVector3d)RenderSphere.Center;
		ViewFrame.Origin -= (double)RenderSphere.W * ViewFrame.X();

		FSpatialPhoto4f BasePhoto;
		BasePhoto.Frame = ViewFrame;
		BasePhoto.NearPlaneDist = NearPlaneDist;
		BasePhoto.HorzFOVDegrees = HorizontalFOVDegrees;
		BasePhoto.Dimensions = PhotoDimensions;

		auto CaptureImageTypeFunc = [&BasePhoto, &RenderCapture](ERenderCaptureType CaptureType, FSpatialPhotoSet4f& PhotoSet)
		{
			FSpatialPhoto4f NewPhoto = BasePhoto;
			RenderCapture.CaptureFromPosition(CaptureType, NewPhoto.Frame, NewPhoto.HorzFOVDegrees, NewPhoto.NearPlaneDist, NewPhoto.Image);
			PhotoSet.Add(MoveTemp(NewPhoto));
		};

		if (bEnableBaseColor)
		{
			CaptureImageTypeFunc(ERenderCaptureType::BaseColor, BaseColorPhotoSet);
		}
		if (bEnableRoughness)
		{
			CaptureImageTypeFunc(ERenderCaptureType::Roughness, RoughnessPhotoSet);
		}
		if (bEnableSpecular)
		{
			CaptureImageTypeFunc(ERenderCaptureType::Specular, SpecularPhotoSet);
		}
		if (bEnableMetallic)
		{
			CaptureImageTypeFunc(ERenderCaptureType::Metallic, MetallicPhotoSet);
		}
		if (bEnableWorldNormal)
		{
			CaptureImageTypeFunc(ERenderCaptureType::WorldNormal, WorldNormalPhotoSet);
		}
		if (bEnableEmissive)
		{
			CaptureImageTypeFunc(ERenderCaptureType::Emissive, EmissivePhotoSet);
		}
	}

}



void FSceneCapturePhotoSet::OptimizePhotoSets()
{
	// todo:
	//  1) crop photos to regions with actual pixels
	//  2) pack into fewer photos  (eg pack spec/rough/metallic)
	//  3) RLE encoding or other compression
}



FSceneCapturePhotoSet::FSceneSample::FSceneSample()
{
	HaveValues = FRenderCaptureTypeFlags::None();
	BaseColor = FVector4f(0, 0, 0, 1);
	Roughness = FVector4f(0, 0, 0, 1);
	Specular = FVector4f(0, 0, 0, 1);
	Metallic = FVector4f(0, 0, 0, 1);
	Emissive = FVector4f(0, 0, 0, 1);
	WorldNormal = FVector4f(0, 0, 1, 1);
}

FVector4f FSceneCapturePhotoSet::FSceneSample::GetValue(ERenderCaptureType CaptureType) const
{
	switch (CaptureType)
	{
	case ERenderCaptureType::BaseColor:
		return BaseColor;
	case ERenderCaptureType::WorldNormal:
		return WorldNormal;
	case ERenderCaptureType::Roughness:
		return Roughness;
	case ERenderCaptureType::Metallic:
		return Metallic;
	case ERenderCaptureType::Specular:
		return Specular;
	case ERenderCaptureType::Emissive:
		return Emissive;
	default:
		check(false);
	}
	return FVector4f::Zero();
}


bool FSceneCapturePhotoSet::ComputeSample(
	const FRenderCaptureTypeFlags& SampleChannels,
	const FVector3d& Position,
	const FVector3d& Normal,
	TFunctionRef<bool(const FVector3d&, const FVector3d&)> VisibilityFunction,
	FSceneSample& DefaultsInResultsOut) const
{
	// This could be much more efficient if (eg) we knew that all the photo sets have
	// the same captures, then the query only has to be done once and can be used to sample each specific photo

	if (SampleChannels.bBaseColor)
	{
		DefaultsInResultsOut.BaseColor =
			BaseColorPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.BaseColor);
		DefaultsInResultsOut.HaveValues.bBaseColor = true;
	}
	if (SampleChannels.bRoughness)
	{
		DefaultsInResultsOut.Roughness =
			RoughnessPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.Roughness);
		DefaultsInResultsOut.HaveValues.bRoughness = true;
	}
	if (SampleChannels.bSpecular)
	{
		DefaultsInResultsOut.Specular =
			SpecularPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.Specular);
		DefaultsInResultsOut.HaveValues.bSpecular = true;
	}
	if (SampleChannels.bMetallic)
	{
		DefaultsInResultsOut.Metallic =
			MetallicPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.Metallic);
		DefaultsInResultsOut.HaveValues.bMetallic = true;
	}
	if (SampleChannels.bEmissive)
	{
		DefaultsInResultsOut.Emissive =
			EmissivePhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.Emissive);
		DefaultsInResultsOut.HaveValues.bEmissive = true;
	}
	if (SampleChannels.bWorldNormal)
	{
		DefaultsInResultsOut.WorldNormal =
			WorldNormalPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, DefaultsInResultsOut.WorldNormal);
		DefaultsInResultsOut.HaveValues.bWorldNormal = true;
	}

	return true;
}