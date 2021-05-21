// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "FrameTypes.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"


namespace UE
{
namespace Geometry
{

/**
 * TSpatialPhoto represents a 2D image located in 3D space, ie the image plus camera parameters, 
 * which is essentially a "Photograph" of some 3D scene (hence the name)
 */
template<typename PixelType>
struct TSpatialPhoto
{
	/** Coordinate system of the view camera - X() is forward, Z() is up */
	FFrame3d Frame;

	/** Near-plane distance for the camera, image pixels lie on this plane */
	double NearPlaneDist = 1.0;
	/** Horizontal Field-of-View of the camera in degrees (full FOV, so generally calculations will use half this value) */
	double HorzFOVDegrees = 90.0;
	/** Pixel dimensions of the photo image */
	FImageDimensions Dimensions;
	/** Pixels of the image */
	TImageBuilder<PixelType> Image;
};
typedef TSpatialPhoto<FVector4f> FSpatialPhoto4f;
typedef TSpatialPhoto<FVector3f> FSpatialPhoto3f;
typedef TSpatialPhoto<float> FSpatialPhoto1f;



/**
 * TSpatialPhotoSet is a set of TSpatialPhotos. 
 * The ComputeSample() function can be used to determine the value "seen"
 * by the photo set at a given 3D position/normal, if possible.
 */
template<typename PixelType, typename RealType>
class TSpatialPhotoSet
{
public:
	
	/** Add a photo to the photo set via move operation */
	void Add(TSpatialPhoto<PixelType>&& Photo)
	{
		TSharedPtr<TSpatialPhoto<PixelType>, ESPMode::ThreadSafe> NewPhoto = MakeShared<TSpatialPhoto<PixelType>, ESPMode::ThreadSafe>(MoveTemp(Photo));
		Photos.Add(NewPhoto);
	}

	/** @return the number of photos in the photo set */
	int32 Num() const { return Photos.Num(); }

	/** @return the photo at the given index */
	const TSpatialPhoto<PixelType>& Get(int32 Index) const { return *Photos[Index]; }

	/**
	 * Estimate a pixel value at the given 3D Position/Normal using the PhotoSet. 
	 * This is effectively a reprojection process, that tries to find the "best"
	 * pixel value in the photo set that projects onto the given Position/Normal.
	 * 
	 * A position may be visible from multiple photos, in this case the dot-product
	 * between the view vector and normal is used to decide which photo pixel to use.
	 * 
	 * VisibilityFunction is used to determine if a 3D point is visible from the given photo point.
	 * Generally the caller would implement some kind of raycast to do this.
	 * 
	 * @returns the best valid sample if found, or DefaultValue if no suitable sample is available
	 */
	PixelType ComputeSample(
		const FVector3d& Position, 
		const FVector3d& Normal, 
		TFunctionRef<bool(const FVector3d&,const FVector3d&)> VisibilityFunction,
		const PixelType& DefaultValue
	) const;

protected:
	TArray<TSharedPtr<TSpatialPhoto<PixelType>, ESPMode::ThreadSafe>> Photos;
};
typedef TSpatialPhotoSet<FVector4f, float> FSpatialPhotoSet4f;
typedef TSpatialPhotoSet<FVector3f, float> FSpatialPhotoSet3f;
typedef TSpatialPhotoSet<float, float> FSpatialPhotoSet1f;




template<typename PixelType, typename RealType>
PixelType TSpatialPhotoSet<PixelType, RealType>::ComputeSample(
	const FVector3d& Position, 
	const FVector3d& Normal, 
	TFunctionRef<bool(const FVector3d&, const FVector3d&)> VisibilityFunction,
	const PixelType& DefaultValue ) const
{
	double DotTolerance = -0.1;		// dot should be negative for normal pointing towards photo

	PixelType BestSample = DefaultValue;
	double MinDot = 1.0;

	int32 NumPhotos = Num();
	for (int32 pi = 0; pi < NumPhotos; ++pi)
	{
		const TSpatialPhoto<PixelType>& Photo = *Photos[pi];
		check(Photo.Dimensions.IsSquare());

		FVector3d ViewDirection = Photo.Frame.X();
		double ViewDot = ViewDirection.Dot(Normal);
		if (ViewDot > DotTolerance || ViewDot > MinDot)
		{
			continue;
		}

		FFrame3d ViewPlane = Photo.Frame;
		ViewPlane.Origin += Photo.NearPlaneDist * ViewDirection;

		double ViewPlaneWidthWorld = Photo.NearPlaneDist * FMathd::Tan(Photo.HorzFOVDegrees * 0.5 * FMathd::DegToRad);
		double ViewPlaneHeightWorld = ViewPlaneWidthWorld;

		FVector3d RayOrigin = Photo.Frame.Origin;
		FVector3d RayDir = Normalized(Position - RayOrigin);
		FVector3d HitPoint;
		bool bHit = ViewPlane.RayPlaneIntersection(RayOrigin, RayDir, 0, HitPoint);
		if (bHit)
		{
			bool bVisible = VisibilityFunction(Position, HitPoint);
			if ( bVisible )
			{
				double PlaneX = (HitPoint - ViewPlane.Origin).Dot(ViewPlane.Y());
				double PlaneY = (HitPoint - ViewPlane.Origin).Dot(ViewPlane.Z());

				//FVector2d PlanePos = ViewPlane.ToPlaneUV(HitPoint, 0);
				double u = PlaneX / ViewPlaneWidthWorld;
				double v = -(PlaneY / ViewPlaneHeightWorld);
				if (FMathd::Abs(u) < 1 && FMathd::Abs(v) < 1)
				{
					double x = (u/2.0 + 0.5) * (double)Photo.Dimensions.GetWidth();
					double y = (v/2.0 + 0.5) * (double)Photo.Dimensions.GetHeight();
					PixelType Sample = Photo.Image.template BilinearSample<RealType>(FVector2d(x, y), DefaultValue);

					MinDot = ViewDot;
					BestSample = Sample;
				}
			}
		}
	}

	return BestSample;
}


} // end namespace UE::Geometry
} // end namespace UE