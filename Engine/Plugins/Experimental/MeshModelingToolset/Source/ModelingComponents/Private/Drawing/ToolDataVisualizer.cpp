// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Drawing/ToolDataVisualizer.h"
#include "SceneManagement.h" 
#include "ToolContextInterfaces.h"
#include "SceneManagement.h"   // DrawCircle

#include "Util/IndexUtil.h"

FToolDataVisualizer::FToolDataVisualizer()
{
	LineColor = FLinearColor(0.95f, 0.05f, 0.05f);
	PointColor = FLinearColor(0.95f, 0.05f, 0.05f);
	PopAllTransforms();
}


void FToolDataVisualizer::BeginFrame(IToolsContextRenderAPI* RenderAPI, const FViewCameraState& CameraStateIn)
{
	checkf(CurrentPDI == nullptr, TEXT("FToolDataVisualizer::BeginFrame: matching EndFrame was not called last frame!"));
	CurrentPDI = RenderAPI->GetPrimitiveDrawInterface();
	CameraState = CameraStateIn;
	bHaveCameraState = true;
}

void FToolDataVisualizer::BeginFrame(IToolsContextRenderAPI* RenderAPI)
{
	checkf(CurrentPDI == nullptr, TEXT("FToolDataVisualizer::BeginFrame: matching EndFrame was not called last frame!"));
	CurrentPDI = RenderAPI->GetPrimitiveDrawInterface();
	bHaveCameraState = false;
}

void FToolDataVisualizer::EndFrame()
{
	// not safe to hold PDI
	CurrentPDI = nullptr;
}


void FToolDataVisualizer::SetTransform(const FTransform& Transform)
{
	TransformStack.Reset();
	TransformStack.Add(Transform);
	TotalTransform = Transform;
}


void FToolDataVisualizer::PushTransform(const FTransform& Transform)
{
	TransformStack.Add(Transform);
	TotalTransform *= Transform;
}

void FToolDataVisualizer::PopTransform()
{
	TransformStack.Pop(false);
	TotalTransform = FTransform::Identity;
	for (const FTransform& Transform : TransformStack)
	{
		TotalTransform *= Transform;
	}
}

void FToolDataVisualizer::PopAllTransforms()
{
	TransformStack.Reset();
	TotalTransform = FTransform::Identity;
}




void FToolDataVisualizer::InternalDrawTransformedLine(const FVector& A, const FVector& B, const FLinearColor& ColorIn, float LineThicknessIn, bool bDepthTestedIn)
{
	CurrentPDI->DrawLine(A, B, ColorIn,
		(bDepthTestedIn) ? SDPG_World : SDPG_Foreground,
		LineThicknessIn, 0.0f, true);
}


void FToolDataVisualizer::InternalDrawTransformedPoint(const FVector& Position, const FLinearColor& ColorIn, float PointSizeIn, bool bDepthTestedIn)
{
	CurrentPDI->DrawPoint(Position, ColorIn, PointSizeIn,
		(bDepthTestedIn) ? SDPG_World : SDPG_Foreground);
}


void FToolDataVisualizer::InternalDrawCircle(const FVector& Position, const FVector& Normal, float Radius, int Steps, const FLinearColor& Color, float LineThicknessIn, bool bDepthTestedIn)
{
	FVector3f Tan1, Tan2;
	VectorUtil::MakePerpVectors((FVector3f)TransformN(Normal), Tan1, Tan2);
	Tan1.Normalize(); Tan2.Normalize();

	// this function is from SceneManagement.h
	::DrawCircle(CurrentPDI, TransformP(Position), (FVector)Tan1, (FVector)Tan2, 
		Color, Radius, Steps,
		(bDepthTestedIn) ? SDPG_World : SDPG_Foreground,
		LineThicknessIn, 0.0f, true);
}

