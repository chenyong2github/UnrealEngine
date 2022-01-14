// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Containers/StaticArray.h"
#include "ColorManagementDefines.h"

namespace UE { namespace Color {

//! At the time of writing, there is no double-precision 2D vector available hence this custom type.
struct FCoordinate2d
{
	/** Coordinate's x component (lowercase to follow the CIE xy-XYZ convention). */
	double x;

	/** Coordinate's y component (lowercase to follow the CIE xy-XYZ convention). */
	double y;

public:

	/** Constructor */
	FORCEINLINE FCoordinate2d() { }

	/**
	* Constructor
	*
	* @param Inx x coordinate.
	* @param Iny y coordinate.
	*/
	explicit FORCEINLINE FCoordinate2d(double Inx, double Iny)
		: x(Inx)
		, y(Iny)
	{ }

	/**
	* Constructor
	*
	* @param Coordinates xy coordinates.
	*/
	explicit FORCEINLINE FCoordinate2d(FVector2D Coordinates)
		: x(Coordinates.X)
		, y(Coordinates.Y)
	{ }

	/**
	 * Compares these coordinates against another pair for equality.
	 *
	 * @param V The FCoordinate2d to compare against.
	 * @return true if the two coordinates are equal, otherwise false.
	 */
	FORCEINLINE bool operator==(const FCoordinate2d& V) const
	{
		return x == V.x && y == V.x;
	}

	/**
	 * Convert to the CIE xyY colorspace coordinates as an FVector3d.
	 *
	 * @return true if equal
	 */
	FORCEINLINE bool Equals(const FCoordinate2d& V, float Tolerance) const
	{
		return FMath::Abs(x - V.x) <= Tolerance && FMath::Abs(y - V.y) <= Tolerance;
	}

	/**
	 * Gets specific component of the coordinates.
	 *
	 * @param Index the index of coordinate component
	 * @return reference to component.
	 */
	FORCEINLINE double& operator[](int32 Index)
	{
		check(Index >= 0 && Index < 2);
		return (Index == 0) ? x : y;
	}

	/**
	 * Gets specific component of the coordinate.
	 *
	 * @param Index the index of coordinate component
	 * @return copy of component value.
	 */
	FORCEINLINE const double& operator[](int32 Index) const
	{
		check(Index >= 0 && Index < 2);
		return (Index == 0) ? x : y;
	}

	/**
	 * Convert to FVector2D.
	 * 
	 * @return FVector2D
	 */
	FORCEINLINE FVector2D ToVector2D() const
	{
		return FVector2D(x, y);
	}

	/**
	 * Convert to CIE Yxy with a luminance value (default to 1.0).
	 * 
	 * @return FVector3d
	 */
	FORCEINLINE FVector3d ToYxy(double LuminanceY=1.0) const
	{
		return FVector3d(LuminanceY, x, y);
	}

	/**
	 * Convert to CIE XYZ tristimulus values with a luminance value (default to 1.0).
	 * 
	 * @return FVector3d
	 */
	FORCEINLINE FVector3d ToXYZ(double LuminanceY = 1.0) const
	{
		const FVector3d Yxy = ToYxy(LuminanceY);
		return FVector3d(
			Yxy[1] * Yxy[0] / FMath::Max(Yxy[2], 1e-10),
			Yxy[0],
			(1.0 - Yxy[1] - Yxy[2]) * Yxy[0] / FMath::Max(Yxy[2], 1e-10)
		);
	}

	/**
	 * Serialize a coordinate.
	 *
	 * @param Ar Serialization archive.
	 * @param C Coordinate2d being serialized.
	 * @return Reference to Archive after serialization.
	 */
	friend FArchive& operator<<(FArchive& Ar, FCoordinate2d& C)
	{
		return Ar << C.x << C.y;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
};


/** Color space definition as 4 chromaticity coordinates, in double precision internally. */
class COLORMANAGEMENT_API FColorSpace
{
public:
	/**
	 * Get the global engine working color space (as a singleton).
	 * 
	 * @return FColorSpace working color space
	 */
	static const FColorSpace& GetWorking();

