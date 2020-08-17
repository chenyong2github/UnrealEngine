// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/CollisionGeometryVisualization.h"
#include "Generators/LineSegmentGenerators.h"

void UE::PhysicsTools::InitializePreviewGeometryLines(const FPhysicsDataCollection& PhysicsData, UPreviewGeometry* PreviewGeom,
	const FColor& LineColor, float LineThickness, float DepthBias, int32 CircleStepResolution)
{
	int32 CircleSteps = FMath::Max(4, CircleStepResolution);
	FColor SphereColor = LineColor;
	FColor BoxColor = LineColor;
	FColor ConvexColor = LineColor;
	FColor CapsuleColor = LineColor;

	const FKAggregateGeom& AggGeom = PhysicsData.AggGeom;

	// spheres are draw as 3 orthogonal circles
	PreviewGeom->CreateOrUpdateLineSet(TEXT("Spheres"), AggGeom.SphereElems.Num(), [&](int32 Index, TArray<FRenderableLine>& LinesOut)
	{
		const FKSphereElem& Sphere = AggGeom.SphereElems[Index];
		FTransform ElemTransform = Sphere.GetTransform();
		ElemTransform.ScaleTranslation(PhysicsData.ExternalScale3D);
		FTransform3f ElemTransformf(ElemTransform);
		float Radius = PhysicsData.ExternalScale3D.GetAbsMin() * Sphere.Radius;
		UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, FVector3f::Zero(), FVector3f::UnitX(), FVector3f::UnitY(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, SphereColor, LineThickness, DepthBias)); });
		UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, FVector3f::Zero(), FVector3f::UnitX(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, SphereColor, LineThickness, DepthBias)); });
		UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, FVector3f::Zero(), FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, SphereColor, LineThickness, DepthBias)); });
	});


	// boxes are drawn as boxes
	PreviewGeom->CreateOrUpdateLineSet(TEXT("Boxes"), AggGeom.BoxElems.Num(), [&](int32 Index, TArray<FRenderableLine>& LinesOut)
	{
		const FKBoxElem& Box = AggGeom.BoxElems[Index];
		FTransform ElemTransform = Box.GetTransform();
		ElemTransform.ScaleTranslation(PhysicsData.ExternalScale3D);
		FTransform3f ElemTransformf(ElemTransform);
		FVector3f HalfDimensions(
			PhysicsData.ExternalScale3D.X * Box.X * 0.5f,
			PhysicsData.ExternalScale3D.Y * Box.Y * 0.5f,
			PhysicsData.ExternalScale3D.Z * Box.Z * 0.5f);
		UE::Geometry::GenerateBoxSegments<float>(HalfDimensions, FVector3f::Zero(), FVector3f::UnitX(), FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, BoxColor, LineThickness, DepthBias)); });
	});


	// capsules are draw as two hemispheres (with 3 intersecting arcs/circles) and connecting lines
	PreviewGeom->CreateOrUpdateLineSet(TEXT("Capsules"), AggGeom.SphylElems.Num(), [&](int32 Index, TArray<FRenderableLine>& LinesOut)
	{
		const FKSphylElem& Capsule = AggGeom.SphylElems[Index];
		FTransform ElemTransform = Capsule.GetTransform();
		ElemTransform.ScaleTranslation(PhysicsData.ExternalScale3D);
		FTransform3f ElemTransformf(ElemTransform);
		const float HalfLength = Capsule.GetScaledCylinderLength(PhysicsData.ExternalScale3D) * .5f;
		const float Radius = Capsule.GetScaledRadius(PhysicsData.ExternalScale3D);
		FVector3f Top(0, 0, HalfLength), Bottom(0, 0, -HalfLength);

		// top and bottom circles
		UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, Top, FVector3f::UnitX(), FVector3f::UnitY(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
		UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, Bottom, FVector3f::UnitX(), FVector3f::UnitY(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

		// top dome
		UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, PI, Top, FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
		UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, PI, Top, FVector3f::UnitX(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

		// bottom dome
		UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, -PI, Bottom, FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
		UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, -PI, Bottom, FVector3f::UnitX(), FVector3f::UnitZ(), ElemTransformf,
			[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

		// connecting lines
		for (int k = 0; k < 2; ++k)
		{
			FVector DX = (k < 1) ? FVector(-Radius, 0, 0) : FVector(Radius, 0, 0);
			LinesOut.Add(FRenderableLine(
				ElemTransform.TransformPosition((FVector)Top + DX), 
				ElemTransform.TransformPosition((FVector)Bottom + DX), CapsuleColor, LineThickness, DepthBias));
			FVector DY = (k < 1) ? FVector(0, -Radius, 0) : FVector(0, Radius, 0);
			LinesOut.Add(FRenderableLine(
				ElemTransform.TransformPosition((FVector)Top + DY), 
				ElemTransform.TransformPosition((FVector)Bottom + DY), CapsuleColor, LineThickness, DepthBias));
		}
	});



	// convexes are drawn as mesh edges
	PreviewGeom->CreateOrUpdateLineSet(TEXT("Convexes"), AggGeom.ConvexElems.Num(), [&](int32 Index, TArray<FRenderableLine>& LinesOut)
	{
		const FKConvexElem& Convex = AggGeom.ConvexElems[Index];
		FTransform ElemTransform = Convex.GetTransform();
		ElemTransform.ScaleTranslation(PhysicsData.ExternalScale3D);
		ElemTransform.SetScale3D(PhysicsData.ExternalScale3D);
		int32 NumTriangles = Convex.IndexData.Num() / 3;
		for (int32 k = 0; k < NumTriangles; ++k)
		{
			FVector A = ElemTransform.TransformPosition(Convex.VertexData[Convex.IndexData[3 * k]]);
			FVector B = ElemTransform.TransformPosition(Convex.VertexData[Convex.IndexData[3 * k + 1]]);
			FVector C = ElemTransform.TransformPosition(Convex.VertexData[Convex.IndexData[3 * k + 2]]);
			LinesOut.Add(FRenderableLine(A, B, ConvexColor, LineThickness, DepthBias));
			LinesOut.Add(FRenderableLine(B, C, ConvexColor, LineThickness, DepthBias));
			LinesOut.Add(FRenderableLine(C, A, ConvexColor, LineThickness, DepthBias));
		}
	});


	// Unclear whether we actually use these in the Engine, for UBodySetup? Does not appear to be supported by UxX import system,
	// and online documentation suggests they may only be supported for cloth?
	ensure(AggGeom.TaperedCapsuleElems.Num() == 0);
}