void FToolDataVisualizer::InternalDrawWireBox(const FBox& Box, const FLinearColor& ColorIn, float LineThicknessIn, bool bDepthTestedIn)
{
	// corners [ (-x,-y), (x,-y), (x,y), (-x,y) ], -z, then +z
	FVector Corners[8] =
	{
		TransformP(Box.Min),
		TransformP(FVector(Box.Max.X, Box.Min.Y, Box.Min.Z)),
		TransformP(FVector(Box.Max.X, Box.Max.Y, Box.Min.Z)),
		TransformP(FVector(Box.Min.X, Box.Max.Y, Box.Min.Z)),
		TransformP(FVector(Box.Min.X, Box.Min.Y, Box.Max.Z)),
		TransformP(FVector(Box.Max.X, Box.Min.Y, Box.Max.Z)),
		TransformP(Box.Max),
		TransformP(FVector(Box.Min.X, Box.Max.Y, Box.Max.Z))
	};
	for (int FaceIdx = 0; FaceIdx < 6; FaceIdx++)
	{
		for (int Last = 3, Cur = 0; Cur < 4; Last = Cur++)
		{
			InternalDrawTransformedLine(Corners[IndexUtil::BoxFaces[FaceIdx][Last]], Corners[IndexUtil::BoxFaces[FaceIdx][Cur]], 
										ColorIn, LineThicknessIn, bDepthTestedIn);
		}
	}
}

void FToolDataVisualizer::InternalDrawSquare(const FVector& Center, const FVector& SideA, const FVector& SideB, const FLinearColor& Color, float LineThicknessIn, bool bDepthTestedIn)
{
	FVector CC = TransformP(Center);
	FVector SA = TransformV(SideA);
	FVector SB = TransformV(SideB);
	FVector HalfDiag = (SA + SB) * .5f;
	FVector C00 = CC - HalfDiag;
	FVector C11 = CC + HalfDiag;
	FVector C01 = C00 + SB;
	FVector C10 = C00 + SA;
	InternalDrawTransformedLine(C00, C01, Color, LineThicknessIn, bDepthTestedIn);
	InternalDrawTransformedLine(C01, C11, Color, LineThicknessIn, bDepthTestedIn);
	InternalDrawTransformedLine(C10, C11, Color, LineThicknessIn, bDepthTestedIn);
	InternalDrawTransformedLine(C00, C10, Color, LineThicknessIn, bDepthTestedIn);
}

void FToolDataVisualizer::InternalDrawWireCylinder(const FVector& Position, const FVector& Normal, float Radius, float Height, int Steps, const FLinearColor& Color, float LineThicknessIn, bool bDepthTestedIn)
{
	FVector3f Tan1, Tan2;
	VectorUtil::MakePerpVectors((FVector3f)Normal, Tan1, Tan2);
	
	const float	AngleDelta = 2.0f * PI / Steps;
	FVector X(Tan1), Y(Tan2);
	FVector	LastVertex = TransformP(Position + X * Radius);
	FVector LastVertexB = TransformP(Position + X * Radius + Normal * Height);

	for (int32 Step = 0; Step < Steps; Step++)
	{
		float Angle = (Step + 1) * AngleDelta;
		FVector A = Position + (X * FMath::Cos(Angle) + Y * FMath::Sin(Angle)) * Radius;
		FVector B = A + Normal * Height;
		FVector Vertex = TransformP(A);
		FVector VertexB = TransformP(B);
		InternalDrawTransformedLine(LastVertex, Vertex, Color, LineThicknessIn, bDepthTestedIn);
		InternalDrawTransformedLine(Vertex, VertexB, Color, LineThicknessIn, bDepthTestedIn);
		InternalDrawTransformedLine(LastVertexB, VertexB, Color, LineThicknessIn, bDepthTestedIn);
		LastVertex = Vertex;
		LastVertexB = VertexB;
	}

}


void FToolDataVisualizer::InternalDrawViewFacingCircle(const FVector& Position, float Radius, int Steps, const FLinearColor& Color, float LineThicknessIn, bool bDepthTestedIn)
{
	checkf(bHaveCameraState, TEXT("To call this function, you must first call the version of BeginFrame that takes the CameraState"));

	FVector WorldPosition = TransformP(Position);
	FVector WorldNormal = (CameraState.Position - WorldPosition);
	WorldNormal.Normalize();
	FVector3f Tan1, Tan2;
	VectorUtil::MakePerpVectors((FVector3f)WorldNormal, Tan1, Tan2);

	// this function is from SceneManagement.h
	::DrawCircle(CurrentPDI, WorldPosition, (FVector)Tan1, (FVector)Tan2,
		Color, Radius, Steps,
		(bDepthTestedIn) ? SDPG_World : SDPG_Foreground,
		LineThicknessIn, 0.0f, true);
}