	/**
	 * Set the global engine working color space (as a singleton).
	 * 
	 * @param ColorSpace working color space
	 */
	static void SetWorking(FColorSpace ColorSpace);


	/** Constructor */
	FColorSpace() {}

	/**
	* Constructor
	*
	* @param InRed Chromaticity 2D coordinates for the red color.
	* @param InGreen Chromaticity 2D coordinates for the green color.
	* @param InBlue Chromaticity 2D coordinates for the blue color.
	* @param InWhite Chromaticity 2D coordinates for the white point.
	*/
	explicit FColorSpace(const FVector2D& InRed, const FVector2D& InGreen, const FVector2D& InBlue, const FVector2D& InWhite);

	/**
	* Constructor
	*
	* @param ColorSpaceType Color space type.
	*/
	explicit FColorSpace(UE::Color::EColorSpace ColorSpaceType);

	FColorSpace(FColorSpace&&) = default;
	FColorSpace(const FColorSpace&) = default;
	FColorSpace& operator=(FColorSpace&&) = default;
	FColorSpace& operator=(const FColorSpace&) = default;

	/**
	* Getter for the color space chromaticity coordinates as FVector2D.
	* 
	* @param OutRed FVector2D for the red color chromaticity coordinate.
	* @param OutGreen FVector2D for the green color chromaticity coordinate.
	* @param OutBlue FVector2D for the blue color chromaticity coordinate.
	* @param OutWhite FVector2D for the white color chromaticity coordinate.
	*/
	FORCEINLINE void GetChromaticities(FVector2D& OutRed, FVector2D& OutGreen, FVector2D& OutBlue, FVector2D& OutWhite) const
	{
		OutRed		= Chromaticities[0].ToVector2D();
		OutGreen	= Chromaticities[1].ToVector2D();
		OutBlue		= Chromaticities[2].ToVector2D();
		OutWhite	= Chromaticities[3].ToVector2D();
	}

	/**
	* Gets the color space's red chromaticity coordinates.
	*
	* @return FVector2D xy coordinates.
	*/
	FORCEINLINE FVector2D GetRedChromaticity() const
	{
		return Chromaticities[0].ToVector2D();
	}

	/**
	* Gets the color space's red chromaticity coordinates.
	*
	* @return FCoordinate2d xy coordinates.
	*/
	FORCEINLINE FCoordinate2d GetRedChromaticityCoordinate() const
	{
		return Chromaticities[0];
	}

	/**
	* Gets the color space's green chromaticity coordinates.
	*
	* @return FVector2D xy coordinates.
	*/
	FORCEINLINE FVector2D GetGreenChromaticity() const
	{
		return Chromaticities[1].ToVector2D();
	}

	/**
	* Gets the color space's green chromaticity coordinates.
	*
	* @return FCoordinate2d xy coordinates.
	*/
	FORCEINLINE FCoordinate2d GetGreenChromaticityCoordinate() const
	{
		return Chromaticities[1];
	}

	/**
	* Gets the color space's blue chromaticity coordinates.
	*
	* @return FVector2D xy coordinates.
	*/
	FORCEINLINE FVector2D GetBlueChromaticity() const
	{
		return Chromaticities[2].ToVector2D();
	}

	/**
	* Gets the color space's blue chromaticity coordinates.
	*
	* @return FCoordinate2d xy coordinates.
	*/
	FORCEINLINE FCoordinate2d GetBlueChromaticityCoordinate() const
	{
		return Chromaticities[2];
	}

	/**
	* Gets the color space's white point chromaticity coordinates.
	*
	* @return FVector2D xy coordinates.
	*/
	FORCEINLINE FVector2D GetWhiteChromaticity() const
	{
		return Chromaticities[3].ToVector2D();
	}

