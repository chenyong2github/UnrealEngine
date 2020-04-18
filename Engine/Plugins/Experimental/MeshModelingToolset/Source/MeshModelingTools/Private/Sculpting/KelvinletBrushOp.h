// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MatrixTypes.h"
#include "DynamicMesh3.h"
#include "Deformers/Kelvinlets.h"


enum class EKelvinletBrushMode
{
	ScaleKelvinlet,
	PinchKelvinlet,
	TwistKelvinlet,
	PullKelvinlet,
	LaplacianPullKelvinlet,
	BiLaplacianPullKelvinlet,
	BiLaplacianTwistPullKelvinlet,
	LaplacianTwistPullKelvinlet,
	SharpPullKelvinlet
};

class FKelvinletBrushOp
{
public:


	struct FKelvinletBrushOpProperties
	{
		FKelvinletBrushOpProperties(const EKelvinletBrushMode& BrushMode, const UKelvinBrushProperties& Properties, const UBrushBaseProperties& Brush) :
			Mode(BrushMode),
			Direction(1.0, 0.0, 0.0)
		{
			Speed = 0.;
			FallOff = Brush.BrushFalloffAmount;
			Mu = FMath::Max(Properties.Stiffness, 0.f);
			Nu = FMath::Clamp(0.5f * (1.f - 2.f * Properties.Incompressiblity), 0.f, 0.5f);
			Size = FMath::Max(Brush.BrushRadius * Properties.FallOffDistance, 0.f);
			NumSteps = Properties.BrushSteps;
		}


		EKelvinletBrushMode Mode;
		FVector Direction;

		double Speed;   // Optionally used
		double FallOff; // Optionally used
		double Mu; // Shear Modulus
		double Nu; // Poisson ratio

		// regularization parameter
		double Size;

		int NumSteps;

	};

	FKelvinletBrushOp(const FDynamicMesh3& DynamicMesh) :
		Mesh(&DynamicMesh)
	{}

	void ExtractTransform(const FMatrix& WorldToBrush)
	{
		// Extract the parts of the transform and account for the vector * matrix  format of FMatrix by transposing.

		// Transpose of the 3x3 part
		WorldToBrushMat.Row0[0] = WorldToBrush.M[0][0];
		WorldToBrushMat.Row0[1] = WorldToBrush.M[0][1];
		WorldToBrushMat.Row0[2] = WorldToBrush.M[0][2];

		WorldToBrushMat.Row1[0] = WorldToBrush.M[1][0];
		WorldToBrushMat.Row1[1] = WorldToBrush.M[1][1];
		WorldToBrushMat.Row1[2] = WorldToBrush.M[1][2];

		WorldToBrushMat.Row2[0] = WorldToBrush.M[2][0];
		WorldToBrushMat.Row2[1] = WorldToBrush.M[2][1];
		WorldToBrushMat.Row2[2] = WorldToBrush.M[2][2];

		Translation[0] = WorldToBrush.M[3][0];
		Translation[1] = WorldToBrush.M[3][1];
		Translation[2] = WorldToBrush.M[3][2];

		// The matrix should be unitary (det +/- 1) but want this to work with 
		// more general input if needed so just make sure we can invert the matrix
		check(FMath::Abs(WorldToBrush.Determinant()) > 1.e-4);

		BrushToWorldMat = WorldToBrushMat.Inverse();

	};

	~FKelvinletBrushOp() {}

public:
	const FDynamicMesh3* Mesh;


	double TimeStep = 1.0;
	double NumSteps = 0.0;

	// To be applied as WorlToBrushMat * v + Trans
	// Note: could use FTransform3d in TransformTypes.h
	FMatrix3d WorldToBrushMat;
	FMatrix3d BrushToWorldMat;
	FVector3d Translation;

public:



