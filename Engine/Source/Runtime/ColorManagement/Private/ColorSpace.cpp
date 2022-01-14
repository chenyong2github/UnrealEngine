// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorSpace.h"
#include "Math/VectorRegister.h"

namespace UE { namespace Color {

static FColorSpace WorkingColorSpace = FColorSpace(EColorSpace::sRGB);

const FColorSpace& FColorSpace::GetWorking()
{
	return WorkingColorSpace;
}

void FColorSpace::SetWorking(FColorSpace ColorSpace)
{
	WorkingColorSpace = ColorSpace;
}

static bool IsSRGBChromaticities(const TStaticArray<FCoordinate2d, 4>& Chromaticities, double Tolerance = 1.e-7)
{
	return	Chromaticities[0].Equals(FCoordinate2d(0.64, 0.33), Tolerance) &&
		Chromaticities[1].Equals(FCoordinate2d(0.30, 0.60), Tolerance) &&
		Chromaticities[2].Equals(FCoordinate2d(0.15, 0.06), Tolerance) &&
		Chromaticities[3].Equals(FCoordinate2d(0.3127, 0.3290), Tolerance);
}

FColorSpace::FColorSpace(const FVector2D& InRed, const FVector2D& InGreen, const FVector2D& InBlue, const FVector2D& InWhite)
{
	Chromaticities[0] = FCoordinate2d(InRed);
	Chromaticities[1] = FCoordinate2d(InGreen);
	Chromaticities[2] = FCoordinate2d(InBlue);
	Chromaticities[3] = FCoordinate2d(InWhite);

	bIsSRGB = IsSRGBChromaticities(Chromaticities);

	RgbToXYZ = CalcRgbToXYZ();
	XYZToRgb = RgbToXYZ.Inverse();
}

FColorSpace::FColorSpace(EColorSpace ColorSpaceType)
	: Chromaticities(InPlace, FCoordinate2d(0, 0))
	, bIsSRGB(ColorSpaceType == EColorSpace::sRGB)
{
	const FCoordinate2d D65(0.3127, 0.3290);
	const FCoordinate2d D60(0.32168, 0.33767);

	switch (ColorSpaceType)
	{
	case EColorSpace::None:
		break;
	case EColorSpace::sRGB:
		Chromaticities[0] = FCoordinate2d(0.64, 0.33);
		Chromaticities[1] = FCoordinate2d(0.30, 0.60);
		Chromaticities[2] = FCoordinate2d(0.15, 0.06);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::Rec2020:
		Chromaticities[0] = FCoordinate2d(0.708, 0.292);
		Chromaticities[1] = FCoordinate2d(0.170, 0.797);
		Chromaticities[2] = FCoordinate2d(0.131, 0.046);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::ACESAP0:
		Chromaticities[0] = FCoordinate2d(0.7347, 0.2653);
		Chromaticities[1] = FCoordinate2d(0.0000, 1.0000);
		Chromaticities[2] = FCoordinate2d(0.0001, -0.0770);
		Chromaticities[3] = D60;
		break;
	case EColorSpace::ACESAP1:
		Chromaticities[0] = FCoordinate2d(0.713, 0.293);
		Chromaticities[1] = FCoordinate2d(0.165, 0.830);
		Chromaticities[2] = FCoordinate2d(0.128, 0.044);
		Chromaticities[3] = D60;
		break;
	case EColorSpace::P3DCI:
		Chromaticities[0] = FCoordinate2d(0.680, 0.320);
		Chromaticities[1] = FCoordinate2d(0.265, 0.690);
		Chromaticities[2] = FCoordinate2d(0.150, 0.060);
		Chromaticities[3] = FCoordinate2d(0.314, 0.351);
		break;
	case EColorSpace::P3D65:
		Chromaticities[0] = FCoordinate2d(0.680, 0.320);
		Chromaticities[1] = FCoordinate2d(0.265, 0.690);
		Chromaticities[2] = FCoordinate2d(0.150, 0.060);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::REDWideGamut:
		Chromaticities[0] = FCoordinate2d(0.780308, 0.304253);
		Chromaticities[1] = FCoordinate2d(0.121595, 1.493994);
		Chromaticities[2] = FCoordinate2d(0.095612, -0.084589);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::SonySGamut3:
		Chromaticities[0] = FCoordinate2d(0.730, 0.280);
		Chromaticities[1] = FCoordinate2d(0.140, 0.855);
		Chromaticities[2] = FCoordinate2d(0.100, -0.050);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::SonySGamut3Cine:
		Chromaticities[0] = FCoordinate2d(0.766, 0.275);
		Chromaticities[1] = FCoordinate2d(0.225, 0.800);
		Chromaticities[2] = FCoordinate2d(0.089, -0.087);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::AlexaWideGamut:
		Chromaticities[0] = FCoordinate2d(0.684, 0.313);
		Chromaticities[1] = FCoordinate2d(0.221, 0.848);
		Chromaticities[2] = FCoordinate2d(0.0861, -0.1020);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::CanonCinemaGamut:
		Chromaticities[0] = FCoordinate2d(0.740, 0.270);
		Chromaticities[1] = FCoordinate2d(0.170, 1.140);
		Chromaticities[2] = FCoordinate2d(0.080, -0.100);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::GoProProtuneNative:
		Chromaticities[0] = FCoordinate2d(0.698448, 0.193026);
		Chromaticities[1] = FCoordinate2d(0.329555, 1.024597);
		Chromaticities[2] = FCoordinate2d(0.108443, -0.034679);
		Chromaticities[3] = D65;
		break;
	case EColorSpace::PanasonicVGamut:
		Chromaticities[0] = FCoordinate2d(0.730, 0.280);
		Chromaticities[1] = FCoordinate2d(0.165, 0.840);
		Chromaticities[2] = FCoordinate2d(0.100, -0.030);
		Chromaticities[3] = D65;
		break;
	default:
		check(false);
		break;
	}

	RgbToXYZ = CalcRgbToXYZ();
	XYZToRgb = RgbToXYZ.Inverse();
}

FMatrix44d FColorSpace::CalcRgbToXYZ() const
{
	FMatrix44d Mat = FMatrix44d(
		FVector3d(Chromaticities[0].x, Chromaticities[0].y, 1.0 - Chromaticities[0].x - Chromaticities[0].y),
		FVector3d(Chromaticities[1].x, Chromaticities[1].y, 1.0 - Chromaticities[1].x - Chromaticities[1].y),
		FVector3d(Chromaticities[2].x, Chromaticities[2].y, 1.0 - Chromaticities[2].x - Chromaticities[2].y),
		FVector3d{0,0,0}
	);

	FMatrix44d Inverse = Mat.Inverse();
	const FCoordinate2d& White = Chromaticities[3];
	FVector3d WhiteXYZ = FVector3d(White[0] / White[1], 1.0, (1.0f - White[0] - White[1]) / White[1]);
	FVector3d Scale = Inverse.TransformVector(WhiteXYZ);

	for (unsigned i = 0; i < 3; i++)
	{
		Mat.M[0][i] *= Scale[0];
		Mat.M[1][i] *= Scale[1];
		Mat.M[2][i] *= Scale[2];
	}
	return Mat;
}

bool FColorSpace::Equals(const FColorSpace& CS, double Tolerance) const
{
	return	Chromaticities[0].Equals(CS.Chromaticities[0], Tolerance) &&
		Chromaticities[1].Equals(CS.Chromaticities[1], Tolerance) &&
		Chromaticities[2].Equals(CS.Chromaticities[2], Tolerance) &&
		Chromaticities[3].Equals(CS.Chromaticities[3], Tolerance);
}

bool FColorSpace::IsSRGB() const
{
	return bIsSRGB;
}

FMatrix44d FColorSpaceTransform::CalcChromaticAdaptionMatrix(FVector3d SourceXYZ, FVector3d TargetXYZ, EChromaticAdaptationMethod Method)
{
	FMatrix44d XyzToRgb;

	if (Method == EChromaticAdaptationMethod::CAT02)
	{
		XyzToRgb = FMatrix44d(
			FPlane4d(0.7328,  0.4296, -0.1624,  0.),
			FPlane4d(-0.7036,  1.6975,  0.0061,  0.),
			FPlane4d(0.0030,  0.0136,  0.9834,  0.),
			FPlane4d(0., 0., 0., 1.)
		).GetTransposed();
	}
	else if (Method == EChromaticAdaptationMethod::Bradford)
	{
		XyzToRgb = FMatrix44d(
			FPlane4d(0.8951,  0.2664, -0.1614,  0.),
			FPlane4d(-0.7502,  1.7135,  0.0367,  0.),
			FPlane4d(0.0389, -0.0685,  1.0296,  0.),
			FPlane4d(0., 0., 0., 1.)
		).GetTransposed();
	}
	else
	{
		return FMatrix44d::Identity;
	}

	FVector4d SourceRGB = XyzToRgb.TransformVector(SourceXYZ);
	FVector4d TargetRGB = XyzToRgb.TransformVector(TargetXYZ);
	
	FVector4d Scale = FVector4d(
		TargetRGB.X / SourceRGB.X,
		TargetRGB.Y / SourceRGB.Y,
		TargetRGB.Z / SourceRGB.Z,
		1.0);

	FMatrix44d ScaleMat = FMatrix44d::Identity;
	ScaleMat.M[0][0] = Scale.X;
	ScaleMat.M[1][1] = Scale.Y;
	ScaleMat.M[2][2] = Scale.Z;
	ScaleMat.M[3][3] = Scale.W;

	FMatrix44d RgbToXyz = XyzToRgb.Inverse();

	return XyzToRgb * ScaleMat * RgbToXyz;
}

static FMatrix44d CalcColorSpaceTransformMatrix(const FColorSpace& Src, const FColorSpace& Dst, EChromaticAdaptationMethod Method)
{
	if (Method == UE::Color::EChromaticAdaptationMethod::None)
	{
		return Src.GetRgbToXYZ() * Dst.GetXYZToRgb();
	}

	const FVector3d SrcWhite = Src.GetRgbToXYZ().TransformVector(FVector3d::One());
	const FVector3d DstWhite = Dst.GetRgbToXYZ().TransformVector(FVector3d::One());

	if (SrcWhite.Equals(DstWhite, 1.e-7))
	{
		return Src.GetRgbToXYZ() * Dst.GetXYZToRgb();
	}

	FMatrix44d ChromaticAdaptationMat = FColorSpaceTransform::CalcChromaticAdaptionMatrix(SrcWhite, DstWhite, Method);
	return Src.GetRgbToXYZ() * ChromaticAdaptationMat * Dst.GetXYZToRgb();
}

FColorSpaceTransform::FColorSpaceTransform(const FColorSpace& Src, const FColorSpace& Dst, EChromaticAdaptationMethod Method)
	: FMatrix44d(CalcColorSpaceTransformMatrix(Src,Dst,Method))
{

}

FColorSpaceTransform::FColorSpaceTransform(FMatrix44d Matrix)
	: FMatrix44d(MoveTemp(Matrix))
{ }

FLinearColor FColorSpaceTransform::Apply(const FLinearColor& Color) const
{
	FLinearColor Result;
	VectorRegister4Float VecP = VectorLoadAligned((const FVector4f*)&Color);
	VectorRegister4Float VecR = VectorTransformVector(VecP, this);
	VectorStoreAligned(VecR, (FVector4f*)&Result);
	return Result;
}


} } // end namespace UE::Color

