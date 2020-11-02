// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionOneShotManifolds.h"

#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/Transform.h"
#include "Chaos/CollisionResolution.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace Collisions
	{
		// Forward delarations we need from CollisionRestitution.cpp

		FContactPoint BoxBoxContactPoint(const FImplicitBox3& Box1, const FImplicitBox3& Box2, const FRigidTransform3& Box1TM, const FRigidTransform3& Box2TM, const FReal CullDistance, const FReal ShapePadding);
		FContactPoint GenericConvexConvexContactPoint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal CullDistance, const FReal ShapePadding);

		//////////////////////////
		// Box Box
		//////////////////////////

		// This function will clip the input vertices by a reference shape's planes (Specified by ClippingAxis and Distance for an AABB)
		// more vertices may be added to outputVertexBuffer by this function
		// This is the core of the Sutherland-Hodgman algorithm
		uint32 BoxBoxClipVerticesAgainstPlane(const FVec3* InputVertexBuffer, FVec3* outputVertexBuffer, uint32 ClipPointCount, FReal ClippingAxis, FReal Distance)
		{

			auto CalculateIntersect = [=](const FVec3& Point1, const FVec3& Point2) -> FVec3
			{
				// Only needs to be valid if the line connecting Point1 with Point2 actually intersects
				FVec3 Result;

				FReal Denominator = Point2[ClippingAxis] - Point1[ClippingAxis];  // Can be negative
				if (FMath::Abs(Denominator) < SMALL_NUMBER)
				{
					Result = Point1;
				}
				else
				{
					FReal Alpha = (Distance - Point1[ClippingAxis]) / Denominator;
					Result = FMath::Lerp(Point1, Point2, Alpha);
				}
				Result[ClippingAxis] = Distance; // For Robustness
				return Result;
			};

			auto InsideClipFace = [=](const FVec3& Point) -> bool
			{
				// The sign of Distance encodes which plane we are using
				if (Distance >= 0)
				{
					return Point[ClippingAxis] <= Distance;
				}
				return Point[ClippingAxis] >= Distance;
			};

			uint32 NewClipPointCount = 0;
			const uint32 MaxNumberOfPoints = 8;

			for (uint32 ClipPointIndex = 0; ClipPointIndex < ClipPointCount; ClipPointIndex++)
			{
				FVec3 CurrentClipPoint = InputVertexBuffer[ClipPointIndex];
				FVec3 PrevClipPoint = InputVertexBuffer[(ClipPointIndex + ClipPointCount - 1) % ClipPointCount];
				FVec3 InterSect = CalculateIntersect(PrevClipPoint, CurrentClipPoint);

				if (InsideClipFace(CurrentClipPoint))
				{
					if (!InsideClipFace(PrevClipPoint))
					{
						outputVertexBuffer[NewClipPointCount++] = InterSect;
						if (NewClipPointCount >= MaxNumberOfPoints)
						{
							break;
						}
					}
					outputVertexBuffer[NewClipPointCount++] = CurrentClipPoint;
				}
				else if (InsideClipFace(PrevClipPoint))
				{
					outputVertexBuffer[NewClipPointCount++] = InterSect;
				}

				if (NewClipPointCount >= MaxNumberOfPoints)
				{
					break;
				}
			}

			return NewClipPointCount;
		}

		void ConstructBoxBoxOneShotManifold(
			const FImplicitBox3& Box1,
			const FRigidTransform3& Box1Transform, //world
			const FImplicitBox3& Box2,
			const FRigidTransform3& Box2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize)
		{
			const uint32 SpaceDimension = 3;

			// We only build one shot manifolds once
			// All boxes are prescaled
			ensure(Constraint.GetManifoldPoints().Num() == 0);
			ensure(Box1Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(Box2Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			const uint32 MaxContactPointCount = 8;
			uint32 ContactPointCount = 0;

			// Use GJK only once
			const FContactPoint GJKContactPoint = BoxBoxContactPoint(Box1, Box2, Box1Transform, Box2Transform, CullDistance, Constraint.Manifold.RestitutionPadding);

			// ToDo: should we generate no contacts here?
			//if (GJKContactPoint.Phi >= CullDistance)
				//return;

			FRigidTransform3 Box1TransformCenter = Box1Transform;
			Box1TransformCenter.SetTranslation(Box1Transform.TransformPositionNoScale(Box1.GetCenter()));
			FRigidTransform3 Box2TransformCenter = Box2Transform;
			Box2TransformCenter.SetTranslation(Box2Transform.TransformPositionNoScale(Box2.GetCenter()));

			// GJK does not give us any face information yet, so find the best reference face here
			uint32 BestFaceNormalAxisBox1 = 0; // Note: Face normals are axis aligned due to coordinates. This is an element of {0 , 1, 2}
			int32 BestFaceNormalAxisDirectionBox1 = 1; // {-1, 1}
			FReal BestFaceNormalSizeInDirectionBox1 = -1.0f;
			const FVec3 SeparationDirectionLocalBox1 = Box1TransformCenter.InverseTransformVectorNoScale(GJKContactPoint.Normal);   // Todo: Check if we can have Non uniform scale here
			// Box1: Iterating through 2 faces at a time here
			for (uint32 FaceNormalAxis = 0; FaceNormalAxis < SpaceDimension; FaceNormalAxis++)
			{
				const FReal AbsSeparationDirectionDotFaceNormal = FMath::Abs(SeparationDirectionLocalBox1[FaceNormalAxis]);
				if (AbsSeparationDirectionDotFaceNormal > BestFaceNormalSizeInDirectionBox1)
				{
					BestFaceNormalAxisBox1 = FaceNormalAxis;
					BestFaceNormalSizeInDirectionBox1 = AbsSeparationDirectionDotFaceNormal;
					BestFaceNormalAxisDirectionBox1 = SeparationDirectionLocalBox1[FaceNormalAxis] >= 0.0f ? -1 : 1;
				}
			}

			// Now for Box2
			uint32 BestFaceNormalAxisBox2 = 0; // Note: Face normals are axis aligned due to coordinates. This is a n element of {0, 1, 2}
			int32 BestFaceNormalAxisDirectionBox2 = 1; // {-1, 1}
			FReal BestFaceNormalSizeInDirectionBox2 = -1.0f;
			const FVec3 SeparationDirectionLocalBox2 = Box2TransformCenter.InverseTransformVectorNoScale(GJKContactPoint.Normal);   // Todo: Check if we can have Non uniform scale here
			for (uint32 FaceNormalAxis = 0; FaceNormalAxis < SpaceDimension; FaceNormalAxis++)
			{
				FVec3 FaceNormal(FVec3::ZeroVector);
				FaceNormal[FaceNormalAxis] = 1.0f;
				const FReal SeparationDirectionDotFaceNormal = FMath::Abs(SeparationDirectionLocalBox2[FaceNormalAxis]);

				if (SeparationDirectionDotFaceNormal > BestFaceNormalSizeInDirectionBox2)
				{
					BestFaceNormalAxisBox2 = FaceNormalAxis;
					BestFaceNormalSizeInDirectionBox2 = SeparationDirectionDotFaceNormal;
					BestFaceNormalAxisDirectionBox2 = SeparationDirectionLocalBox2[FaceNormalAxis] >= 0.0f ? 1 : -1;  // Note opposite of box1
				}
			}

			const FReal SmallBiasToPreventFeatureFlipping = 0.002; // This improves frame coherence by penalizing box 1 in favour of box 2
			bool ReferenceFaceBox1 = true; // Is the reference face on box1 or box2?
			if (BestFaceNormalSizeInDirectionBox2 + SmallBiasToPreventFeatureFlipping > BestFaceNormalSizeInDirectionBox1)
			{
				ReferenceFaceBox1 = false;
			}

			// Setup pointers to other box and reference box
			const FRigidTransform3* RefBoxTM;
			const FRigidTransform3* OtherBoxTM;
			const FImplicitBox3* RefBox;
			const FImplicitBox3* OtherBox;

			if (ReferenceFaceBox1)
			{
				RefBoxTM = &Box1TransformCenter;
				OtherBoxTM = &Box2TransformCenter;
				RefBox = &Box1;
				OtherBox = &Box2;
			}
			else
			{
				RefBoxTM = &Box2TransformCenter;
				OtherBoxTM = &Box1TransformCenter;
				RefBox = &Box2;
				OtherBox = &Box1;
			}

			// Populate the clipped vertices by the other face's vertices

			// Populate initial clipping vertices with a face from the other box
			FVec3 otherBoxHalfExtents = 0.5f * OtherBox->Extents();
			uint32 ConstantCoordinateIndex = ReferenceFaceBox1 ? BestFaceNormalAxisBox2 : BestFaceNormalAxisBox1;
			FReal ConstantCoordinate = otherBoxHalfExtents[ConstantCoordinateIndex] * (FReal)(ReferenceFaceBox1 ? BestFaceNormalAxisDirectionBox2 : BestFaceNormalAxisDirectionBox1);

			uint32 VariableCoordinateIndices[2];
			FReal VariableCoordinates[2];

			uint32 VariableCoordinateCount = 0;
			for (uint32 Coordinate = 0; Coordinate < 3; ++Coordinate)
			{
				if (Coordinate != ConstantCoordinateIndex)
				{
					VariableCoordinateIndices[VariableCoordinateCount] = Coordinate;
					VariableCoordinates[VariableCoordinateCount] = otherBoxHalfExtents[Coordinate];
					++VariableCoordinateCount;
				}
			}

			ContactPointCount = 4; // Number of face vertices
			const uint32 GreyCode[4] = { 0, 1, 3, 2 }; // Grey code to make sure we add vertices in correct order
			FVec3 ClippedVertices[MaxContactPointCount];
			// Add the vertices in an order that will form a closed loop
			const FRigidTransform3 BoxOtherToRef = OtherBoxTM->GetRelativeTransform(*RefBoxTM);
			for (uint32 Vertex = 0; Vertex < ContactPointCount; Vertex++)
			{
				ClippedVertices[Vertex][ConstantCoordinateIndex] = ConstantCoordinate;
				ClippedVertices[Vertex][VariableCoordinateIndices[0]] = (GreyCode[Vertex] & (1 << 0)) ? VariableCoordinates[0] : -VariableCoordinates[0];
				ClippedVertices[Vertex][VariableCoordinateIndices[1]] = (GreyCode[Vertex] & (1 << 1)) ? VariableCoordinates[1] : -VariableCoordinates[1];
				ClippedVertices[Vertex] = BoxOtherToRef.TransformPositionNoScale(ClippedVertices[Vertex]);
			}

			// Now clip against all planes that belong to the reference plane's, edges
			FVec3 ClippedVertices2[MaxContactPointCount]; // We will use a double buffer as an optimization
			FVec3* VertexBuffer1 = ClippedVertices;
			FVec3* VertexBuffer2 = ClippedVertices2;

			FVec3 refBoxHalfExtents = 0.5f * RefBox->Extents();
			uint32 RefPlaneCoordinateIndex = ReferenceFaceBox1 ? BestFaceNormalAxisBox1 : BestFaceNormalAxisBox2;
			for (uint32 Coordinate = 0; Coordinate < 3; ++Coordinate)
			{
				if (Coordinate != RefPlaneCoordinateIndex)
				{
					ContactPointCount = BoxBoxClipVerticesAgainstPlane(VertexBuffer1, VertexBuffer2, ContactPointCount, Coordinate, refBoxHalfExtents[Coordinate]);
					ContactPointCount = BoxBoxClipVerticesAgainstPlane(VertexBuffer2, VertexBuffer1, ContactPointCount, Coordinate, -refBoxHalfExtents[Coordinate]);
				}
			}

			// Reduce number of contacts to a maximum of 4
			if (ContactPointCount > 4)
			{
				FRotation3 RotateSeperationToZ = FRotation3::FromRotatedVector(ReferenceFaceBox1 ? SeparationDirectionLocalBox1 : SeparationDirectionLocalBox2, FVec3(0.0f, 0.0f, 1.0f));
				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ * ClippedVertices[ContactPointIndex];
				}

				ContactPointCount = ReduceManifoldContactPoints(ClippedVertices, ContactPointCount);

				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ.Inverse() * ClippedVertices[ContactPointIndex];
				}
			}

			// Generate the contact points from the clipped vertices
			for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
			{
				FContactPoint ContactPoint;
				const FVec3 VertexInReferenceCubeCoordinates = ClippedVertices[ContactPointIndex];
				FVec3 PointProjectedOntoReferenceFace = VertexInReferenceCubeCoordinates;
				PointProjectedOntoReferenceFace[RefPlaneCoordinateIndex] = refBoxHalfExtents[RefPlaneCoordinateIndex] * (FReal)(ReferenceFaceBox1 ? BestFaceNormalAxisDirectionBox1 : BestFaceNormalAxisDirectionBox2);
				FVec3 ClippedPointInOtherCubeCoordinates = BoxOtherToRef.InverseTransformPositionNoScale(VertexInReferenceCubeCoordinates);

				ContactPoint.ShapeContactPoints[0] = ReferenceFaceBox1 ? PointProjectedOntoReferenceFace + RefBox->GetCenter() : ClippedPointInOtherCubeCoordinates + OtherBox->GetCenter();
				ContactPoint.ShapeContactPoints[1] = ReferenceFaceBox1 ? ClippedPointInOtherCubeCoordinates + OtherBox->GetCenter() : PointProjectedOntoReferenceFace + RefBox->GetCenter();
				ContactPoint.ShapeContactNormal = ReferenceFaceBox1 ? SeparationDirectionLocalBox1 : SeparationDirectionLocalBox2;
				ContactPoint.ContactNormalOwnerIndex = ReferenceFaceBox1 ? 0 : 1;
				ContactPoint.Location = RefBoxTM->TransformPositionNoScale(PointProjectedOntoReferenceFace);
				ContactPoint.Normal = GJKContactPoint.Normal;
				ContactPoint.Phi = FVec3::DotProduct(PointProjectedOntoReferenceFace - VertexInReferenceCubeCoordinates, ReferenceFaceBox1 ? SeparationDirectionLocalBox1 : -SeparationDirectionLocalBox2);

				Constraint.AddOneshotManifoldContact(ContactPoint, bInInitialize);
			}
		}

		/////////////////////////////
		/// General Convexes
		/////////////////////////////

		// Reduce the number of contact points (in place)
		// Prerequisites to calling this function:
		// The points should be in a reference frame such that the z-axis is the in the direction of the separation vector
		uint32 ReduceManifoldContactPoints(FVec3* Points, uint32 PointCount)
		{
			uint32 OutPointCount = 0;
			if (PointCount <= 4)
				return PointCount;

			// Point 1) Find the deepest contact point
			{
				uint32 DeepestPointIndex = 0;
				FReal DeepestPointPhi = FLT_MAX;
				for (uint32 PointIndex = 0; PointIndex < PointCount; PointIndex++)
				{
					if (Points[PointIndex].Z < DeepestPointPhi)
					{
						DeepestPointIndex = PointIndex;
						DeepestPointPhi = Points[PointIndex].Z;
					}
				}
				// Deepest point will be our first output point
				Swap(Points[0], Points[DeepestPointIndex]);
				++OutPointCount;
			}

			// Point 2) Find the point with the largest distance to the deepest contact point (projected onto the separation plane)
			{
				uint32 FarthestPointIndex = 1;
				FReal FarthestPointDistanceSQR = -1.0f;
				for (uint32 PointIndex = 1; PointIndex < PointCount; PointIndex++)
				{
					FReal PointAToPointBSizeSQR = (Points[PointIndex] - Points[0]).SizeSquared2D();
					if (PointAToPointBSizeSQR > FarthestPointDistanceSQR)
					{
						FarthestPointIndex = PointIndex;
						FarthestPointDistanceSQR = PointAToPointBSizeSQR;
					}
				}
				// Farthest point will be added now
				Swap(Points[1], Points[FarthestPointIndex]);
				++OutPointCount;
			}

			// Point 3) Largest triangle area
			{
				uint32 LargestTrianglePointIndex = 2;
				FReal LargestTrianglePointSignedArea = 0.0f; // This will actually be double the signed area
				FVec3 P0to1 = Points[1] - Points[0];
				for (uint32 PointIndex = 2; PointIndex < PointCount; PointIndex++)
				{
					FReal TriangleSignedArea = (FVec3::CrossProduct(P0to1, Points[PointIndex] - Points[0])).Z; // Dot in direction of separation vector
					if (FMath::Abs(TriangleSignedArea) > FMath::Abs(LargestTrianglePointSignedArea))
					{
						LargestTrianglePointIndex = PointIndex;
						LargestTrianglePointSignedArea = TriangleSignedArea;
					}
				}
				// Point causing the largest triangle will be added now
				Swap(Points[2], Points[LargestTrianglePointIndex]);
				++OutPointCount;
				// Ensure the winding order is consistent
				if (LargestTrianglePointSignedArea < 0)
				{
					Swap(Points[0], Points[1]);
				}
			}

			// Point 4) Find the largest triangle connecting with our current triangle
			{
				uint32 LargestTrianglePointIndex = 3;
				FReal LargestPositiveTrianglePointSignedArea = 0.0f;
				for (uint32 PointIndex = 3; PointIndex < PointCount; PointIndex++)
				{
					for (uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++)
					{
						FReal TriangleSignedArea = (FVec3::CrossProduct(Points[PointIndex] - Points[EdgeIndex], Points[(EdgeIndex + 1) % 3] - Points[EdgeIndex])).Z; // Dot in direction of separation vector
						if (TriangleSignedArea > LargestPositiveTrianglePointSignedArea)
						{
							LargestTrianglePointIndex = PointIndex;
							LargestPositiveTrianglePointSignedArea = TriangleSignedArea;
						}
					}
				}
				// Point causing the largest positive triangle area will be added now
				Swap(Points[3], Points[LargestTrianglePointIndex]);
				++OutPointCount;
			}

			return OutPointCount; // This should always be 4
		}

		// This function will clip the input vertices by a reference shape's planes
		// more vertices may be added to outputVertexBuffer by this function
		// This is the core of the Sutherland-Hodgman algorithm
		// Plane Normals face outwards 
		uint32 ClipVerticesAgainstPlane(const FVec3* InputVertexBuffer, FVec3* outputVertexBuffer, uint32 ClipPointCount, uint32 MaxNumberOfOutputPoints, FVec3 ClippingPlaneNormal, FReal PlaneDistance)
		{

			auto CalculateIntersect = [=](const FVec3& Point1, const FVec3& Point2) -> FVec3
			{
				// Only needs to be valid if the line connecting Point1 with Point2 actually intersects
				FVec3 Result;

				FReal Denominator = FVec3::DotProduct(Point2 - Point1, ClippingPlaneNormal); // Can be negative
				if (FMath::Abs(Denominator) < SMALL_NUMBER)
				{
					Result = Point1;
				}
				else
				{
					FReal Alpha = (PlaneDistance - FVec3::DotProduct(Point1, ClippingPlaneNormal)) / Denominator;
					Result = FMath::Lerp(Point1, Point2, Alpha);
				}
				return Result;
			};

			auto InsideClipFace = [=](const FVec3& Point) -> bool
			{
				// Epsilon is there so that previously clipped points will still be inside the plane
				return FVec3::DotProduct(Point, ClippingPlaneNormal) <= PlaneDistance + PlaneDistance * SMALL_NUMBER; 
			};

			uint32 NewClipPointCount = 0;

			for (uint32 ClipPointIndex = 0; ClipPointIndex < ClipPointCount; ClipPointIndex++)
			{
				FVec3 CurrentClipPoint = InputVertexBuffer[ClipPointIndex];
				FVec3 PrevClipPoint = InputVertexBuffer[(ClipPointIndex + ClipPointCount - 1) % ClipPointCount];
				FVec3 InterSect = CalculateIntersect(PrevClipPoint, CurrentClipPoint);

				if (InsideClipFace(CurrentClipPoint))
				{
					if (!InsideClipFace(PrevClipPoint))
					{
						outputVertexBuffer[NewClipPointCount++] = InterSect;
						if (NewClipPointCount >= MaxNumberOfOutputPoints)
						{
							break;
						}
					}
					outputVertexBuffer[NewClipPointCount++] = CurrentClipPoint;
				}
				else if (InsideClipFace(PrevClipPoint))
				{
					outputVertexBuffer[NewClipPointCount++] = InterSect;
				}

				if (NewClipPointCount >= MaxNumberOfOutputPoints)
				{
					break;
				}
			}

			return NewClipPointCount;
		}
		
		template <typename ConvexImplicitType>
		void ConstructConvexConvexOneShotManifold(
			const ConvexImplicitType& Convex1,
			const FRigidTransform3& Convex1Transform, //world
			const ConvexImplicitType& Convex2,
			const FRigidTransform3& Convex2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize)
		{
			const uint32 SpaceDimension = 3;

			// We only build one shot manifolds once
			// All convexes are pre-scaled
			ensure(Constraint.GetManifoldPoints().Num() == 0);
			ensure(Convex1Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(Convex2Transform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			const uint32 MaxContactPointCount = 32; // This should be tuned
			uint32 ContactPointCount = 0;

			// Use GJK only once
			const FContactPoint GJKContactPoint = GenericConvexConvexContactPoint(Convex1, Convex1Transform, Convex2, Convex2Transform, CullDistance, Constraint.Manifold.RestitutionPadding);

			// ToDo: should we generate no contacts here?
			//if (GJKContactPoint.Phi >= CullDistance)
				//return;

			const FVec3 SeparationDirectionLocalConvex1 = Convex1Transform.InverseTransformVectorNoScale(GJKContactPoint.Normal);

			const int32 MostOpposingPlaneIndexConvex1 = Convex1.GetMostOpposingPlane(SeparationDirectionLocalConvex1);
			const TPlaneConcrete<FReal, 3> BestPlaneConvex1 = Convex1.GetPlane(MostOpposingPlaneIndexConvex1);
			const FReal BestPlaneDotNormalConvex1 = FVec3::DotProduct(-SeparationDirectionLocalConvex1, BestPlaneConvex1.Normal());

			// Now for Convex2
			const FVec3 SeparationDirectionLocalConvex2 = Convex2Transform.InverseTransformVectorNoScale(GJKContactPoint.Normal);
			const int32 MostOpposingPlaneIndexConvex2 = Convex2.GetMostOpposingPlane(-SeparationDirectionLocalConvex2);
			const TPlaneConcrete<FReal, 3> BestPlaneConvex2 = Convex2.GetPlane(MostOpposingPlaneIndexConvex2);
			const FReal BestPlaneDotNormalConvex2 = FVec3::DotProduct(SeparationDirectionLocalConvex2, BestPlaneConvex2.Normal());

			const FReal SmallBiasToPreventFeatureFlipping = 0.002; // This improves frame coherence by penalizing convex 1 in favour of convex 2
			bool ReferenceFaceConvex1 = true; // Is the reference face on convex1 or convex2?
			if (BestPlaneDotNormalConvex2 + SmallBiasToPreventFeatureFlipping > BestPlaneDotNormalConvex1)
			{
				ReferenceFaceConvex1 = false;
			}

			// Setup pointers to other Convex and reference Convex
			const FRigidTransform3* RefConvexTM;
			const FRigidTransform3* OtherConvexTM;
			const ConvexImplicitType* RefConvex;
			const ConvexImplicitType* OtherConvex;

			if (ReferenceFaceConvex1)
			{
				RefConvexTM = &Convex1Transform;
				OtherConvexTM = &Convex2Transform;
				RefConvex = &Convex1;
				OtherConvex = &Convex2;
			}
			else
			{
				RefConvexTM = &Convex2Transform;
				OtherConvexTM = &Convex1Transform;
				RefConvex = &Convex2;
				OtherConvex = &Convex1;
			}

			// Populate the clipped vertices by the other face's vertices
			const FRigidTransform3 ConvexOtherToRef = OtherConvexTM->GetRelativeTransform(*RefConvexTM);
			FVec3 ClippedVertices[MaxContactPointCount];
			TArrayView<const int32> OtherConvexFaceVertices =  OtherConvex->GetPlaneVertices(ReferenceFaceConvex1? MostOpposingPlaneIndexConvex2 : MostOpposingPlaneIndexConvex1);
			ContactPointCount = FMath::Min(OtherConvexFaceVertices.Num(), (int32)MaxContactPointCount); // Number of face vertices

			for (int32 VertexIndex = 0; VertexIndex < (int32)ContactPointCount; ++VertexIndex)
			{
				// Todo Check for Grey code
				ClippedVertices[VertexIndex] = OtherConvex->GetVertex(OtherConvexFaceVertices[VertexIndex]);
				ClippedVertices[VertexIndex] = ConvexOtherToRef.TransformPositionNoScale(ClippedVertices[VertexIndex]);
			}			

			// Now clip against all planes that belong to the reference plane's, edges
			
			FVec3 ClippedVertices2[MaxContactPointCount]; // We will use a double buffer as an optimization
			FVec3* VertexBuffer1 = ClippedVertices;
			FVec3* VertexBuffer2 = ClippedVertices2;

			TArrayView<const int32> RefConvexFaceVertices = RefConvex->GetPlaneVertices(ReferenceFaceConvex1 ? MostOpposingPlaneIndexConvex1 : MostOpposingPlaneIndexConvex2);
			int32 ClippingPlaneCount = RefConvexFaceVertices.Num();
			for (int32 ClippingPlaneIndex = 0; ClippingPlaneIndex < ClippingPlaneCount; ++ClippingPlaneIndex)
			{
				// Note winding order matters here!
				FVec3 PrevPoint = RefConvex->GetVertex(RefConvexFaceVertices[(ClippingPlaneIndex + ClippingPlaneCount - 1) % ClippingPlaneCount]);
				FVec3 CurrentPoint = RefConvex->GetVertex(RefConvexFaceVertices[ClippingPlaneIndex]);
				FVec3 PlaneNormal = -FVec3::CrossProduct(ReferenceFaceConvex1 ? BestPlaneConvex1.Normal() : BestPlaneConvex2.Normal(), CurrentPoint - PrevPoint);
				PlaneNormal.SafeNormalize();
				ContactPointCount = ClipVerticesAgainstPlane(VertexBuffer1, VertexBuffer2, ContactPointCount, MaxContactPointCount, PlaneNormal, FVec3::DotProduct(CurrentPoint, PlaneNormal));
				Swap(VertexBuffer1, VertexBuffer2); // VertexBuffer1 will now point to the latest
			}

			
			// Reduce number of contacts to a maximum of 4
			if (ContactPointCount > 4)
			{
				FRotation3 RotateSeperationToZ = FRotation3::FromRotatedVector(ReferenceFaceConvex1 ? SeparationDirectionLocalConvex1 : SeparationDirectionLocalConvex2, FVec3(0.0f, 0.0f, 1.0f));
				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ * ClippedVertices[ContactPointIndex];
				}

				ContactPointCount = ReduceManifoldContactPoints(ClippedVertices, ContactPointCount);

				for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
				{
					ClippedVertices[ContactPointIndex] = RotateSeperationToZ.Inverse() * ClippedVertices[ContactPointIndex];
				}
			}

			// Generate the contact points from the clipped vertices
			for (uint32 ContactPointIndex = 0; ContactPointIndex < ContactPointCount; ++ContactPointIndex)
			{
				FContactPoint ContactPoint;
				const FVec3 VertexInReferenceCubeCoordinates = VertexBuffer1[ContactPointIndex];
				const TPlaneConcrete<FReal, 3> RefPlane = ReferenceFaceConvex1 ? BestPlaneConvex1 : BestPlaneConvex2;
				const FVec3 RefFaceNormal = ReferenceFaceConvex1 ? BestPlaneConvex1.Normal() : BestPlaneConvex2.Normal();
				FReal ReferencePlaneDistance = FVec3::DotProduct(RefPlane.X(), RefPlane.Normal());
				FVec3 PointProjectedOntoReferenceFace = VertexInReferenceCubeCoordinates - (FVec3::DotProduct(VertexInReferenceCubeCoordinates, RefPlane.Normal()) - ReferencePlaneDistance) * RefPlane.Normal();
				FVec3 ClippedPointInOtherCubeCoordinates = ConvexOtherToRef.InverseTransformPositionNoScale(VertexInReferenceCubeCoordinates);

				ContactPoint.ShapeContactPoints[0] = ReferenceFaceConvex1 ? PointProjectedOntoReferenceFace : ClippedPointInOtherCubeCoordinates;
				ContactPoint.ShapeContactPoints[1] = ReferenceFaceConvex1 ? ClippedPointInOtherCubeCoordinates : PointProjectedOntoReferenceFace;
				ContactPoint.ShapeContactNormal = ReferenceFaceConvex1 ? SeparationDirectionLocalConvex1 : SeparationDirectionLocalConvex2;
				ContactPoint.ContactNormalOwnerIndex = ReferenceFaceConvex1 ? 0 : 1;
				ContactPoint.Location = RefConvexTM->TransformPositionNoScale(PointProjectedOntoReferenceFace);
				ContactPoint.Normal = GJKContactPoint.Normal;
				ContactPoint.Phi = FVec3::DotProduct(PointProjectedOntoReferenceFace - VertexInReferenceCubeCoordinates, ReferenceFaceConvex1 ? SeparationDirectionLocalConvex1 : -SeparationDirectionLocalConvex2);

				Constraint.AddOneshotManifoldContact(ContactPoint, bInInitialize);
			}
		}

		template 
		void ConstructConvexConvexOneShotManifold<FImplicitBox3>(
			const FImplicitBox3& Implicit1,
			const FRigidTransform3& Convex1Transform, //world
			const FImplicitBox3& Implicit2,
			const FRigidTransform3& Convex2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize);


		template
		void ConstructConvexConvexOneShotManifold<FConvex>(
				const FConvex& Implicit1,
				const FRigidTransform3& Convex1Transform, //world
				const FConvex& Implicit2,
				const FRigidTransform3& Convex2Transform, //world
				const FReal CullDistance,
				FRigidBodyPointContactConstraint& Constraint,
				bool bInInitialize);

		


	}
}

