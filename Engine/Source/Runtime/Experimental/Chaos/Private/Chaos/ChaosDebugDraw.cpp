// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Collision/CollisionConstraintAllocator.h"
#include "Chaos/Collision/ParticlePairMidPhase.h"
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
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{

	extern bool bChaos_Collision_Manifold_FixNormalsInWorldSpace;

	namespace DebugDraw
	{
#if CHAOS_DEBUG_DRAW

		bool bChaosDebugDebugDrawShapeBounds = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawShapeBounds(TEXT("p.Chaos.DebugDraw.ShowShapeBounds"), bChaosDebugDebugDrawShapeBounds, TEXT("Whether to show the bounds of each shape in DrawShapes"));

		bool bChaosDebugDebugDrawCollisionParticles = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawShapeParticles(TEXT("p.Chaos.DebugDraw.ShowCollisionParticles"), bChaosDebugDebugDrawCollisionParticles, TEXT("Whether to show the collision particles if present"));

		bool bChaosDebugDebugDrawInactiveContacts = true;
		FAutoConsoleVariableRef CVarChaosDebugDrawInactiveContacts(TEXT("p.Chaos.DebugDraw.ShowInactiveContacts"), bChaosDebugDebugDrawInactiveContacts, TEXT("Whether to show inactive contacts (ones that contributed no impulses or pushout)"));

		bool bChaosDebugDebugDrawContactIterations = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawContactIterations(TEXT("p.Chaos.DebugDraw.ShowContactIterations"), bChaosDebugDebugDrawContactIterations, TEXT("Whether to show an indicator of how many iterations a contact was active for"));

		bool bChaosDebugDebugDrawColorShapesByShapeType = false;
		FAutoConsoleVariableRef CVarChaosDebugDebugDrawColorShapesByShapeType(TEXT("p.Chaos.DebugDraw.ColorShapesByShapeType"), bChaosDebugDebugDrawColorShapesByShapeType, TEXT("Whether to use shape type to define the color of the shapes instead of using the particle state "));

		bool bChaosDebugDebugDrawColorShapesByIsland = false;
		FAutoConsoleVariableRef CVarChaosDebugDebugDrawColorShapesByIsland(TEXT("p.Chaos.DebugDraw.ColorShapesByIsland"), bChaosDebugDebugDrawColorShapesByIsland, TEXT("Whether to use particle island to define the color of the shapes instead of using the particle state "));

		bool bChaosDebugDebugDrawColorBoundsByShapeType = false;
		FAutoConsoleVariableRef CVarChaosDebugDebugDrawColorBoundsByShapeType(TEXT("p.Chaos.DebugDraw.ColorBoundsByShapeType"), bChaosDebugDebugDrawColorBoundsByShapeType, TEXT("Whether to use shape type to define the color of the bounds instead of using the particle state (if multiple shapes , will use the first one)"));

		bool bChaosDebugDebugDrawConvexVertices = false;
		bool bChaosDebugDebugDrawCoreShapes = false;
		bool bChaosDebugDebugDrawExactCoreShapes = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawConvexVertices(TEXT("p.Chaos.DebugDraw.ShowConvexVertices"), bChaosDebugDebugDrawConvexVertices, TEXT("Whether to show the vertices of convex shapes"));
		FAutoConsoleVariableRef CVarChaosDebugDrawCoreShapes(TEXT("p.Chaos.DebugDraw.ShowCoreShapes"), bChaosDebugDebugDrawCoreShapes, TEXT("Whether to show the core (margin-reduced) shape where applicable"));
		FAutoConsoleVariableRef CVarChaosDebugDrawExactShapes(TEXT("p.Chaos.DebugDraw.ShowExactCoreShapes"), bChaosDebugDebugDrawExactCoreShapes, TEXT("Whether to show the exact core shape. NOTE: Extremely expensive and should only be used on a small scene with a couple convex shapes in it"));

		bool bChaosDebugDebugDrawIslands = true;
		FAutoConsoleVariableRef CVarChaosDebugDrawIslands(TEXT("p.Chaos.DebugDraw.ShowIslands"), bChaosDebugDebugDrawIslands, TEXT("Whether to show the iosland boxes when drawing islands (if you want only the contact graph)"));

		bool bChaosDebugDebugDrawContactGraph = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawContactGraph(TEXT("p.Chaos.DebugDraw.ShowContactGraph"), bChaosDebugDebugDrawContactGraph, TEXT("Whether to show the contactgraph when drawing islands"));

		bool bChaosDebugDebugDrawContactGraphUsed = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawContactGraphUsed(TEXT("p.Chaos.DebugDraw.ShowContactGraphUsed"), bChaosDebugDebugDrawContactGraphUsed, TEXT("Whether to show the used edges contactgraph when drawing islands (collisions with impulse)"));

		bool bChaosDebugDebugDrawContactGraphUnused = false;
		FAutoConsoleVariableRef CVarChaosDebugDrawContactGraphUnused(TEXT("p.Chaos.DebugDraw.ShowContactGraphUnused"), bChaosDebugDebugDrawContactGraphUnused, TEXT("Whether to show the unused edges contactgraph when drawing islands (collisions with no impulse)"));

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
			/* PushOutScale =				*/ 0.0f,
			/* InertiaScale =				*/ 0.0f,
			/* DrawPriority =				*/ 10,
			/* bShowSimple =				*/ true,
			/* bShowComplex =				*/ false,
			/* bInShowLevelSetCollision =	*/ false,
			/* InShapesColorsPerState =     */ GetDefaultShapesColorsByState(),
			/* InShapesColorsPerShaepType=  */ GetDefaultShapesColorsByShapeType(),
			/* InBoundsColorsPerState =     */ GetDefaultBoundsColorsByState(),
			/* InBoundsColorsPerShapeType=  */ GetDefaultBoundsColorsByShapeType()
		);

		const FChaosDebugDrawSettings& GetChaosDebugDrawSettings(const FChaosDebugDrawSettings* InSettings)
		{
			if (InSettings != nullptr)
			{
				return *InSettings;
			}

			return ChaosDefaultDebugDebugDrawSettings;
		}

		//-------------------------------------------------------------------------------------------------

		FChaosDebugDrawColorsByState::FChaosDebugDrawColorsByState(
			FColor InDynamicColor,
			FColor InSleepingColor,
			FColor InKinematicColor,
			FColor InStaticColor
		)
			: DynamicColor(InDynamicColor)
			, SleepingColor(InSleepingColor)
			, KinematicColor(InKinematicColor)
			, StaticColor(InStaticColor)
		{}

		FColor FChaosDebugDrawColorsByState::GetColorFromState(EObjectStateType State) const
		{
			switch (State)
			{
			case EObjectStateType::Sleeping:	return SleepingColor;
			case EObjectStateType::Kinematic:	return KinematicColor;
			case EObjectStateType::Static:		return StaticColor;
			case EObjectStateType::Dynamic:		return DynamicColor;
			default:							return FColor::Purple; // nice visible color :)
			}
		}

		const FChaosDebugDrawColorsByState& GetDefaultShapesColorsByState()
		{
			// default colors by state for shapes
			static FChaosDebugDrawColorsByState ChaosDefaultShapesColorsByState(
				/* InDynamicColor =	  */ FColor(255, 255, 0),
				/* InSleepingColor =  */ FColor(128, 128, 128),
				/* InKinematicColor = */ FColor(0, 128, 255),
				/* InStaticColor =	  */ FColor(255, 0, 0)
			);

			return ChaosDefaultShapesColorsByState;
		}

		const FChaosDebugDrawColorsByState& GetDefaultBoundsColorsByState()
		{
			// default colors by state for bounds ( darker version of the shapes colors - see above )
			static FChaosDebugDrawColorsByState ChaosDefaultBoundsColorsByState(
				/* InDynamicColor =	  */ FColor(128, 128, 0),
				/* InSleepingColor =  */ FColor(64, 64, 64),
				/* InKinematicColor = */ FColor(0, 64, 128),
				/* InStaticColor =	  */ FColor(128, 0, 0)
			);

			return ChaosDefaultBoundsColorsByState;
		}

		FColor GetIslandColor(const int32 IslandIndex, const bool bIsAwake)
		{
			static FColor AwakeColors[] =
			{
				FColor::Red,
				FColor::Orange,
				FColor::Yellow,
				FColor::Green,
				FColor::Blue,
				FColor::Magenta,
			};
			const int32 NumAwakeColors = UE_ARRAY_COUNT(AwakeColors);
			static FColor SleepingColor = FColor::Black;

			return bIsAwake ? AwakeColors[IslandIndex % NumAwakeColors] : SleepingColor;
		};


		//-------------------------------------------------------------------------------------------------

		FChaosDebugDrawColorsByShapeType::FChaosDebugDrawColorsByShapeType(
			FColor InSimpleTypeColor,
			FColor InConvexColor,
			FColor InHeightFieldColor,
			FColor InTriangleMeshColor,
			FColor InLevelSetColor
		)
			: SimpleTypeColor(InSimpleTypeColor)
			, ConvexColor(InConvexColor)
			, HeightFieldColor(InHeightFieldColor)
			, TriangleMeshColor(InTriangleMeshColor)
			, LevelSetColor(InLevelSetColor)
		{}

		FColor FChaosDebugDrawColorsByShapeType::GetColorFromShapeType(EImplicitObjectType ShapeType) const
		{
			switch(ShapeType)
			{
			case ImplicitObjectType::Sphere:			return SimpleTypeColor;
			case ImplicitObjectType::Box:				return SimpleTypeColor;
			case ImplicitObjectType::Plane:				return SimpleTypeColor;
			case ImplicitObjectType::Capsule:			return SimpleTypeColor;
			case ImplicitObjectType::TaperedCylinder:	return SimpleTypeColor;
			case ImplicitObjectType::Cylinder:			return SimpleTypeColor;
			case ImplicitObjectType::Convex:			return ConvexColor;
			case ImplicitObjectType::HeightField:		return HeightFieldColor;
			case ImplicitObjectType::TriangleMesh:		return TriangleMeshColor;
			case ImplicitObjectType::LevelSet:			return LevelSetColor;			
			default:									return FColor::Purple; // nice visible color :)
			};
		}

		CHAOS_API const FChaosDebugDrawColorsByShapeType& GetDefaultShapesColorsByShapeType()
		{
			// default colors by shaep type for shapes
			static FChaosDebugDrawColorsByShapeType ChaosDefaultShapesColorsByShapeType(
				/* InSimpleTypeColor,   */ FColor(0, 255, 0),
				/* InConvexColor,		*/ FColor(0, 255, 255),
				/* InHeightFieldColor,	*/ FColor(0, 0, 255),
				/* InTriangleMeshColor,	*/ FColor(255, 0, 0),
				/* InLevelSetColor		*/ FColor(255, 0, 128)
			);

			return ChaosDefaultShapesColorsByShapeType;
		}

		CHAOS_API const FChaosDebugDrawColorsByShapeType& GetDefaultBoundsColorsByShapeType()
		{
			// default colors by shape type for bounds ( darker version of the shapes colors - see above )
			static FChaosDebugDrawColorsByShapeType ChaosDefaultBoundsColorsByShapeType(
				/* InSimpleTypeColor,   */ FColor(0, 128, 0),
				/* InConvexColor,		*/ FColor(0, 128, 128),
				/* InHeightFieldColor,	*/ FColor(0, 0, 128),
				/* InTriangleMeshColor,	*/ FColor(128, 0, 0),
				/* InLevelSetColor		*/ FColor(128, 0, 64)
			);

			return ChaosDefaultBoundsColorsByShapeType;
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
							const FRealSingle LineThickness = 0.5f * Settings.ShapeThicknesScale * Settings.LineThickness;
							const FVec3 InnerP0 = ShapeTransform.TransformPositionNoScale(Shape->GetMarginAdjustedVertexScaled(VertexIndex0, Margin, ShapeTransform.GetScale3D(), nullptr));
							const FVec3 InnerP1 = ShapeTransform.TransformPositionNoScale(Shape->GetMarginAdjustedVertexScaled(VertexIndex1, Margin, ShapeTransform.GetScale3D(), nullptr));
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
			{
				const TImplicitObjectScaled<FLevelSet, bInstanced>* Scaled = Shape->template GetObject<TImplicitObjectScaled<FLevelSet, bInstanced>>();
				// even though thhe levelset is scaled, the debugdraw uses the collisionParticles  that are pre-scaled
				// so no need to pass the scaled transform and just extract the wrapped LevelSet
				DrawShapesImpl(Particle, ShapeTransform, Scaled->GetUnscaledObject(), Scaled->GetMargin(), Color, Settings);
				break;
			}
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

			// Are we within the region of interest?
			const FReal ParticleSize = Particle->HasBounds() ? 0.5f * Particle->LocalBounds().Extents().Size() : TNumericLimits<FReal>::Max();
			if (!FDebugDrawQueue::GetInstance().IsInRegionOfInterest(ShapeTransform.GetLocation(), ParticleSize))
			{
				return;
			}

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

			FColor ShapeColor = Color;
			if (bChaosDebugDebugDrawColorShapesByShapeType)
			{
				ShapeColor = Settings.ShapesColorsPerShapeType.GetColorFromShapeType(InnerType);
			}
			if (bChaosDebugDebugDrawColorShapesByIsland && (FConstGenericParticleHandle(Particle)->IslandIndex() != INDEX_NONE))
			{
				ShapeColor = GetIslandColor(FConstGenericParticleHandle(Particle)->IslandIndex(), true);
			}

			// If we get here, we have an actual shape to render
			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
			{
				const TSphere<FReal, 3>* Sphere = Shape->template GetObject<TSphere<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Sphere->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugSphere(P, Sphere->GetRadius(), 8, ShapeColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::Box:
			{
				const TBox<FReal, 3>* Box = Shape->template GetObject<TBox<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Box->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugBox(P, (FReal)0.5 * Box->Extents(), ShapeTransform.GetRotation(), ShapeColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
			{
				const FCapsule* Capsule = Shape->template GetObject<FCapsule>();
				const FVec3 P = ShapeTransform.TransformPosition(Capsule->GetCenter());
				const FRotation3 Q = ShapeTransform.GetRotation() * FRotationMatrix::MakeFromZ(Capsule->GetAxis());
				FDebugDrawQueue::GetInstance().DrawDebugCapsule(P, (FReal)0.5 * Capsule->GetHeight() + Capsule->GetRadius(), Capsule->GetRadius(), Q, ShapeColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.ShapeThicknesScale * Settings.LineThickness);
				break;
			}
			case ImplicitObjectType::LevelSet:
			{
				const FLevelSet* LevelSet = Shape->template GetObject<FLevelSet>();
				DrawShapesLevelSetImpl(Particle, ShapeTransform, LevelSet, ShapeColor, Settings);
				break;
			}
			break;
			case ImplicitObjectType::Convex:
			{
				const FConvex* Convex = Shape->template GetObject<FConvex>();

				const FReal NetMargin = bChaosDebugDebugDrawCoreShapes ? Margin + Convex->GetMargin() : 0.0f;
				DrawShapesConvexImpl(Particle, ShapeTransform, Convex, NetMargin, ShapeColor, Settings);

				// Generate the exact marging-reduced convex for comparison with the runtime approximation
				// Warning: extremely expensive!
				if (bChaosDebugDebugDrawExactCoreShapes)
				{
					TArray<FConvex::FVec3Type> ScaledVerts(Convex->GetVertices());
					FConvex::FVec3Type Scale(ShapeTransform.GetScale3D());
					for (FConvex::FVec3Type& Vert : ScaledVerts)
					{
						Vert *= Scale;
					}
					FConvex ShrunkScaledConvex(ScaledVerts, FReal(0));
					ShrunkScaledConvex.MovePlanesAndRebuild(FConvex::FRealType(-Margin)); // potential loss of precision but margin should remain within the float range

					const FRigidTransform3 ShrunkScaledTransform = FRigidTransform3(ShapeTransform.GetTranslation(), ShapeTransform.GetRotation());
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
				DrawShapesTriangleMeshImpl(Particle, ShapeTransform, TriangleMesh, ShapeColor, Settings);
				break;
			}
			case ImplicitObjectType::HeightField:
			{
				const FHeightField* HeightField = Shape->template GetObject<FHeightField>();
				DrawShapesHeightFieldImpl(Particle, ShapeTransform, HeightField, ShapeColor, Settings);
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
							FDebugDrawQueue::GetInstance().DrawDebugPoint(P, ShapeColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.PointSize);
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

			DrawShapesImpl(Particle, FRigidTransform3(P, Q), Particle->Geometry().Get(), 0.0f, InColor, Settings);
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticle* Particle, const FColor& InColor, const FChaosDebugDrawSettings& Settings)
		{
			FVec3 P = SpaceTransform.TransformPosition(Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->R());

			DrawShapesImpl(Particle->Handle(), FRigidTransform3(P, Q), Particle->Geometry().Get(), 0.0f, InColor, Settings);
		}

		static EImplicitObjectType GetFirstConcreteShapeType(const FImplicitObject* Shape)
		{
			EImplicitObjectType InnerType = GetInnerType(Shape->GetType());
			if (InnerType == ImplicitObjectType::Union)
			{
				const FImplicitObjectUnion* Union = Shape->template GetObject<FImplicitObjectUnion>();
				for (auto& UnionShape : Union->GetObjects())
				{
					// use the first as reference as we can only display one color for the bounds
					return GetFirstConcreteShapeType(UnionShape.Get());
				}
			}
			else if (InnerType == ImplicitObjectType::Transformed)
			{
				const TImplicitObjectTransformed<FReal, 3>* Transformed = Shape->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
				return GetFirstConcreteShapeType(Transformed->GetTransformedObject());
			}
			else if (InnerType == ImplicitObjectType::UnionClustered)
			{
				const FImplicitObjectUnionClustered* Union = Shape->template GetObject<FImplicitObjectUnionClustered>();
				for (auto& UnionShape : Union->GetObjects())
				{
					// use the first as reference as we can only display one color for the bounds
					return GetFirstConcreteShapeType(UnionShape.Get());
				}
			}
			return InnerType;
		}

		void DrawParticleBoundsImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* InParticle, const FReal Dt, const FChaosDebugDrawSettings& Settings)
		{
			FConstGenericParticleHandle Particle = InParticle;

			const FAABB3 Box = InParticle->WorldSpaceInflatedBounds();

			const FVec3 P = SpaceTransform.TransformPosition(Box.GetCenter());
			const FRotation3 Q = SpaceTransform.GetRotation();
			FColor Color = Settings.BoundsColorsPerState.GetColorFromState(InParticle->ObjectState());
			if (bChaosDebugDebugDrawColorBoundsByShapeType)
			{
				if (const FImplicitObject* Shape = Particle->Geometry().Get())
				{
					Color = Settings.BoundsColorsPerShapeType.GetColorFromShapeType(GetFirstConcreteShapeType(Shape));
				}
			}

			FDebugDrawQueue::GetInstance().DrawDebugBox(P, 0.5f * Box.Extents(), Q, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);

			for (const auto& Shape : InParticle->ShapesArray())
			{
				const EImplicitObjectType ShapeType = GetFirstConcreteShapeType(Shape->GetGeometry().Get());
				const bool bIsComplex = (ShapeType == ImplicitObjectType::TriangleMesh) || (ShapeType == ImplicitObjectType::HeightField);
				if (!bIsComplex)
				{
					const FAABB3 ShapeBox = Shape->GetWorldSpaceInflatedShapeBounds();
					const FVec3 ShapeP = SpaceTransform.TransformPosition(ShapeBox.GetCenter());
					const FRotation3 ShapeQ = SpaceTransform.GetRotation();
					const FColor ShapeColor = (bChaosDebugDebugDrawColorBoundsByShapeType) ? Settings.BoundsColorsPerShapeType.GetColorFromShapeType(ShapeType) : Color;

					FDebugDrawQueue::GetInstance().DrawDebugBox(ShapeP, 0.5f * ShapeBox.Extents(), ShapeQ, ShapeColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				}
			}
		}

		void DrawParticleTransformImpl(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* InParticle, int32 Index, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
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

			if (Settings.InertiaScale > 0.0f)
			{
				if (const TPBDRigidParticleHandle<FReal, 3>* Rigid = InParticle->CastToRigidParticle())
				{
					const FVec3 EquivalentBoxSize = Utilities::BoxSizeFromInertia(Rigid->I().GetDiagonal(), Rigid->M());
					FDebugDrawQueue::GetInstance().DrawDebugBox(PCOM, 0.5f * Settings.InertiaScale * EquivalentBoxSize, QCOM, FColor::Magenta, false, 0.0f, 0, Settings.LineThickness);
				}
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

		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraint& Contact, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			if ((Settings.ContactWidth > 0) || (Settings.ContactLen > 0) || (Settings.ImpulseScale > 0.0f))
			{
				if (Contact.GetUseManifold())
				{
					const FConstGenericParticleHandle Particle0 = Contact.Particle[0];
					const FConstGenericParticleHandle Particle1 = Contact.Particle[1];
					const FRigidTransform3 WorldActorTransform0 = FParticleUtilities::GetActorWorldTransform(Particle0);
					const FRigidTransform3 WorldActorTransform1 = FParticleUtilities::GetActorWorldTransform(Particle1);

					// Are we within the region of interest?
					const FReal Particle0Size = Contact.Particle[0]->HasBounds() ? 0.5f * Contact.Particle[0]->LocalBounds().Extents().Size() : TNumericLimits<FReal>::Max();
					const FReal Particle1Size = Contact.Particle[1]->HasBounds() ? 0.5f * Contact.Particle[1]->LocalBounds().Extents().Size() : TNumericLimits<FReal>::Max();
					if (!FDebugDrawQueue::GetInstance().IsInRegionOfInterest(WorldActorTransform0.GetLocation(), Particle0Size))
					{
						return;
					}
					if (!FDebugDrawQueue::GetInstance().IsInRegionOfInterest(WorldActorTransform1.GetLocation(), Particle1Size))
					{
						return;
					}

					for (const FManifoldPoint& ManifoldPoint : Contact.GetManifoldPoints())
					{
						const bool bIsActive = !ManifoldPoint.NetPushOut.IsNearlyZero() || !ManifoldPoint.NetImpulse.IsNearlyZero();
						if (!bIsActive && !bChaosDebugDebugDrawInactiveContacts)
						{
							continue;
						}

						const int32 ContactPlaneOwner = 1;
						const int32 ContactPointOwner = 1 - ContactPlaneOwner;
						const FRigidTransform3& PlaneTransform = (ContactPlaneOwner == 0) ? Contact.ImplicitTransform[0] * WorldActorTransform0 : Contact.ImplicitTransform[1] * WorldActorTransform1;
						const FRigidTransform3& PointTransform = (ContactPlaneOwner == 0) ? Contact.ImplicitTransform[1] * WorldActorTransform1 : Contact.ImplicitTransform[0] * WorldActorTransform0;
						const FConstGenericParticleHandle PlaneParticle = (ContactPlaneOwner == 0) ? Particle0 : Particle1;
						const FVec3 PlaneNormal = ManifoldPoint.ContactPoint.Normal;
						const FVec3 PointLocation = PointTransform.TransformPosition(ManifoldPoint.ContactPoint.ShapeContactPoints[ContactPointOwner]);
						const FVec3 PlaneLocation = PlaneTransform.TransformPosition(ManifoldPoint.ContactPoint.ShapeContactPoints[ContactPlaneOwner]);
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
						if ((Settings.PushOutScale > 0) && !ManifoldPoint.NetPushOut.IsNearlyZero())
						{
							FColor Color = (ColorScale * PushOutImpusleColor).ToFColor(false);
							FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPointPlaneLocation, WorldPointPlaneLocation + Settings.DrawScale * Settings.PushOutScale * ManifoldPoint.NetPushOut, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						}
						if ((Settings.ImpulseScale > 0) && !ManifoldPoint.NetImpulse.IsNearlyZero())
						{
							FColor Color = (ColorScale * PushOutImpusleColor).ToFColor(false);
							FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPointPlaneLocation, WorldPointPlaneLocation + Settings.DrawScale * Settings.ImpulseScale * ManifoldPoint.NetImpulse, Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						}

						// Manifold plane and normal
						DrawCollisionImpl(WorldPlaneLocation, WorldPlaneNormal, ManifoldPoint.ContactPoint.Phi, ManifoldPoint.NetImpulse, DiscColor, NormalColor, ImpulseColor, ColorScale, Settings);

						// Manifold point
						FMatrix Axes = FRotationMatrix::MakeFromX(WorldPlaneNormal);
						FDebugDrawQueue::GetInstance().DrawDebugCircle(WorldPointLocation, 0.5f * Settings.DrawScale * Settings.ContactWidth, 12, DiscColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);

						// Previous points
						const FVec3 WorldPrevPointLocation = SpaceTransform.TransformPosition(ManifoldPoint.WorldContactPoints[ContactPointOwner]);
						const FVec3 WorldPrevPlaneLocation = SpaceTransform.TransformPosition(ManifoldPoint.WorldContactPoints[ContactPlaneOwner]);
						FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPrevPointLocation, WorldPointLocation, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
						FDebugDrawQueue::GetInstance().DrawDebugLine(WorldPrevPlaneLocation, WorldPlaneLocation, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);

						// Whether restored
						if (Contact.WasManifoldRestored())
						{
							const FReal BoxScale = Settings.DrawScale * Settings.ContactWidth;
							FDebugDrawQueue::GetInstance().DrawDebugBox(WorldPlaneLocation, FVec3(BoxScale, BoxScale, FReal(0.01)), FRotation3(FRotationMatrix::MakeFromZ(WorldPlaneNormal)), FColor::Blue, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, 0.5f * Settings.LineThickness);
						}
						else if (ManifoldPoint.bWasRestored)
						{
							const FReal BoxScale = Settings.DrawScale * Settings.ContactWidth;
							FDebugDrawQueue::GetInstance().DrawDebugBox(WorldPlaneLocation, FVec3(BoxScale, BoxScale, FReal(0.01)), FRotation3(FRotationMatrix::MakeFromZ(WorldPlaneNormal)), FColor::Purple, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, 0.5f * Settings.LineThickness);
						}
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

		void DrawCollidingShapesImpl(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, FRealSingle ColorScale, const FChaosDebugDrawSettings& Settings)
		{
			TArray<const FImplicitObject*> Shapes;
			TArray<FConstGenericParticleHandle> ShapeParticles;
			TArray<FRigidTransform3> ShapeTransforms;

			for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
			{
				const FPBDCollisionConstraint* PointConstraint = &Collisions.GetConstraint(ConstraintIndex);
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

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FVec3& InPa, const FVec3& InCa, const FVec3& InXa, const FMatrix33& Ra, const FVec3& InPb, const FVec3& InCb, const FVec3& InXb, const FMatrix33& Rb, int32 IslandIndex, int32 LevelIndex, int32 ColorIndex, int32 Index, Chaos::FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings& Settings)
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
				const FRealSingle ConnectorThickness = 1.5f * Settings.LineThickness;
				const FReal CoMSize = Settings.DrawScale * Settings.JointComSize;
				// Leave a gap around the actor position so we can see where the center is
				FVec3 Sa = Pa;
				const FReal Lena = (Xa - Pa).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Pa, Xa, FMath::Clamp<FReal>(CoMSize / Lena, 0., 1.));
				}
				FVec3 Sb = Pb;
				const FReal Lenb = (Xb - Pb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Pb, Xb, FMath::Clamp<FReal>(CoMSize / Lena, 0., 1.));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pa, Sa, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pb, Sb, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
			}
			if (FeatureMask.bCoMConnector)
			{
				const FRealSingle ConnectorThickness = 1.5f * Settings.LineThickness;
				const FReal CoMSize = Settings.DrawScale * Settings.JointComSize;
				// Leave a gap around the body position so we can see where the center is
				FVec3 Sa = Ca;
				const FReal Lena = (Xa - Ca).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Ca, Xa, FMath::Clamp<FReal>(CoMSize / Lena, 0., 1.));
				}
				FVec3 Sb = Cb;
				const FReal Lenb = (Xb - Cb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Cb, Xb, FMath::Clamp<FReal>(CoMSize / Lena, 0., 1.));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Ca, Sa, FColor::Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Cb, Sb, FColor::Black, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, ConnectorThickness);
			}
			if (FeatureMask.bStretch)
			{
				const FRealSingle StretchThickness = 3.0f * Settings.LineThickness;
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
			if (FeatureMask.bIsland && (IslandIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { IslandIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				TextPos += Settings.FontHeight * FVec3(0, 0, 1);
			}
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FPBDJointConstraintHandle* ConstraintHandle, Chaos::FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask, const FChaosDebugDrawSettings& Settings)
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
				DrawJointConstraintImpl(SpaceTransform, Pa, Ca, Xa, Ra, Pb, Cb, Xb, Rb, ConstraintHandle->GetConstraintIsland(), ConstraintHandle->GetConstraintLevel(), ConstraintHandle->GetConstraintColor(), ConstraintHandle->GetConstraintIndex(), ColorScale, FeatureMask, Settings);
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

		void DrawConstraintGraphImpl(const FRigidTransform3& SpaceTransform, const FPBDConstraintGraph& Graph, const FChaosDebugDrawSettings& Settings)
		{
			auto DrawGraphCollision = [&](const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraint* Constraint,  int32 IslandIndex, int32 LevelIndex, int32 ColorIndex, int32 OrderIndex, bool bIsUsed, const FChaosDebugDrawSettings& Settings)
			{
				FVec3 ContactPos = Constraint->GetContactLocation();
				if (Constraint->GetManifoldPoints().Num() > 0)
				{
					ContactPos = FVec3(0);
					for (const FManifoldPoint& ManifoldPoint : Constraint->GetManifoldPoints())
					{
						ContactPos += SpaceTransform.TransformPosition(ManifoldPoint.WorldContactPoints[0]);
						ContactPos += SpaceTransform.TransformPosition(ManifoldPoint.WorldContactPoints[1]);
					}
					ContactPos /= (FReal)(2 * Constraint->GetManifoldPoints().Num());
				}

				const FRigidTransform3 Transform0 = FParticleUtilities::GetCoMWorldTransform(FConstGenericParticleHandle(Constraint->GetConstrainedParticles()[0])) * SpaceTransform;
				const FRigidTransform3 Transform1 = FParticleUtilities::GetCoMWorldTransform(FConstGenericParticleHandle(Constraint->GetConstrainedParticles()[1])) * SpaceTransform;

				if ((bChaosDebugDebugDrawContactGraphUsed && bIsUsed) || (bChaosDebugDebugDrawContactGraphUnused && !bIsUsed))
				{
					FColor Color = bIsUsed ? FColor::Green : FColor::Red;
					FDebugDrawQueue::GetInstance().DrawDebugLine(Transform0.GetLocation(), Transform1.GetLocation(), Color, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
				}

				if (bChaosDebugDebugDrawContactGraph)
				{
					FDebugDrawQueue::GetInstance().DrawDebugLine(Transform0.GetLocation(), ContactPos, FColor::Red, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
					FDebugDrawQueue::GetInstance().DrawDebugLine(Transform1.GetLocation(), ContactPos, FColor::White, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
					FDebugDrawQueue::GetInstance().DrawDebugString(ContactPos, FString::Format(TEXT("{0}-{1}-{2}"), { LevelIndex, ColorIndex, OrderIndex }), nullptr, FColor::Yellow, KINDA_SMALL_NUMBER, false, Settings.FontScale);
				}
			};

			for (int32 IslandIndex = 0; IslandIndex < Graph.NumIslands(); ++IslandIndex)
			{
				const FPBDIslandSolver* Island = Graph.GetSolverIsland(IslandIndex);

				FAABB3 IslandAABB = FAABB3::EmptyAABB();
				const TArray<FGeometryParticleHandle*> Particles = Island->GetParticles();
				for (const FGeometryParticleHandle* GeoParticle : Particles)
				{
					FConstGenericParticleHandle Particle = GeoParticle;
					if (Particle->IsDynamic() && Particle->HasBounds())
					{
						IslandAABB.GrowToInclude(Particle->BoundingBox());
					}
				}

				if (bChaosDebugDebugDrawIslands)
				{
					const FColor IslandColor = GetIslandColor(IslandIndex, !Island->IsSleeping());
					const FAABB3 Bounds = IslandAABB.TransformedAABB(SpaceTransform);
					FDebugDrawQueue::GetInstance().DrawDebugBox(Bounds.Center(), 0.5f * Bounds.Extents(), SpaceTransform.GetRotation(), IslandColor, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, 3.0f * Settings.LineThickness);
				}

				if (bChaosDebugDebugDrawContactGraph || bChaosDebugDebugDrawContactGraphUnused || bChaosDebugDebugDrawContactGraphUsed)
				{
					const auto& Constraints = Island->GetConstraints();
					for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
					{
						const FConstraintHandle* Constraint = Constraints[ConstraintIndex];

						if (const FPBDCollisionConstraintHandle* Collision = Constraint->As<FPBDCollisionConstraintHandle>())
						{
							// @chaos(todo): store level and color in the constraint? Only for debug draw...
							const int32 LevelIndex = INDEX_NONE;
							const int32 ColorIndex = INDEX_NONE;
							const bool bIsUsed = !Collision->GetConstraint()->AccumulatedImpulse.IsNearlyZero();
							DrawGraphCollision(SpaceTransform, Collision->GetConstraint(), IslandIndex, LevelIndex, ColorIndex, ConstraintIndex, bIsUsed, Settings);
						}
					}
				}
			}
		}

		void DrawSuspensionConstraintsImpl(const FRigidTransform3& SpaceTransform, const FPBDSuspensionConstraints& Constraints, int32 ConstraintIndex, const FChaosDebugDrawSettings& Settings)
		{
			FConstGenericParticleHandle Particle = Constraints.GetConstrainedParticles(ConstraintIndex)[0];
			const FPBDSuspensionSettings& ConstraintSettings = Constraints.GetSettings(ConstraintIndex);
			const FPBDSuspensionResults& ConstraintResults = Constraints.GetResults(ConstraintIndex);
			const FVec3& PLocal = Constraints.GetConstraintPosition(ConstraintIndex);
			const FRigidTransform3 ParticleTransform = FParticleUtilitiesPQ::GetActorWorldTransform(Particle);

			const FVec3 PWorld = ParticleTransform.TransformPosition(PLocal);
			const FVec3 AxisWorld = ParticleTransform.TransformVector(ConstraintSettings.Axis);
			const FReal AxisLen = ConstraintResults.Length;
			const FVec3 PushOutWorld = ConstraintResults.NetPushOut;

			FDebugDrawQueue::GetInstance().DrawDebugLine(
				SpaceTransform.TransformPosition(PWorld), 
				SpaceTransform.TransformPosition(PWorld + AxisLen * AxisWorld), 
				FColor::Green, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);

			if (Settings.PushOutScale > 0)
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(
					SpaceTransform.TransformPosition(PWorld),
					SpaceTransform.TransformPosition(PWorld + Settings.PushOutScale * PushOutWorld),
					FColor::Blue, false, KINDA_SMALL_NUMBER, Settings.DrawPriority, Settings.LineThickness);
			}
		}


		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, FReal ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				const FChaosDebugDrawSettings& DebugDrawSettings = GetChaosDebugDrawSettings(Settings);
				for (auto& Particle : ParticlesView)
				{				
					FColor Color = ((float)ColorScale * DebugDrawSettings.ShapesColorsPerState.GetColorFromState(Particle.ObjectState())).ToFColor(false);
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, DebugDrawSettings);
				}
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, FReal ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				const FChaosDebugDrawSettings& DebugDrawSettings = GetChaosDebugDrawSettings(Settings);
				for (auto& Particle : ParticlesView)
				{
					FColor Color = ((float)ColorScale * DebugDrawSettings.ShapesColorsPerState.GetColorFromState(Particle.ObjectState())).ToFColor(false);
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, DebugDrawSettings);
				}
			}
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, FReal ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				const FChaosDebugDrawSettings& DebugDrawSettings = GetChaosDebugDrawSettings(Settings);
				for (auto& Particle : ParticlesView)
				{
					FColor Color = ((float)ColorScale * DebugDrawSettings.ShapesColorsPerState.GetColorFromState(Particle.ObjectState())).ToFColor(false);
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color, DebugDrawSettings);
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

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FReal Dt, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FReal Dt, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FReal Dt, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Dt, GetChaosDebugDrawSettings(Settings));
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

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const FCollisionConstraintAllocator& CollisionAllocator, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				CollisionAllocator.VisitCollisions(
					[&](const FPBDCollisionConstraint* Collision)
					{
						DrawCollisionImpl(SpaceTransform, Collision, ColorScale, GetChaosDebugDrawSettings(Settings));
					});
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

		void DrawConstraintGraph(const FRigidTransform3& SpaceTransform, const FPBDConstraintGraph& Graph, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawConstraintGraphImpl(SpaceTransform, Graph, GetChaosDebugDrawSettings(Settings));
			}
		}

		void DrawCollidingShapes(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawCollidingShapesImpl(SpaceTransform, Collisions, ColorScale, GetChaosDebugDrawSettings(Settings));
			}
		}

		class FSpatialDebugDrawInterface: public ISpatialDebugDrawInterface
		{
		public:
			FSpatialDebugDrawInterface(const FChaosDebugDrawSettings& InSettings)
				: Settings(InSettings)
			{}
			
			virtual ~FSpatialDebugDrawInterface() override = default;

			virtual void Box(const FAABB3& InBox, const TVec3<FReal>& InLinearColor, float InThickness) override
			{
				FDebugDrawQueue::GetInstance().DrawDebugBox(InBox.Center(), InBox.Extents() * FReal(0.5), FQuat::Identity, FLinearColor(InLinearColor).ToFColor(false), false, -1.f, Settings.DrawPriority, InThickness);
			}

			virtual void Line(const TVec3<FReal>& InBegin, const TVec3<FReal>& InEnd, const TVec3<FReal>& InLinearColor, float InThickness) override
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(InBegin, InEnd, FLinearColor(InLinearColor).ToFColor(false), false, -1.f, Settings.DrawPriority, InThickness);
			}
		private:
			FChaosDebugDrawSettings Settings;
		};

		void DrawSpatialAccelerationStructure(const ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>& InSpatialAccelerationStructure, const FChaosDebugDrawSettings* InSettings)
		{
		#if !UE_BUILD_SHIPPING
			FSpatialDebugDrawInterface DebugDrawInterface(GetChaosDebugDrawSettings(InSettings));
			InSpatialAccelerationStructure.DebugDraw(&DebugDrawInterface);
		#endif
		}

		void DrawSuspensionConstraints(const FRigidTransform3& SpaceTransform, const FPBDSuspensionConstraints& Constraints, const FChaosDebugDrawSettings* Settings)
		{
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					DrawSuspensionConstraintsImpl(SpaceTransform, Constraints, ConstraintIndex, GetChaosDebugDrawSettings(Settings));
				}
			}
		}

#endif
	}
}
