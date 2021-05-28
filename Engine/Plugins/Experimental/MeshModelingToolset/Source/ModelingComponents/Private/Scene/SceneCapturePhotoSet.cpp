// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/SceneCapturePhotoSet.h"
#include "EngineUtils.h"

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
		case ERenderCaptureType::CombinedMRS:
			bEnablePackedMRS = bEnabled;
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

	// Workaround for Nanite scene proxies visibility
	// Unregister all components to remove unwanted proxies from the scene. This is currently the only way to "hide" nanite meshes.
	TSet<AActor*> VisibleActorsSet(VisibleActors);
	TArray<AActor*> ActorsToRegister;
	for (TActorIterator<AActor> Actor(TargetWorld); Actor; ++Actor)
	{
		if (!VisibleActorsSet.Contains(*Actor))
		{
			Actor->UnregisterAllComponents();
			ActorsToRegister.Add(*Actor);
		}
	}

	FWorldRenderCapture RenderCapture;
	RenderCapture.SetWorld(TargetWorld);
	RenderCapture.SetVisibleActors(VisibleActors);
	RenderCapture.SetDimensions(PhotoDimensions);
	if (bWriteDebugImages)
	{
		RenderCapture.SetEnableWriteDebugImage(true, 0, DebugImagesFolderName);
	}

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

		FSpatialPhoto3f BasePhoto3f;
		BasePhoto3f.Frame = ViewFrame;
		BasePhoto3f.NearPlaneDist = NearPlaneDist;
		BasePhoto3f.HorzFOVDegrees = HorizontalFOVDegrees;
		BasePhoto3f.Dimensions = PhotoDimensions;

		auto CaptureImageTypeFunc_3f = [&BasePhoto3f, &RenderCapture](ERenderCaptureType CaptureType, FSpatialPhotoSet3f& PhotoSet)
		{
			FSpatialPhoto3f NewPhoto = BasePhoto3f;
			FImageAdapter Image(&NewPhoto.Image);
			RenderCapture.CaptureFromPosition(CaptureType, NewPhoto.Frame, NewPhoto.HorzFOVDegrees, NewPhoto.NearPlaneDist, Image);
			PhotoSet.Add(MoveTemp(NewPhoto));
		};

		FSpatialPhoto1f BasePhoto1f;
		BasePhoto1f.Frame = ViewFrame;
		BasePhoto1f.NearPlaneDist = NearPlaneDist;
		BasePhoto1f.HorzFOVDegrees = HorizontalFOVDegrees;
		BasePhoto1f.Dimensions = PhotoDimensions;

		auto CaptureImageTypeFunc_1f = [&BasePhoto1f, &RenderCapture](ERenderCaptureType CaptureType, FSpatialPhotoSet1f& PhotoSet)
		{
			FSpatialPhoto1f NewPhoto = BasePhoto1f;
			FImageAdapter Image(&NewPhoto.Image);
			RenderCapture.CaptureFromPosition(CaptureType, NewPhoto.Frame, NewPhoto.HorzFOVDegrees, NewPhoto.NearPlaneDist, Image);
			PhotoSet.Add(MoveTemp(NewPhoto));
		};

		if (bEnableBaseColor)
		{
			CaptureImageTypeFunc_3f(ERenderCaptureType::BaseColor, BaseColorPhotoSet);
		}
		if (bEnableRoughness)
		{
			CaptureImageTypeFunc_1f(ERenderCaptureType::Roughness, RoughnessPhotoSet);
		}
		if (bEnableSpecular)
		{
			CaptureImageTypeFunc_1f(ERenderCaptureType::Specular, SpecularPhotoSet);
		}
		if (bEnableMetallic)
		{
			CaptureImageTypeFunc_1f(ERenderCaptureType::Metallic, MetallicPhotoSet);
		}
		if (bEnablePackedMRS)
		{
			CaptureImageTypeFunc_3f(ERenderCaptureType::CombinedMRS, PackedMRSPhotoSet);
		}
		if (bEnableWorldNormal)
		{
			CaptureImageTypeFunc_3f(ERenderCaptureType::WorldNormal, WorldNormalPhotoSet);
		}
		if (bEnableEmissive)
		{
			CaptureImageTypeFunc_3f(ERenderCaptureType::Emissive, EmissivePhotoSet);
		}
	}

	// Workaround for Nanite scene proxies visibility
	// Reregister all components we previously unregistered
	for (AActor* Actor : ActorsToRegister)
	{
		Actor->RegisterAllComponents();
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
	BaseColor = FVector3f(0, 0, 0);
	Roughness = 0.0f;
	Specular = 0.0f;
	Metallic = 0.0f;
	Emissive = FVector3f(0, 0, 0);
	WorldNormal = FVector3f(0, 0, 1);
}

FVector3f FSceneCapturePhotoSet::FSceneSample::GetValue3f(ERenderCaptureType CaptureType) const
{
	switch (CaptureType)
	{
	case ERenderCaptureType::BaseColor:
		return BaseColor;
	case ERenderCaptureType::WorldNormal:
		return WorldNormal;
	case ERenderCaptureType::Roughness:
		return Roughness * FVector3f::One();
	case ERenderCaptureType::Metallic:
		return Metallic * FVector3f::One();
	case ERenderCaptureType::Specular:
		return Specular * FVector3f::One();
	case ERenderCaptureType::Emissive:
		return Emissive;
	default:
		check(false);
	}
	return FVector3f::Zero();
}

FVector4f FSceneCapturePhotoSet::FSceneSample::GetValue4f(ERenderCaptureType CaptureType) const
{
	switch (CaptureType)
	{
	case ERenderCaptureType::BaseColor:
		return FVector4f(BaseColor.X, BaseColor.Y, BaseColor.Z, 1.0f);
	case ERenderCaptureType::WorldNormal:
		return FVector4f(WorldNormal.X, WorldNormal.Y, WorldNormal.Z, 1.0f);
	case ERenderCaptureType::Roughness:
		return FVector4f(Roughness, Roughness, Roughness, 1.0f);
	case ERenderCaptureType::Metallic:
		return FVector4f(Metallic, Metallic, Metallic, 1.0f);
	case ERenderCaptureType::Specular:
		return FVector4f(Specular, Specular, Specular, 1.0f);
	case ERenderCaptureType::CombinedMRS:
		return FVector4f(Metallic, Roughness, Specular, 1.0f);
	case ERenderCaptureType::Emissive:
		return FVector4f(Emissive.X, Emissive.Y, Emissive.Z, 1.0f);
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
	if (SampleChannels.bCombinedMRS)
	{
		FVector3f MRSValue(DefaultsInResultsOut.Metallic, DefaultsInResultsOut.Roughness, DefaultsInResultsOut.Specular);
		MRSValue = PackedMRSPhotoSet.ComputeSample(Position, Normal, VisibilityFunction, MRSValue);
		DefaultsInResultsOut.Metallic = MRSValue.X;
		DefaultsInResultsOut.Roughness = MRSValue.Y;
		DefaultsInResultsOut.Specular = MRSValue.Z;
		DefaultsInResultsOut.HaveValues.bMetallic = true;
		DefaultsInResultsOut.HaveValues.bRoughness = true;
		DefaultsInResultsOut.HaveValues.bSpecular = true;
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

void FSceneCapturePhotoSet::SetEnableWriteDebugImages(bool bEnable, FString FolderName)
{
	bWriteDebugImages = bEnable;
	if (FolderName.Len() > 0)
	{
		DebugImagesFolderName = FolderName;
	}
}