	void ApplyBrush(const FKelvinletBrushOpProperties& Properties, const FMatrix& WorldToBrush, const TArray<int>& VertRIO, TArray<FVector3d>& ROIPositionBuffer)
	{

		ExtractTransform(WorldToBrush);

		NumSteps = Properties.NumSteps;

		switch (Properties.Mode)
		{

		case EKelvinletBrushMode::ScaleKelvinlet:
		{
			double Scale = Properties.Direction.X;
			FScaleKelvinlet ScaleKelvinlet(Scale, Properties.Size, Properties.Mu, Properties.Nu);
			ApplyKelvinlet(ScaleKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::PullKelvinlet:
		{
			const FVector& Dir = Properties.Direction;
			FVector3d Force(Dir.X, Dir.Y, Dir.Z);

			FLaplacianPullKelvinlet LaplacianPullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);
			FBiLaplacianPullKelvinlet BiLaplacianPullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);

			const double Alpha = Properties.FallOff;
			// Lerp between a broad and a narrow kelvinlet based on the fall-off 
			FBlendPullKelvinlet BlendPullKelvinlet(BiLaplacianPullKelvinlet, LaplacianPullKelvinlet, Alpha);
			ApplyKelvinlet(BlendPullKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::SharpPullKelvinlet:
		{
			const FVector& Dir = Properties.Direction;
			FVector3d Force(Dir.X, Dir.Y, Dir.Z);

			FSharpLaplacianPullKelvinlet SharpLaplacianPullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);
			FSharpBiLaplacianPullKelvinlet SharpBiLaplacianPullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);

			const double Alpha = Properties.FallOff;
			// Lerp between a broad and a narrow Kelvinlet based on the fall-off 
			FBlendPullSharpKelvinlet BlendPullSharpKelvinlet(SharpBiLaplacianPullKelvinlet, SharpLaplacianPullKelvinlet, Alpha);
			ApplyKelvinlet(BlendPullSharpKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::LaplacianPullKelvinlet:
		{
			const FVector& Dir = Properties.Direction;
			FVector3d Force(Dir.X, Dir.Y, Dir.Z);
			FLaplacianPullKelvinlet PullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);
			ApplyKelvinlet(PullKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::BiLaplacianPullKelvinlet:
		{
			const FVector& Dir = Properties.Direction;
			FVector3d Force(Dir.X, Dir.Y, Dir.Z);
			FBiLaplacianPullKelvinlet PullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);
			ApplyKelvinlet(PullKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::TwistKelvinlet:
		{
			const FVector3d& TwistAxis = Properties.Direction;
			FTwistKelvinlet TwistKelvinlet(TwistAxis, Properties.Size, Properties.Mu, Properties.Nu);
			ApplyKelvinlet(TwistKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::PinchKelvinlet:
		{
			const FVector& Dir = Properties.Direction;

			FMatrix3d ForceMatrix = CrossProductMatrix(FVector3d(Dir.X, Dir.Y, Dir.Z));
			// make symmetric
			ForceMatrix.Row0[1] = -ForceMatrix.Row0[1];
			ForceMatrix.Row0[2] = -ForceMatrix.Row0[2];
			ForceMatrix.Row1[2] = -ForceMatrix.Row1[2];
			FPinchKelvinlet PinchKelvinlet(ForceMatrix, Properties.Size, Properties.Mu, Properties.Nu);
			ApplyKelvinlet(PinchKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;
		}
		case EKelvinletBrushMode::LaplacianTwistPullKelvinlet:
		{
			FVector3d TwistAxis = Properties.Direction;
			TwistAxis.Normalize();
			TwistAxis *= Properties.Speed;
			FTwistKelvinlet TwistKelvinlet(TwistAxis, Properties.Size, Properties.Mu, Properties.Nu);

			const FVector& Dir = Properties.Direction;
			FVector3d Force(Dir.X, Dir.Y, Dir.Z);
			FLaplacianPullKelvinlet PullKelvinlet(Force, Properties.Size, Properties.Mu, Properties.Nu);

			FLaplacianTwistPullKelvinlet TwistPullKelvinlet(TwistKelvinlet, PullKelvinlet, 0.5);
			ApplyKelvinlet(TwistPullKelvinlet, VertRIO, ROIPositionBuffer, TimeStep, NumSteps);
			break;

		}
		default:
			check(0);
		}
	}


	// NB: this just moves the verts, but doesn't update the normal.  The kelvinlets will have to be extended if we want
	// to do the Jacobian Transpose operation on the normals - but for now, we should just rebuild the normals after the brush
	template <typename KelvinletType>
	void DisplaceKelvinlet(const KelvinletType& Kelvinlet, const TArray<int>& VertexROI, TArray<FVector3d>& ROIPositionBuffer)
	{
		int NumV = VertexROI.Num();
		ROIPositionBuffer.SetNum(NumV, false);

		const bool bForceSingleThread = false;

		ParallelFor(NumV, [&Kelvinlet, &VertexROI, &ROIPositionBuffer, this](int k)

		{
			int VertIdx = VertexROI[k];
			FVector3d Pos = Mesh->GetVertex(VertIdx);
			Pos = XForm(Pos);

			Pos = Kelvinlet.Evaluate(Pos) + Pos;

			// Update the position in the ROI Array
			ROIPositionBuffer[k] = InvXForm(Pos);
		}
		, bForceSingleThread);

	}

	template <typename KelvinletType>
	void IntegrateKelvinlet(const KelvinletType& Kelvinlet, const TArray<int>& VertexROI, TArray<FVector3d>& ROIPositionBuffer, const double Dt, const int Steps)
	{
		int NumV = VertexROI.Num();
		ROIPositionBuffer.SetNum(NumV, false);

		const bool bForceSingleThread = false;

		ParallelFor(NumV, [&Kelvinlet, &VertexROI, &ROIPositionBuffer, Dt, Steps, this](int k)
		{
			int VertIdx = VertexROI[k];
			FVector3d Pos = XForm(Mesh->GetVertex(VertIdx));

			double TimeScale = 1. / (Steps);
			// move with several time steps
			for (int i = 0; i < Steps; ++i)
			{
				// the position after deformation
				Pos = Kelvinlet.IntegrateRK3(Pos, Dt * TimeScale);
			}
			// Update the position in the ROI Array
			ROIPositionBuffer[k] = InvXForm(Pos);
		}, bForceSingleThread);


	}

	template <typename KelvinletType>
	void ApplyKelvinlet(const KelvinletType& Kelvinlet, const TArray<int>& VertexROI, TArray<FVector3d>& ROIPositionBuffer, const double Dt, const int NumIntegrationSteps)
	{

		if (NumIntegrationSteps == 0)
		{
			DisplaceKelvinlet(Kelvinlet, VertexROI, ROIPositionBuffer);
		}
		else
		{
			IntegrateKelvinlet(Kelvinlet, VertexROI, ROIPositionBuffer, Dt, NumIntegrationSteps);
		}
	}

private:

	// apply the transform.
	FVector3d XForm(const FVector3d& Pos) const
	{
		return WorldToBrushMat * Pos + Translation;
	}

	// apply the inverse transform.
	FVector3d InvXForm(const FVector3d& Pos) const
	{
		return BrushToWorldMat * (Pos - Translation);
	}

};