	/**
	* Gets the color space's white point chromaticity coordinates.
	*
	* @return FCoordinate2d xy coordinates.
	*/
	FORCEINLINE FCoordinate2d GetWhiteChromaticityCoordinate() const
	{
		return Chromaticities[3];
	}

	/**
	* Gets the RGB-to-XYZ conversion matrix.
	*
	* @return FMatrix conversion matrix.
	*/
	FORCEINLINE const FMatrix44d& GetRgbToXYZ() const
	{
		return RgbToXYZ;
	}

	/**
	* Gets the XYZ-to-RGB conversion matrix.
	*
	* @return FMatrix conversion matrix.
	*/
	FORCEINLINE const FMatrix44d& GetXYZToRgb() const
	{
		return XYZToRgb;
	}

	/**
	 * Check against another vector for equality.
	 *
	 * @param V The vector to check against.
	 * @return true if the vectors are equal, false otherwise.
	 */
	FORCEINLINE bool operator==(const FColorSpace& CS) const
	{
		return Chromaticities == CS.Chromaticities;
	}

	/**
	 * Check against another colorspace for inequality.
	 *
	 * @param V The vector to check against.
	 * @return true if the vectors are not equal, false otherwise.
	 */
	FORCEINLINE bool operator!=(const FColorSpace& CS) const
	{
		return Chromaticities != CS.Chromaticities;
	}

	/**
	 * Check against another colorspace for equality, within specified error limits.
	 *
	 * @param V The vector to check against.
	 * @param Tolerance Error tolerance.
	 * @return true if the vectors are equal within tolerance limits, false otherwise.
	 */
	bool Equals(const FColorSpace& CS, double Tolerance = 1.e-7) const;

	/**
	 * Convenience function to verify if the color space matches the engine's default sRGB chromaticities.
	 *
	 * @return true if sRGB.
	 */
	bool IsSRGB() const;

private:

	FMatrix44d CalcRgbToXYZ() const;

	/** Red, green, blue, white chromaticities, in order. */
	TStaticArray<FCoordinate2d, 4> Chromaticities;

	FMatrix44d RgbToXYZ;
	FMatrix44d XYZToRgb;

	bool bIsSRGB;

public:

	/**
	 * Serializer.
	 *
	 * @param Ar The Serialization Archive.
	 * @param CS The Color Space being serialized.
	 * @return Reference to the Archive after serialization.
	 */
	friend FArchive& operator<<(FArchive& Ar, FColorSpace& CS)
	{
		return Ar << CS.Chromaticities;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
};


struct FColorSpaceTransform : FMatrix44d
{
	/**
	* Constructor: create a color space transformation matrix from a source to a target color space using the RGB-XYZ-RGB conversions.
	*
	* @param Source Source color space.
	* @param Target Target color space.
	* @param Method Chromatic adapation method.
	*/
	COLORMANAGEMENT_API explicit FColorSpaceTransform(const FColorSpace& Src, const FColorSpace& Dst, EChromaticAdaptationMethod Method);

	/**
	* Constructor: create a color space transformation matrix from a raw matrix.
	*
	* @param Matrix Color space transformation matrix.
	*/
	COLORMANAGEMENT_API explicit FColorSpaceTransform(FMatrix44d Matrix);

	/**
	* Apply color space transform to FLinearColor.
	* 
	* @param Color Color to transform.
	*/
	COLORMANAGEMENT_API FLinearColor Apply(const FLinearColor& Color) const;

	/**
	* Calculate the chromatic adaptation matrix using the specific method.
	*
	* @param SourceXYZ Source color in XYZ space.
	* @param TargetXYZ Target color in XYZ space.
	* @param Method Adaptation method (None, Bradford, CAT02).
	*/
	COLORMANAGEMENT_API static FMatrix44d CalcChromaticAdaptionMatrix(FVector3d SourceXYZ, FVector3d TargetXYZ, EChromaticAdaptationMethod Method);
};

} }  // end namespace UE::Color
