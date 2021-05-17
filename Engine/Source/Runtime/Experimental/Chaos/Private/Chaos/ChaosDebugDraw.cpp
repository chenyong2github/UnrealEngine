// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/HeightField.h"
#include "Chaos/Triangle.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Evolution/SimulationSpace.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintColor.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace DebugDraw
	{
#if CHAOS_DEBUG_DRAW

		bool bChaosDebugDebugDrawShapeBounds = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawShapeBounds(TEXT("p.Chaos.DebugDraw.ShowShapeBounds"), bChaosDebugDebugDrawShapeBounds, TEXT("Whether to show the bounds of each shape in DrawShapes"));

		bool bChaosDebugDebugDrawCollisionParticles = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawShapeParticles(TEXT("p.Chaos.DebugDraw.ShowCollisionParticles"), bChaosDebugDebugDrawCollisionParticles, TEXT("Whether to show the collision particles if present"));

		bool bChaosDebugDebugDrawInactiveContacts = true;
		FAutoConsoleVariableRef CVarChaosDebugDrawInactiveContacts(TEXT("p.Chaos.DebugDraw.ShowInactiveContacts"), bChaosDebugDebugDrawInactiveContacts, TEXT("Whether to show inactive contacts (ones that contributed no impulses or pushout)"));

		bool bChaosDebugDebugDrawConvexVertices = false;
		bool bChaosDebugDebugDrawCoreShapes = false;
		bool bChaosDebugDebugDrawExactCoreShapes = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawConvexVertices(TEXT("p.Chaos.DebugDraw.ShowConvexVertices"), bChaosDebugDebugDrawConvexVertices, TEXT("Whether to show the vertices of convex shapes"));
		FAutoConsoleVariableRef CVarChaosDebugDrawCoreShapes(TEXT("p.Chaos.DebugDraw.ShowCoreShapes"), bChaosDebugDebugDrawCoreShapes, TEXT("Whether to show the core (margin-reduced) shape where applicable"));
		FAutoConsoleVariableRef CVarChaosDebugDrawExactShapes(TEXT("p.Chaos.DebugDraw.ShowExactCoreShapes"), bChaosDebugDebugDrawExactCoreShapes, TEXT("Whether to show the exact core shape. NOTE: Extremely expensive and should only be used on a small scene with a couple convex shapes in it"));

		bool bChaosDebugDebugDrawContactGraph = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawContactGraph(TEXT("p.Chaos.DebugDraw.ShowContactGraph"), bChaosDebugDebugDrawContactGraph, TEXT("Whether to show the contactgraph when drawing islands"));

		float ChaosDebugDrawConvexExplodeDistance = 0.0f;
		FAutoConsoleVariableRef CVarChaosDebugDrawConvexExplodeDistance(TEXT("p.Chaos.DebugDraw.ConvexExplodeDistance"), ChaosDebugDrawConvexExplodeDistance, TEXT("Explode convex edges by this amount (useful for looking at convex integrity)"));

		// NOTE: These settings should never really be used - they are the fallback defaults
		// if the user does not specify settings in the debug draw call.
		// See PBDRigidsColver.cpp and ImmediatePhysicsSimulation_Chaos.cpp for example.
		FChaosDebugDrawSettings ChaosDefaultDebugDebugDrawSettings(
			/* ArrowSize =					*/ 1.5f,
			/* BodyAxisLen =				*/ 4.0f,
			/* ContactLen =					*/ 4.0f,
			/* ContactWidth =				*/ 2.0f,
			/* ContactPhiWidth =			*/ 0.0f,
			/* ContactOwnerWidth =			*/ 0.0f,
			/* ConstraintAxisLen =			*/ 5.0f,
			/* JointComSize =				*/ 2.0f,
			/* LineThickness =				*/ 0.15f,
			/* DrawScale =					*/ 1.0f,
			/* FontHeight =					*/ 10.0f,
			/* FontScale =					*/ 1.5f,
			/* ShapeThicknesScale =			*/ 1.0f,
			/* PointSize =					*/ 2.0f,
			/* VelScale =					*/ 0.0f,
			/* AngVelScale =				*/ 0.0f,
			/* ImpulseScale =				*/ 0.0f,
			/* DrawPriority =				*/ 10.0f,
			/* bShowSimple =				*/ true,
			/* bShowComplex =				*/ false,
			/* bInShowLevelSetCollision =	*/ false
		);

		const FChaosDebugDrawSettings& GetChaosDebugDrawSettings(const FChaosDebugDrawSettings* InSettings)
		{
			if (InSettings != nullptr)
			{
				return *InSettings;
			}
			return ChaosDefaultDebugDebugDrawSettings;
		}

		//
		//
		//

		void DrawShapesImpl(const FGeometryParticleHandle* Particle, const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FReal Margin, const FColor& Color, const FChaosDebugDrawSettings& Settings);

		void DrawShape(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			DrawShapesImpl(nullptr, ShapeTransform, Shape, 0.0f, Color, GetChaosDebugDrawSettings(Settings));
		}

		void DrawShapesConvexImpl(const TGeometryParticleHandle<FReal, 3>* Particle, const FRigidTransform3& ShapeTransform, const FConvex* Shape, const FReal InMargin, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			if (Shape->HasStructureData())
			{
				const FReal Margin = InMargin + Shape->GetMargin();

				for (int32 PlaneIndex = 0; PlaneIndex < Shape->GetFaces().Num(); ++PlaneIndex)
				{
					const int32 PlaneVerticesNum = Shape->NumPlaneVertices(PlaneIndex);
					for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVerticesNum; ++PlaneVertexIndex)
					{
						const int32 VertexIndex0 = Shape->GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
						const int32 VertexIndex1 = Shape->GetPlaneVertex(PlaneIndex, Utilities::WrapIndex(PlaneVertexIndex + 1, 0, PlaneVerticesNum));

						const FVec3 OuterP0 = ShapeTransform.TransformPosition(Shape->GetVertex(VertexIndex0));
						const FVec3 OuterP1 = ShapeTransform.TransformPosition(Shape->GetVertex(VertexIndex1));

						const FVec3 N0 = ShapeTransform.TransformVectorNoScale(Shape->GetPlane(PlaneIndex).Normal());
						const FVec3 ExplodeDelta = ChaosDebugDrawConvexExplodeDistance * N0;

						// Outer shape
						FDebugDrawQueue::GetInstance().DrawDebugLine(OuterP0 + ExplodeDelta, OuterP1 + ExplodeDelta, Color, false, -1.f, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);

						// Core shape and lines connecting core to outer
						if (Margin > 0.0f)
						{
							const FReal LineThickness = 0.5f * Settings.ShapeThicknesScale * Settings.LineThickness;
							const FVec3 InnerP0 = ShapeTransform.TransformPositionNoScale(Shape->GetMarginAdjustedVertexScaled(VertexIndex0, Margin, ShapeTransform.GetScale3D()));
							const FVec3 InnerP1 = ShapeTransform.TransformPositionNoScale(Shape->GetMarginAdjustedVertexScaled(VertexIndex1, Margin, ShapeTransform.GetScale3D()));
							FDebugDrawQueue::GetInstance().DrawDebugLine(InnerP0, InnerP1, FColor::Blue, false, -1.f, Settings.DrawPriority, LineThickness);
							FDebugDrawQueue::GetInstance().DrawDebugLine(InnerP0, OuterP0, FColor::Black, false, -1.f, Settings.DrawPriority, LineThickness);
						}

						// Vertex and face normal
						if (bChaosDebugDebugDrawConvexVertices)
						{
							FDebugDrawQueue::GetInstance().DrawDebugLine(OuterP0 + ExplodeDelta, OuterP0 + ExplodeDelta + Settings.DrawScale * 20.0f * N0, FColor::Green, false, -1.f, Settings.DrawPriority, Settings.LineThickness);
						}
					}
				}
			}
		}

		void DrawShapesHeightFieldImpl(const TGeometryParticleHandle<FReal, 3>* Particle, const FRigidTransform3& ShapeTransform, const FHeightField* Shape, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			const FVec3& WorldQueryCenter = FDebugDrawQueue::GetInstance().GetCenterOfInterest();
			const FReal WorldQueryRadius = FDebugDrawQueue::GetInstance().GetRadiusOfInterest();
			const FAABB3 WorldQueryBounds = FAABB3(WorldQueryCenter - FVec3(WorldQueryRadius), WorldQueryCenter + FVec3(WorldQueryRadius));
			const FAABB3 LocalQueryBounds = WorldQueryBounds.InverseTransformedAABB(ShapeTransform);

			Shape->VisitTriangles(LocalQueryBounds, [&](const FTriangle& Tri)
				{
					FVec3 A = ShapeTransform.TransformPosition(Tri[0]);
					FVec3 B = ShapeTransform.TransformPosition(Tri[1]);
					FVec3 C = ShapeTransform.TransformPosition(Tri[2]);
					FDebugDrawQueue::GetInstance().DrawDebugLine(A, B, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
					FDebugDrawQueue::GetInstance().DrawDebugLine(B, C, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
					FDebugDrawQueue::GetInstance().DrawDebugLine(C, A, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
				});
		}

		void DrawShapesTriangleMeshImpl(const TGeometryParticleHandle<FReal, 3>* Particle, const FRigidTransform3& ShapeTransform, const FTriangleMeshImplicitObject* Shape, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			const FVec3& WorldQueryCenter = FDebugDrawQueue::GetInstance().GetCenterOfInterest();
			const FReal WorldQueryRadius = FDebugDrawQueue::GetInstance().GetRadiusOfInterest();
			const FAABB3 WorldQueryBounds = FAABB3(WorldQueryCenter - FVec3(WorldQueryRadius), WorldQueryCenter + FVec3(WorldQueryRadius));
			const FAABB3 LocalQueryBounds = WorldQueryBounds.InverseTransformedAABB(ShapeTransform);

			Shape->VisitTriangles(LocalQueryBounds, [&](const FTriangle& Tri)
			{
				FVec3 A = ShapeTransform.TransformPosition(Tri[0]);
				FVec3 B = ShapeTransform.TransformPosition(Tri[1]);
				FVec3 C = ShapeTransform.TransformPosition(Tri[2]);
				FDebugDrawQueue::GetInstance().DrawDebugLine(A, B, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(B, C, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(C, A, Color, false, -1.f, 0, Settings.ShapeThicknesScale * Settings.LineThickness);
			});
		}

		void DrawShapesLevelSetImpl(const TGeometryParticleHandle<FReal, 3>* Particle, const FRigidTransform3& ShapeTransform, const FLevelSet* Shape, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			if (!Settings.bShowLevelSetCollision)
			{
				return;
			}

			if (const TPBDRigidParticleHandle<FReal, 3>* Rigid = Particle->CastToRigidParticle())
			{
				const TUniquePtr<TBVHParticles<FReal, 3>>& CollisionParticles = Rigid->CollisionParticles();
				if (CollisionParticles != nullptr)
				{
					for (int32 ParticleIndex = 0; ParticleIndex < (int32)CollisionParticles->Size(); ++ParticleIndex)
					{
						const FVec3 P = ShapeTransform.TransformPosition(CollisionParticles->X(ParticleIndex));
						FDebugDrawQueue::GetInstance().DrawDebugPoint(P, Color, false, -1.f, 0, Settings.PointSize);
					}
				}
			}
		}

		template <bool bInstanced>
		void DrawShapesScaledImpl(const FGeometryParticleHandle* Particle, const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FReal Margin, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			const EImplicitObjectType PackedType = Shape->GetType();
			const EImplicitObjectType InnerType = GetInnerType(PackedType);
			CHAOS_CHECK(IsScaled(PackedType));
			CHAOS_CHECK(IsInstanced(PackedType) == bInstanced);

			FRigidTransform3 ScaleTM = FRigidTransform3::Identity;
			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
				break;
			case ImplicitObjectType::Box:
				break;
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
				break;
			case ImplicitObjectType::Transformed:
				break;
			case ImplicitObjectType::Union:
				break;
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
			{
				const TImplicitObjectScaled<FConvex, bInstanced>* Scaled = Shape->template GetObject<TImplicitObjectScaled<FConvex, bInstanced>>();
				ScaleTM.SetScale3D(Scaled->GetScale());
				DrawShapesImpl(Particle, ScaleTM * ShapeTransform, Scaled->GetUnscaledObject(), Scaled->GetMargin(), Color, Settings);
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
			{
				const TImplicitObjectScaled<FTriangleMeshImplicitObject, bInstanced>* Scaled = Shape->template GetObject<TImplicitObjectScaled<FTriangleMeshImplicitObject, bInstanced>>();
				ScaleTM.SetScale3D(Scaled->GetScale());
				DrawShapesImpl(Particle, ScaleTM * ShapeTransform, Scaled->GetUnscaledObject(), 0.0f, Color, Settings);
				break;
			}
			case ImplicitObjectType::HeightField:
			{
				const TImplicitObjectScaled<FHeightField, bInstanced>* Scaled = Shape->template GetObject<TImplicitObjectScaled<FHeightField, bInstanced>>();
				ScaleTM.SetScale3D(Scaled->GetScale());
				DrawShapesImpl(Particle, ScaleTM * ShapeTransform, Scaled->GetUnscaledObject(), 0.0f, Color, Settings);
				break;
			}
			default:
				break;
			}
		}

		void DrawShapesInstancedImpl(const FGeometryParticleHandle* Particle, const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FReal Margin, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			const EImplicitObjectType PackedType = Shape->GetType();
			const EImplicitObjectType InnerType = GetInnerType(PackedType);
			CHAOS_CHECK(IsScaled(PackedType) == false);
			CHAOS_CHECK(IsInstanced(PackedType));

			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
				break;
			case ImplicitObjectType::Box:
				break;
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
				break;
			case ImplicitObjectType::Transformed:
				break;
			case ImplicitObjectType::Union:
				break;
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
			{
				const TImplicitObjectInstanced<FConvex>* Instanced = Shape->template GetObject<TImplicitObjectInstanced<FConvex>>();
				DrawShapesImpl(Particle, ShapeTransform, Instanced->GetInstancedObject(), Instanced->GetMargin(), Color, Settings);
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
			{
				const TImplicitObjectInstanced<FTriangleMeshImplicitObject>* Scaled = Shape->template GetObject<TImplicitObjectInstanced<FTriangleMeshImplicitObject>>();
				DrawShapesImpl(Particle, ShapeTransform, Scaled->GetInstancedObject(), 0.0f, Color, Settings);
				break;
			}
			case ImplicitObjectType::HeightField:
			{
				const TImplicitObjectInstanced<FHeightField>* Scaled = Shape->template GetObject<TImplicitObjectInstanced<FHeightField>>();
				DrawShapesImpl(Particle, ShapeTransform, Scaled->GetInstancedObject(), 0.0f, Color, Settings);
				break;
			}
			default:
				break;
			}
		}

		void DrawShapesImpl(const FGeometryParticleHandle* Particle, const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FReal Margin, const FColor& Color, const FChaosDebugDrawSettings& Settings)
		{
			const EImplicitObjectType PackedType = Shape->GetType(); // Type includes scaling and instancing data
			const EImplicitObjectType InnerType = GetInnerType(Shape->GetType());

			// Unwrap the wrapper/aggregating shapes
			if (IsScaled(PackedType))
			{
				if (IsInstanced(PackedType))
				{
					DrawShapesScaledImpl<true>(Particle, ShapeTransform, Shape, Margin, Color, Settings);
				}
				else
				{
					DrawShapesScaledImpl<false>(Particle, ShapeTransform, Shape, Margin, Color, Settings);
				}
				return;
			}
			else if (IsInstanced(PackedType))
			{
				DrawShapesInstancedImpl(Particle, ShapeTransform, Shape, Margin, Color, Settings);
				return;
			}
			else if (InnerType == ImplicitObjectType::Transformed)
			{
				const TImplicitObjectTransformed<FReal, 3>* Transformed = Shape->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform = FRigidTransform3(ShapeTransform.TransformPosition(Transformed->GetTransform().GetLocation()), ShapeTransform.GetRotation() * Transformed->GetTransform().GetRotation());
				DrawShapesImpl(Particle, TransformedTransform, Transformed->GetTransformedObject(), Margin, Color, Settings);
				return;
			}
			else if (InnerType == ImplicitObjectType::Union)
			{
				const FImplicitObjectUnion* Union = Shape->template GetObject<FImplicitObjectUnion>();
				for (auto& UnionShape : Union->GetObjects())
				{
					DrawShapesImpl(Particle, ShapeTransform, UnionShape.Get(), Margin, Color, Settings);
				}
				return;
			}
			else if (InnerType == ImplicitObjectType::UnionClustered)
			{
				const FImplicitObjectUnionClustered* Union = Shape->template GetObject<FImplicitObjectUnionClustered>();
				for (auto& UnionShape : Union->GetObjects())
				{
					DrawShapesImpl(Particle, ShapeTransform, UnionShape.Get(), Margin, Color, Settings);
				}
				return;
			}


			// Whether we should show meshes and non-mesh shapes
			bool bShowMeshes = Settings.bShowComplexCollision;
			bool bShowNonMeshes = Settings.bShowSimpleCollision;
			const FPerShapeData* ShapeData = Particle->GetImplicitShape(Shape);
			if (ShapeData != nullptr)
			{
				bShowMeshes = (Settings.bShowComplexCollision && (ShapeData->GetCollisionTraceType() != EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAsComplex))
					|| (Settings.bShowSimpleCollision && (ShapeData->GetCollisionTraceType() == EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple));
				bShowNonMeshes = (Settings.bShowSimpleCollision && (ShapeData->GetCollisionTraceType() != EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple))
					|| (Settings.bShowComplexCollision && (ShapeData->GetCollisionTraceType() == EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAsComplex));
			}

			// Quit if we don't want to show this shape
			const bool bIsMesh = (InnerType == ImplicitObjectType::TriangleMesh);
			if (bIsMesh && !bShowMeshes)
			{
				return;
			}
			else if (!bIsMesh && !bShowNonMeshes)
			{
				return;
			}

			// If we get here, we have an actual shape to render
			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
			{
				const TSphere<FReal, 3>* Sphere = Shape->template GetObject<TSphere<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Sphere->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugSphere(P, Sphere->GetRadius(), 8, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::Box:
			{
				const TBox<FReal, 3>* Box = Shape->template GetObject<TBox<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Box->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugBox(P, (FReal)0.5 * Box->Extents(), ShapeTransform.GetRotation(), Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
			{
				const FCapsule* Capsule = Shape->template GetObject<FCapsule>();
				const FVec3 P = ShapeTransform.TransformPosition(Capsule->GetCenter());
				const FRotation3 Q = ShapeTransform.GetRotation() * FRotationMatrix::MakeFromZ(Capsule->GetAxis());
				FDebugDrawQueue::GetInstance().DrawDebugCapsule(P, (FReal)0.5 * Capsule->GetHeight() + Capsule->GetRadius(), Capsule->GetRadius(), Q, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::LevelSet:
			{
				const FLevelSet* LevelSet = Shape->template GetObject<FLevelSet>();
				DrawShapesLevelSetImpl(Particle, ShapeTransform, LevelSet, Color, Settings);
				break;
			}
			break;
			case ImplicitObjectType::Convex:
			{
				const FConvex* Convex = Shape->template GetObject<FConvex>();

				const FReal NetMargin = bChaosDebugDebugDrawCoreShapes ? Margin + Convex->GetMargin() : 0.0f;
				DrawShapesConvexImpl(Particle, ShapeTransform, Convex, NetMargin, Color, Settings);

				// Generate the exact marging-reduced convex for comparison with the runtime approximation
				// Warning: extremely expensive!
				if (bChaosDebugDebugDrawExactCoreShapes)
				{
					const FRigidTransform3 ShrunkScaledTransform = FRigidTransform3(ShapeTransform.GetTranslation(), ShapeTransform.GetRotation());
					TArray<FVec3> ScaledVerts = Convex->GetVertices();
					for (FVec3& Vert : ScaledVerts)
					{
						Vert = Vert * ShapeTransform.GetScale3D();
					}
					FConvex ShrunkScaledConvex(ScaledVerts, 0.0f);
					ShrunkScaledConvex.MovePlanesAndRebuild(-Margin);
					DrawShapesConvexImpl(Particle, ShrunkScaledTransform, &ShrunkScaledConvex, 0.0f, FColor::Red, Settings);
				}
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
			{
				const FTriangleMeshImplicitObject* TriangleMesh = Shape->template GetObject<FTriangleMeshImplicitObject>();
				DrawShapesTriangleMeshImpl(Particle, ShapeTransform, TriangleMesh, Color, Settings);
				break;
			}
			case ImplicitObjectType::HeightField:
			{
				const FHeightField* HeightField = Shape->template GetObject<FHeightField>();
				DrawShapesHeightFieldImpl(Particle, ShapeTransform, HeightField, Color, Settings);
				break;
			}
			default:
				break;
			}

			if (bChaosDebugDebugDrawCollisionParticles && (Particle != nullptr))
			{
				if (const TPBDRigidParticleHandle<FReal, 3>* Rigid = Particle->CastToRigidParticle())
				{
					const TUniquePtr<FBVHParticles>& Particles = Rigid->CollisionParticles();
					if (Particles != nullptr)
					{
						for (int32 ParticleIndex = 0; ParticleIndex < (int32)Particles->Size(); ++ParticleIndex)
						{
							FVec3 P = ShapeTransform.TransformPosition(Particles->X(ParticleIndex));
							FDebugDrawQueue::GetInstance().DrawDebugPoint(P, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.PointSize);
						}
					}
				}
			}

			if (bChaosDebugDebugDrawShapeBounds)
			{
				const FColor ShapeBoundsColor = FColor::Orange;
				const FAABB3& ShapeBounds = Shape->BoundingBox();
				const FVec3 ShapeBoundsPos = ShapeTransform.TransformPosition(ShapeBounds.Center());
				FDebugDrawQueue::GetInstance().DrawDebugBox(ShapeBoundsPos, 0.5f * ShapeBounds.Extents(), ShapeTransform.GetRotation(), ShapeBoundsColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* Particle, const FColor& InColor, const FChaosDebugDrawSettings& Settings)
		{
			FVec3 P = SpaceTransform.TransformPosition(Particle->ObjectState() == EObjectStateType::Dynamic ? Particle->CastToRigidParticle()->P() : Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->ObjectState() == EObjectStateType::Dynamic ? Particle->CastToRigidParticle()->Q() : Particle->R());

			// @todo(choas): move debug draw colors into debug draw settings
			FColor Color = InColor;
			if (Particle->ObjectState() == EObjectStateType::Sleeping)
			{
				Color = FColor(InColor.R / 2, InColor.G / 2, InColor.B / 2, InColor.A);
			}

			DrawShapesImpl(Particle, FRigidTransform3(P, Q), Particle->Geometry().Get(), 0.0f, Color, Settings);
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticle* Particle, const FColor& InColor, const FChaosDebugDrawSettings& Settings)
		{
			FVec3 P = SpaceTransform.TransformPosition(Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->R());

			// @todo(choas): move debug draw colors into debug draw settings
			FColor Color = InColor;
			if (Particle->ObjectState() == EObjectStateType::Sleeping)
			{
				Color = FColor(InColor.R / 2, InColor.G / 2, InColor.B / 2, InColor.A);
			}

			DrawShapesImpl(Particle->Handle(), FRigidTransform3(P, Q), Particle->Geometry().Get(), 0.0f, Color, Settings);
		}

		void DrawParticleBoundsImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* InParticle, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings& Settings)
		{
			FConstGenericParticleHandle Particle = InParticle;

			// This matches the calculation in SpatialAccelerationBroadPhase.h
			const FReal Box1Thickness = ComputeBoundsThickness(*InParticle, Dt, BoundsThickness, BoundsThicknessVelocityInflation).Size();
			const FAABB3 Box = ComputeWorldSpaceBoundingBox<FReal>(*InParticle).ThickenSymmetrically(FVec3(Box1Thickness));

			const FVec3 P = SpaceTransform.TransformPosition(Box.GetCenter());
			const FRotation3 Q = SpaceTransform.GetRotation();
			const FMatrix33 Qm = Q.ToMatrix();
			FColor Color = FColor::Black;
			if (InParticle->ObjectState() == EObjectStateType::Dynamic)
			{
				Color = FColor::White;
			}
			else if (InParticle->ObjectState() == EObjectStateType::Sleeping)
			{
				Color = FColor(128, 128, 128);
			}
			else if (InParticle->ObjectState() == EObjectStateType::Kinematic)
			{
				Color = FColor(64, 64, 64);
			}

			FDebugDrawQueue::GetInstance().DrawDebugBox(P, 0.5f * Box.Extents(), Q, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
		}

		void DrawParticleTransformImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* InParticle, int32 Index, FReal ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			FColor Red = (ColorScale * FColor::Red).ToFColor(false);
			FColor Green = (ColorScale * FColor::Green).ToFColor(false);
			FColor Blue = (ColorScale * FColor::Blue).ToFColor(false);

			FConstGenericParticleHandle Particle(InParticle);
			FVec3 PCOM = SpaceTransform.TransformPosition(FParticleUtilities::GetCoMWorldPosition(Particle));
			FRotation3 QCOM = SpaceTransform.GetRotation() * FParticleUtilities::GetCoMWorldRotation(Particle);
			FMatrix33 QCOMm = QCOM.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + Settings.DrawScale * Settings.BodyAxisLen * QCOMm.GetAxis(0), Settings.DrawScale * Settings.ArrowSize, Red, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + Settings.DrawScale * Settings.BodyAxisLen * QCOMm.GetAxis(1), Settings.DrawScale * Settings.ArrowSize, Green, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + Settings.DrawScale * Settings.BodyAxisLen * QCOMm.GetAxis(2), Settings.DrawScale * Settings.ArrowSize, Blue, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);

			FColor Black = FColor::Black;
			FColor Grey = (ColorScale * FColor(64, 64, 64)).ToFColor(false);
			FVec3 PActor = SpaceTransform.TransformPosition(FParticleUtilities::GetActorWorldTransform(Particle).GetTranslation());
			FDebugDrawQueue::GetInstance().DrawDebugPoint(PActor, Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.DrawScale * Settings.PointSize);
			FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PActor, Grey, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
		
			if (Index >= 0)
			{
				//FDebugDrawQueue::GetInstance().DrawDebugString(PCOM + FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("{0}{1}"), { Particle->IsKinematic()? TEXT("K"): TEXT("D"), Index }), nullptr, FColor::White, KINDA_SMALL_NUMBER, false, FontScale);
			}

			if ((Settings.VelScale > 0.0f) && (Particle->V().Size() > KINDA_SMALL_NUMBER))
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PCOM + SpaceTransform.TransformVector(Particle->V()) * Settings.VelScale, Red, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				//FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PCOM + SpaceTransform.TransformVector(Particle->VSmooth()) * Settings.VelScale, Blue, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
			if ((Settings.AngVelScale > 0.0f) && (Particle->W().Size() > KINDA_SMALL_NUMBER))
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PCOM + SpaceTransform.TransformVector(Particle->W()) * Settings.AngVelScale, Green, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
		}

		void DrawCollisionImpl(const FVec3& Location, const FVec3& Normal, FReal Phi, const FVec3& Impulse, const FColor& DiscColor, const FColor& NormalColor, const FColor& ImpulseColor, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
			if (Settings.ContactWidth > 0)
			{
				FColor C0 = (ColorScale * DiscColor).ToFColor(false);
				FDebugDrawQueue::GetInstance().DrawDebugCircle(Location, Settings.DrawScale * Settings.ContactWidth, 12, C0, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
			}
			if (Settings.ContactLen > 0)
			{
				FColor C1 = (ColorScale * NormalColor).ToFColor(false);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, Location + Settings.DrawScale * Settings.ContactLen * Normal, C1, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
			if (Settings.ContactPhiWidth > 0 && (Phi < FLT_MAX))
			{
				FColor C2 = (ColorScale * FColor(128, 128, 0)).ToFColor(false);
				FDebugDrawQueue::GetInstance().DrawDebugCircle(Location - Phi * Normal, Settings.DrawScale * Settings.ContactPhiWidth, 12, C2, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
			}
			if ((Settings.ImpulseScale > 0) && !Impulse.IsNearlyZero())
			{
				FColor C3 = (ColorScale * ImpulseColor).ToFColor(false);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, Location + Settings.DrawScale * Settings.ImpulseScale * Impulse, C3, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
		}

		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const FCollisionConstraintBase& Contact, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			if ((Settings.ContactWidth > 0) || (Settings.ContactLen > 0) || (Settings.ImpulseScale > 0.0f))
			{
				const FRigidBodyPointContactConstraint* PointConstraint = Contact.template As<FRigidBodyPointContactConstraint>();
				if (PointConstraint->GetManifoldPoints().Num() > 0)
				{
					FConstGenericParticleHandle Particle0 = Contact.Particle[0];
					FConstGenericParticleHandle Particle1 = Contact.Particle[1];
					const FRigidTransform3 WorldCoMTransform0 = FParticleUtilities::GetCoMWorldTransform(Particle0);
					const FRigidTransform3 WorldCoMTransform1 = FParticleUtilities::GetCoMWorldTransform(Particle1);

					for (const FManifoldPoint& ManifoldPoint : PointConstraint->GetManifoldPoints())
					{
						const bool bIsActive = ManifoldPoint.bActive || !ManifoldPoint.NetPushOut.IsNearlyZero();
						if (!bIsActive && !bChaosDebugDebugDrawInactiveContacts)
						{
							continue;
						}

						const int32 ContactPlaneOwner = ManifoldPoint.ContactPoint.ContactNormalOwnerIndex;
						const int32 ContactPointOwner = 1 - ContactPlaneOwner;
						const FRigidTransform3& PlaneTransform = (ContactPlaneOwner == 0) ? WorldCoMTransform0 : WorldCoMTransform1;
						const FRigidTransform3& PointTransform = (ContactPlaneOwner == 0) ? WorldCoMTransform1 : WorldCoMTransform0;
						FConstGenericParticleHandle PlaneParticle = (ContactPlaneOwner == 0) ? Particle0 : Particle1;
						const FVec3 PlaneNormal = PlaneTransform.TransformVector((ContactPlaneOwner == 1) ? ManifoldPoint.CoMContactNormal : -ManifoldPoint.CoMContactNormal);	// Normal is always points body 2->1 internally, but shows better if it points from plane owner to point owner
						const FVec3 PointLocation = PointTransform.TransformPosition(ManifoldPoint.CoMContactPoints[ContactPointOwner]) - ManifoldPoint.ContactPoint.ShapeMargins[ContactPointOwner] * PlaneNormal;
						const FVec3 PlaneLocation = PlaneTransform.TransformPosition(ManifoldPoint.CoMContactPoints[ContactPlaneOwner]) + ManifoldPoint.ContactPoint.ShapeMargins[ContactPlaneOwner] * PlaneNormal;
						const FVec3 PointPlaneLocation = PointLocation - FVec3::DotProduct(PointLocation - PlaneLocation, PlaneNormal) * PlaneNormal;

						// Dynamic friction, restitution = red
						// Static friction, no restitution = green
						// Inactive = gray
						FColor DiscColor = FColor(200, 0, 0);
						FColor NormalColor = FColor(200, 0, 0);
						FColor ImpulseColor = FColor(0, 0, 200);
						FColor PushOutColor = FColor(200, 200, 0);
						FColor PushOutImpusleColor = FColor(0, 200, 200);
						if (ManifoldPoint.bInsideStaticFrictionCone)
						{
							DiscColor = FColor(150, 200, 0);
						}
						if (!ManifoldPoint.bRestitutionEnabled)
						{
							NormalColor = FColor(150, 200, 0);
						}
						if (!bIsActive)
						{
							DiscColor = FColor(100, 100, 100);
							NormalColor = FColor(100, 100, 100);
						}

						const FVec3 WorldPointLocation = SpaceTransform.TransformPosition(PointLocation);
						const FVec3 WorldPlaneLocation = SpaceTransform.TransformPosition(PlaneLocation);
						const FVec3 WorldPointPlaneLocation = SpaceTransform.TransformPosition(PointPlaneLocation);
						const FVec3 WorldPlaneNormal = SpaceTransform.TransformVectorNoScale(PlaneNormal);

						// Pushout
						if ((Settings.ImpulseScale > 0) && !ManifoldPoint.NetPushOut.IsNearlyZero())
						{
							FColor Color = (ColorScale * PushOutImpusleColor).ToFColor(false);
							FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPointPlaneLocation, WorldPointPlaneLocation + Settings.DrawScale * ManifoldPoint.NetPushOut, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						}
						if ((Settings.ImpulseScale > 0) && !FMath::IsNearlyZero(ManifoldPoint.NetPushOutImpulseNormal))
						{
							FColor Color = (ColorScale * PushOutImpusleColor).ToFColor(false);
							FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPointPlaneLocation, WorldPointPlaneLocation + Settings.DrawScale * Settings.ImpulseScale * ManifoldPoint.NetPushOutImpulseNormal * WorldPlaneNormal, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						}
						if ((Settings.ImpulseScale > 0) && !FMath::IsNearlyZero(ManifoldPoint.NetPushOutImpulseTangent))
						{
							const FColor Color = (ColorScale * PushOutImpusleColor).ToFColor(false);
							const FVec3 Tangent = (ManifoldPoint.NetPushOut - FVec3::DotProduct(ManifoldPoint.NetPushOut, ManifoldPoint.ContactPoint.Normal) * ManifoldPoint.ContactPoint.Normal).GetSafeNormal();
							const FVec3 WorldTangent = SpaceTransform.TransformVectorNoScale(Tangent);
							FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPointPlaneLocation, WorldPointPlaneLocation + Settings.DrawScale * Settings.ImpulseScale * ManifoldPoint.NetPushOutImpulseTangent * WorldTangent, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						}

						// Manifold plane and normal
						DrawCollisionImpl(WorldPlaneLocation, WorldPlaneNormal, ManifoldPoint.ContactPoint.Phi, ManifoldPoint.NetImpulse, DiscColor, NormalColor, ImpulseColor, ColorScale, Settings);

						// Manifold point
						FMatrix Axes = FRotationMatrix::MakeFromX(WorldPlaneNormal);
						FDebugDrawQueue::GetInstance().DrawDebugCircle(WorldPointLocation, 0.5f * Settings.DrawScale * Settings.ContactWidth, 12, DiscColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
					}
				}
				else
				{
					const FVec3 Location = SpaceTransform.TransformPosition(Contact.GetLocation());
					const FVec3 Normal = SpaceTransform.TransformVector(Contact.GetNormal());
					DrawCollisionImpl(Location, Normal, Contact.GetPhi(), FVec3(0), FColor(200, 0, 0), FColor(200, 0, 0), FColor(200, 0, 0), ColorScale, Settings);
				}
			}
			if (Settings.ContactOwnerWidth > 0)
			{
				const FVec3 Location = SpaceTransform.TransformPosition(Contact.GetLocation());
				const FVec3 Normal = SpaceTransform.TransformVector(Contact.GetNormal());

				const FColor C3 = (ColorScale * FColor(128, 128, 128)).ToFColor(false);
				const FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
				const FVec3 P0 = SpaceTransform.TransformPosition(Contact.Particle[0]->X());
				const FVec3 P1 = SpaceTransform.TransformPosition(Contact.Particle[1]->X());
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, P0, C3, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness * 0.5f);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, P1, C3, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness * 0.5f);
			}
		}
		
		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraintHandle* ConstraintHandle, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			DrawCollisionImpl(SpaceTransform, ConstraintHandle->GetContact(), ColorScale, Settings);
		}

		void DrawCollidingShapesImpl(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, float ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			TArray<const FImplicitObject*> Shapes;
			TArray<FConstGenericParticleHandle> ShapeParticles;
			TArray<FRigidTransform3> ShapeTransforms;

			for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
			{
				const FRigidBodyPointContactConstraint* PointConstraint = Collisions.GetConstraint(ConstraintIndex).template As<FRigidBodyPointContactConstraint>();
				if (!PointConstraint->GetDisabled() && (PointConstraint->Manifold.Phi < TNumericLimits<FReal>::Max()))
				{
					const FImplicitObject* Implicit0 = PointConstraint->Manifold.Implicit[0];
					const FImplicitObject* Implicit1 = PointConstraint->Manifold.Implicit[1];
					if ((Implicit0 != nullptr) && (Implicit1 != nullptr))
					{
						FConstGenericParticleHandle Particle0 = PointConstraint->Particle[0];
						FConstGenericParticleHandle Particle1 = PointConstraint->Particle[1];
						const FRigidTransform3 WorldActorTransform0 = FParticleUtilities::GetActorWorldTransform(Particle0);
						const FRigidTransform3 WorldActorTransform1 = FParticleUtilities::GetActorWorldTransform(Particle1);
						const FRigidTransform3 ShapeWorldTransform0 = PointConstraint->ImplicitTransform[0] * WorldActorTransform0;
						const FRigidTransform3 ShapeWorldTransform1 = PointConstraint->ImplicitTransform[1] * WorldActorTransform1;

						if (!Shapes.Contains(Implicit0))
						{
							Shapes.Add(Implicit0);
							ShapeParticles.Add(Particle0);
							ShapeTransforms.Add(ShapeWorldTransform0);
						}

						if (!Shapes.Contains(Implicit1))
						{
							Shapes.Add(Implicit1);
							ShapeParticles.Add(Particle1);
							ShapeTransforms.Add(ShapeWorldTransform1);
						}
					}
				}
			}

			for (int32 ShapeIndex = 0; ShapeIndex < Shapes.Num(); ++ShapeIndex)
			{
				DrawShapesImpl(
					ShapeParticles[ShapeIndex]->Handle(), 
					ShapeTransforms[ShapeIndex], 
					Shapes[ShapeIndex], 
					0.0f, 
					ShapeParticles[ShapeIndex]->IsDynamic() ? FColor::Yellow : FColor::Red, 
					Settings);
			}
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FVec3& InPa, const FVec3& InCa, const FVec3& InXa, const FMatrix33& Ra, const FVec3& InPb, const FVec3& InCb, const FVec3& InXb, const FMatrix33& Rb, int32 IslandIndex, int32 LevelIndex, int32 ColorIndex, int32 BatchIndex, int32 Index, FReal ColorScale,  const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings& Settings)
		{
			using namespace Chaos::DebugDraw;
			FColor R = (ColorScale * FColor::Red).ToFColor(false);
			FColor G = (ColorScale * FColor::Green).ToFColor(false);
			FColor B = (ColorScale * FColor::Blue).ToFColor(false);
			FColor C = (ColorScale * FColor::Cyan).ToFColor(false);
			FColor M = (ColorScale * FColor::Magenta).ToFColor(false);
			FColor Y = (ColorScale * FColor::Yellow).ToFColor(false);
			FVec3 Pa = SpaceTransform.TransformPosition(InPa);
			FVec3 Pb = SpaceTransform.TransformPosition(InPb);
			FVec3 Ca = SpaceTransform.TransformPosition(InCa);
			FVec3 Cb = SpaceTransform.TransformPosition(InCb);
			FVec3 Xa = SpaceTransform.TransformPosition(InXa);
			FVec3 Xb = SpaceTransform.TransformPosition(InXb);

			if (FeatureMask.bActorConnector)
			{
				const FReal ConnectorThickness = 1.5f * Settings.LineThickness;
				const FReal CoMSize = Settings.DrawScale * Settings.JointComSize;
				// Leave a gap around the actor position so we can see where the center is
				FVec3 Sa = Pa;
				const FReal Lena = (Xa - Pa).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Pa, Xa, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FVec3 Sb = Pb;
				const FReal Lenb = (Xb - Pb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Pb, Xb, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pa, Sa, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pb, Sb, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
			}
			if (FeatureMask.bCoMConnector)
			{
				const FReal ConnectorThickness = 1.5f * Settings.LineThickness;
				const FReal CoMSize = Settings.DrawScale * Settings.JointComSize;
				// Leave a gap around the body position so we can see where the center is
				FVec3 Sa = Ca;
				const FReal Lena = (Xa - Ca).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Ca, Xa, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FVec3 Sb = Cb;
				const FReal Lenb = (Xb - Cb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Cb, Xb, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Ca, Sa, FColor::Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Cb, Sb, FColor::Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
			}
			if (FeatureMask.bStretch)
			{
				const FReal StretchThickness = 3.0f * Settings.LineThickness;
				FDebugDrawQueue::GetInstance().DrawDebugLine(Xa, Xb, M, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, StretchThickness);
			}
			if (FeatureMask.bAxes)
			{
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(0)), Settings.DrawScale * Settings.ArrowSize, R, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(1)), Settings.DrawScale * Settings.ArrowSize, G, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(2)), Settings.DrawScale * Settings.ArrowSize, B, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(0)), Settings.DrawScale * Settings.ArrowSize, C, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(1)), Settings.DrawScale * Settings.ArrowSize, M, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + Settings.DrawScale * Settings.ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(2)), Settings.DrawScale * Settings.ArrowSize, Y, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
			FVec3 TextPos = Xb;
			if (FeatureMask.bLevel && (LevelIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { LevelIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
			if (FeatureMask.bIndex && (Index >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { Index }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
			if (FeatureMask.bColor && (ColorIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { ColorIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
			if (FeatureMask.bBatch && (BatchIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { BatchIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
			if (FeatureMask.bIsland && (IslandIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { IslandIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FPBDJointConstraintHandle* ConstraintHandle, FReal ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings& Settings)
		{
			TVec2<FGeometryParticleHandle*> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
			auto RigidParticle0 = ConstrainedParticles[0]->CastToRigidParticle();
			auto RigidParticle1 = ConstrainedParticles[1]->CastToRigidParticle();
			if ((RigidParticle0 && RigidParticle0->ObjectState() == EObjectStateType::Dynamic) || (RigidParticle1 && RigidParticle1->ObjectState() == EObjectStateType::Dynamic))
			{
				FVec3 Pa = FParticleUtilities::GetActorWorldTransform(FConstGenericParticleHandle(ConstraintHandle->GetConstrainedParticles()[1])).GetTranslation();
				FVec3 Pb = FParticleUtilities::GetActorWorldTransform(FConstGenericParticleHandle(ConstraintHandle->GetConstrainedParticles()[0])).GetTranslation();
				FVec3 Ca = FParticleUtilities::GetCoMWorldPosition(FConstGenericParticleHandle(ConstraintHandle->GetConstrainedParticles()[1]));
				FVec3 Cb = FParticleUtilities::GetCoMWorldPosition(FConstGenericParticleHandle(ConstraintHandle->GetConstrainedParticles()[0]));
				FVec3 Xa, Xb;
				FMatrix33 Ra, Rb;
				ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb);
				DrawJointConstraintImpl(SpaceTransform, Pa, Ca, Xa, Ra, Pb, Cb, Xb, Rb, ConstraintHandle->GetConstraintIsland(), ConstraintHandle->GetConstraintLevel(), ConstraintHandle->GetConstraintColor(), ConstraintHandle->GetConstraintBatch(), ConstraintHandle->GetConstraintIndex(), ColorScale, FeatureMask, Settings);
			}
		}

		void DrawSimulationSpaceImpl(const FSimulationSpace& SimSpace, const FChaosDebugDrawSettings& Settings)
		{
			const FVec3 Pos = SimSpace.Transform.GetLocation();
			const FRotation3& Rot = SimSpace.Transform.GetRotation();
			const FMatrix33 Rotm = Rot.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + Settings.DrawScale * Settings.BodyAxisLen * Rotm.GetAxis(0), Settings.DrawScale * Settings.ArrowSize, FColor::Red, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + Settings.DrawScale * Settings.BodyAxisLen * Rotm.GetAxis(1), Settings.DrawScale * Settings.ArrowSize, FColor::Green, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + Settings.DrawScale * Settings.BodyAxisLen * Rotm.GetAxis(2), Settings.DrawScale * Settings.ArrowSize, FColor::Blue, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);

			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + Settings.VelScale * SimSpace.LinearVelocity, FColor::Cyan, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + Settings.AngVelScale * SimSpace.AngularVelocity, FColor::Cyan, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + 0.01f * Settings.VelScale * SimSpace.LinearAcceleration, FColor::Yellow, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + 0.01f * Settings.AngVelScale * SimSpace.AngularAcceleration, FColor::Orange, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
		}

		void DrawConstraintGraphImpl(const FRigidTransform3& SpaceTransform, const FPBDConstraintColor& GraphColor, const FChaosDebugDrawSettings& Settings)
		{
			static FColor IslandColors[] = 
			{
				FColor::Red,
				FColor::Orange,
				FColor::Yellow,
				FColor::Green,
				FColor::Blue,
				FColor::Magenta,
				FColor::White,
				FColor::Black
			};
			const int32 NumIslandColors = UE_ARRAY_COUNT(IslandColors);

			auto DrawGraphCollision = [&](const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraintHandle& CollisionConstraint,  int32 IslandIndex, int32 LevelIndex, int32 ColorIndex, int32 OrderIndex, const FChaosDebugDrawSettings& Settings)
			{
				FColor IslandColor = IslandColors[IslandIndex % NumIslandColors];

				const FRigidTransform3 Transform0 = FParticleUtilities::GetCoMWorldTransform(FConstGenericParticleHandle(CollisionConstraint.GetConstrainedParticles()[0])) * SpaceTransform;
				const FRigidTransform3 Transform1 = FParticleUtilities::GetCoMWorldTransform(FConstGenericParticleHandle(CollisionConstraint.GetConstrainedParticles()[1])) * SpaceTransform;

				FVec3 ContactPos = CollisionConstraint.GetContactLocation();
				if (CollisionConstraint.GetContact().GetType() == FCollisionConstraintBase::FType::SinglePoint)
				{
					const FRigidBodyPointContactConstraint* Constraint = CollisionConstraint.GetContact().template As<const FRigidBodyPointContactConstraint>();
					if ((Constraint != nullptr) && (Constraint->GetManifoldPoints().Num() > 0))
					{
						ContactPos = FVec3(0);
						for (const FManifoldPoint& ManifoldPoint : Constraint->GetManifoldPoints())
						{
							ContactPos += Transform0.TransformPositionNoScale(ManifoldPoint.CoMContactPoints[0]);
							ContactPos += Transform1.TransformPositionNoScale(ManifoldPoint.CoMContactPoints[1]);
						}
						ContactPos /= (FReal)(2 * Constraint->GetManifoldPoints().Num());
					}
				}


				FDebugDrawQueue::GetInstance().DrawDebugLine(Transform0.GetLocation(), ContactPos, FColor::Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Transform1.GetLocation(), ContactPos, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugString(ContactPos, FString::Format(TEXT("{0}-{1}-{2}"), { LevelIndex, ColorIndex, OrderIndex }), nullptr, FColor::Yellow, KINDA_SMALL_NUMBER, false, Settings.FontScale);

			};

			for (int32 IslandIndex = 0; IslandIndex < GraphColor.NumIslands(); ++IslandIndex)
			{
				FAABB3 IslandAABB = FAABB3::EmptyAABB();

				const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelColorConstraintMap = GraphColor.GetIslandLevelToColorToConstraintListMap(IslandIndex);
				int32 MaxColor = GraphColor.GetIslandMaxColor(IslandIndex);
				int32 MaxLevel = GraphColor.GetIslandMaxLevel(IslandIndex);
				for (int32 LevelIndex = 0; LevelIndex <= MaxLevel; ++LevelIndex)
				{
					for (int32 ColorIndex = 0; ColorIndex <= MaxColor; ++ColorIndex)
					{
						if (LevelColorConstraintMap[LevelIndex].Contains(ColorIndex) && LevelColorConstraintMap[LevelIndex][ColorIndex].Num())
						{
							const TArray<FConstraintHandle*>& ConstraintHandles = LevelColorConstraintMap[LevelIndex][ColorIndex];
							for (int32 HandleIndex = 0; HandleIndex < ConstraintHandles.Num(); ++HandleIndex)
							{
								const FConstraintHandle* ConstraintHandle = ConstraintHandles[HandleIndex];

								if (const FPBDCollisionConstraintHandle* CollisionHandle = ConstraintHandle->As<FPBDCollisionConstraintHandle>())
								{
									if (CollisionHandle->GetConstrainedParticles()[0]->HasBounds() && (CollisionHandle->GetConstrainedParticles()[0]->ObjectState() == EObjectStateType::Dynamic))
									{
										IslandAABB.GrowToInclude(CollisionHandle->GetConstrainedParticles()[0]->WorldSpaceInflatedBounds());
									}
									if (CollisionHandle->GetConstrainedParticles()[1]->HasBounds() && (CollisionHandle->GetConstrainedParticles()[1]->ObjectState() == EObjectStateType::Dynamic))
									{
										IslandAABB.GrowToInclude(CollisionHandle->GetConstrainedParticles()[1]->WorldSpaceInflatedBounds());
									}

									if (bChaosDebugDebugDrawContactGraph)
									{
										DrawGraphCollision(SpaceTransform, *CollisionHandle, IslandIndex, LevelIndex, ColorIndex, HandleIndex, Settings);
									}
								}
							}

						}
					}
				}

				FAABB3 Bounds = IslandAABB.TransformedAABB(SpaceTransform);
				FDebugDrawQueue::GetInstance().DrawDebugBox(Bounds.Center(), 0.5f * Bounds.Extents(), SpaceTransform.GetRotation(), IslandColors[IslandIndex % NumIslandColors], false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
		}


		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawParticleShapesImpl(SpaceTransform, Particle, Color, GetChaosDebugDrawSettings(Settings));
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const FGeometryParticle* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawParticleShapesImpl(SpaceTransform, Particle, Color, GetChaosDebugDrawSettings(Settings));
			}
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, BoundsThickness, BoundsThicknessVelocityInflation, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, BoundsThickness, BoundsThicknessVelocityInflation, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, BoundsThickness, BoundsThicknessVelocityInflation, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleCollisions(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* Particle, const FPBDCollisionConstraints& Collisions, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const Chaos::FPBDCollisionConstraintHandle * ConstraintHandle : Collisions.GetConstConstraintHandles())
				{
					TVec2<const FGeometryParticleHandle*> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
					if ((ConstrainedParticles[0] == Particle) || (ConstrainedParticles[1] == Particle))
					{
						DrawCollisionImpl(SpaceTransform, ConstraintHandle, 1.0f, GetChaosDebugDrawSettings(Settings));
					}
				}
			}
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
				{
					DrawCollisionImpl(SpaceTransform, Collisions.GetConstraint(ConstraintIndex), ColorScale, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const TArray<FPBDCollisionConstraintHandle*>& ConstraintHandles, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBDCollisionConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					DrawCollisionImpl(SpaceTransform, ConstraintHandle, ColorScale, GetChaosDebugDrawSettings(Settings));
				}
			}
		}


		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, Chaos::FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBDJointConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					DrawJointConstraintImpl(SpaceTransform, ConstraintHandle, ColorScale, FeatureMask, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, Chaos::FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					DrawJointConstraintImpl(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale, FeatureMask, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawSimulationSpace(const FSimulationSpace& SimSpace, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawSimulationSpaceImpl(SimSpace, GetChaosDebugDrawSettings(Settings));
			}
		}

		void DrawConstraintGraph(const FRigidTransform3& SpaceTransform, const FPBDConstraintColor& Graph, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawConstraintGraphImpl(SpaceTransform, Graph, GetChaosDebugDrawSettings(Settings));
			}
		}

		void DrawCollidingShapes(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, float ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawCollidingShapesImpl(SpaceTransform, Collisions, ColorScale, GetChaosDebugDrawSettings(Settings));
			}
		}

#endif
	}
}
