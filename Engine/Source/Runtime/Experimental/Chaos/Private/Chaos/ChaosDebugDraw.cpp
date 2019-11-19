// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBD6DJointConstraints.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	namespace DebugDraw
	{
		float ArrowSize = 1.5f;
		float BodyAxisLen = 12.0f;
		float ContactLen = 6.0f;
		float ContactWidth = 2.0f;
		float ContactPhiWidth = 1.5f;
		float ConstraintAxisLen = 5.0f;
		float LineThickness = 0.25f;
		int DrawPriority = 10.0f;
		float DrawScale = 1.0f;
		float FontHeight = 10.0f;
		float FontScale = 1.0f;
		float ShapeThicknesScale = 2.0f;
		float PointSize = 2.0f;

		FAutoConsoleVariableRef CVarArrowSize(TEXT("p.Chaos.DebugDrawArrowSize"), ArrowSize, TEXT("ArrowSize."));
		FAutoConsoleVariableRef CVarBodyAxisLen(TEXT("p.Chaos.DebugDrawBodyAxisLen"), BodyAxisLen, TEXT("BodyAxisLen."));
		FAutoConsoleVariableRef CVarContactLen(TEXT("p.Chaos.DebugDrawContactLen"), ContactLen, TEXT("ContactLen."));
		FAutoConsoleVariableRef CVarContactWidth(TEXT("p.Chaos.DebugDrawContactWidth"), ContactWidth, TEXT("ContactWidth."));
		FAutoConsoleVariableRef CVarContactPhiWidth(TEXT("p.Chaos.DebugDrawContactPhiWidth"), ContactPhiWidth, TEXT("ContactPhiWidth."));
		FAutoConsoleVariableRef CVarConstraintAxisLen(TEXT("p.Chaos.DebugDrawConstraintAxisLen"), ConstraintAxisLen, TEXT("ConstraintAxisLen."));
		FAutoConsoleVariableRef CVarLineThickness(TEXT("p.Chaos.DebugDrawLineThickness"), LineThickness, TEXT("LineThickness."));
		FAutoConsoleVariableRef CVarScale(TEXT("p.Chaos.DebugDrawScale"), DrawScale, TEXT("Scale applied to all Chaos Debug Draw line lengths etc."));

		//
		//
		//

#if CHAOS_DEBUG_DRAW

		void DrawShapesImpl(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, FColor Color)
		{
			// @todo(ccaulfield): handle scale throughout
			switch (Shape->GetType(false))
			{
			case ImplicitObjectType::Sphere:
			{
				const TSphere<FReal, 3>* Sphere = Shape->template GetObject<TSphere<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Sphere->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugSphere(P, Sphere->GetRadius(), 20, Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Box:
			{
				const TBox<FReal, 3>* Box = Shape->template GetObject<TBox<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Box->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugBox(P, (FReal)0.5 * Box->Extents(), ShapeTransform.GetRotation(), Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
			{
				const TCapsule<FReal>* Capsule = Shape->template GetObject<TCapsule<FReal>>();
				const FVec3 P = ShapeTransform.TransformPosition(Capsule->GetCenter());
				const FRotation3 Q = ShapeTransform.GetRotation() * FRotationMatrix::MakeFromZ(Capsule->GetAxis());
				FDebugDrawQueue::GetInstance().DrawDebugCapsule(P, (FReal)0.5 * Capsule->GetHeight() + Capsule->GetRadius(), Capsule->GetRadius(), Q, Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Transformed:
			{
				const TImplicitObjectTransformed<FReal, 3>* Transformed = Shape->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform = FRigidTransform3(ShapeTransform.TransformPosition(Transformed->GetTransform().GetLocation()), ShapeTransform.GetRotation() * Transformed->GetTransform().GetRotation());
				DrawShapesImpl(TransformedTransform, Transformed->GetTransformedObject(), Color);
				break;
			}
			case ImplicitObjectType::Union:
			{
				const TImplicitObjectUnion<FReal, 3>* Union = Shape->template GetObject<TImplicitObjectUnion<FReal, 3>>();
				for (auto& UnionShape : Union->GetObjects())
				{
					DrawShapesImpl(ShapeTransform, UnionShape.Get(), Color);
				}
				break;
			}
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
				break;
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
				break;
			case ImplicitObjectType::HeightField:
				break;
			default:
				break;
			}
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<FReal, 3>* Particle, FReal ColorScale)
		{
			FColor ShapeColor = Particle->AsDynamic() ? FColor::Yellow : FColor::Orange;
			FColor Color = (((FReal)0.5 * ColorScale) * ShapeColor).ToFColor(false);
			FVec3 P = SpaceTransform.TransformPosition(Particle->AsDynamic() ? Particle->AsDynamic()->P() : Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->AsDynamic() ? Particle->AsDynamic()->Q() : Particle->R());

			DrawShapesImpl(FRigidTransform3(P, Q), Particle->Geometry().Get(), Color);
		}

		void DrawParticleTransformImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<FReal, 3>* Particle, FReal ColorScale)
		{
			FColor R = (ColorScale * FColor::Red).ToFColor(false);
			FColor G = (ColorScale * FColor::Green).ToFColor(false);
			FColor B = (ColorScale * FColor::Blue).ToFColor(false);

			FVec3 P = SpaceTransform.TransformPosition((Particle->AsDynamic()) ? Particle->AsDynamic()->P() : Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * ((Particle->AsDynamic()) ? Particle->AsDynamic()->Q() : Particle->R());
			FMatrix33 Qm = Q.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(0), DrawScale * ArrowSize, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(1), DrawScale * ArrowSize, G, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(2), DrawScale * ArrowSize, B, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
		}

		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const TPBDCollisionConstraintHandle<float, 3>* ConstraintHandle, float ColorScale)
		{
			if (ConstraintHandle->GetType() == TCollisionConstraintBase<float,3>::FType::SinglePoint)
			{
				const TRigidBodyContactConstraint<FReal, 3>& Contact = ConstraintHandle->GetContact< TRigidBodyContactConstraint<float, 3> >();
				if (Contact.GetPhi() > 0)
				{
					ColorScale = ColorScale * (FReal)0.1;
				}

				FVec3 Location = SpaceTransform.TransformPosition(Contact.GetLocation());
				FVec3 Normal = SpaceTransform.TransformVector(Contact.GetNormal());

				if (ContactWidth > 0)
				{
					FColor C0 = (ColorScale * FColor(128, 0, 0)).ToFColor(false);
					FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
					FDebugDrawQueue::GetInstance().DrawDebugCircle(Location, DrawScale * ContactWidth, 12, C0, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
				}
				if (ContactLen > 0)
				{
					FColor C1 = (ColorScale * FColor(255, 0, 0)).ToFColor(false);
					FDebugDrawQueue::GetInstance().DrawDebugLine(Location, Location + DrawScale * ContactLen * Normal, C1, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				}
				if (ContactPhiWidth > 0 && Contact.GetPhi() < FLT_MAX)
				{
					FColor C2 = (ColorScale * FColor(128, 128, 0)).ToFColor(false);
					FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
					FDebugDrawQueue::GetInstance().DrawDebugCircle(Location - Contact.GetPhi() * Normal, DrawScale * ContactPhiWidth, 12, C2, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
				}
			}
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FVec3& InPa, const FVec3& InXa, const FMatrix33& Ra, const FVec3& InPb, const FVec3& InXb, const FMatrix33& Rb, const FVec3& CR, int32 Level, FReal ColorScale, uint32 FeatureMask)
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
			FVec3 Xa = SpaceTransform.TransformPosition(InXa);
			FVec3 Xb = SpaceTransform.TransformPosition(InXb);

			if (FeatureMask & EDebugDrawJointFeature::Connector)
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pa, Xa, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pb, Xb, C, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugPoint(Xa, R, false, KINDA_SMALL_NUMBER, DrawPriority, DrawScale * PointSize);
				FDebugDrawQueue::GetInstance().DrawDebugPoint(Xb, C, false, KINDA_SMALL_NUMBER, DrawPriority, DrawScale * PointSize);
			}
			if (FeatureMask & EDebugDrawJointFeature::Axes)
			{
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(0)), DrawScale * ArrowSize, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(1)), DrawScale * ArrowSize, G, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(2)), DrawScale * ArrowSize, B, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(0)), DrawScale * ArrowSize, C, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(1)), DrawScale * ArrowSize, M, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(2)), DrawScale * ArrowSize, Y, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			}
			if ((FeatureMask & EDebugDrawJointFeature::Level) && (Level >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(Xb + FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("{0}"), { Level }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
			}
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 3 * FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("T=  {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Twist]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 2 * FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("S1= {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Swing1]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 1 * FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("S2= {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Swing2]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FPBDJointConstraintHandle* ConstraintHandle, FReal ColorScale, uint32 FeatureMask)
		{
			TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
			if (ConstrainedParticles[0]->AsDynamic() || ConstrainedParticles[1]->AsDynamic())
			{
				FVec3 Pa = TGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[1])->P();
				FVec3 Pb = TGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[0])->P();
				FVec3 Xa, Xb, CR;
				FMatrix33 Ra, Rb;
				ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb, CR);
				DrawJointConstraintImpl(SpaceTransform, Pa, Xa, Ra, Pb, Xb, Rb, CR, ConstraintHandle->GetConstraintLevel(), ColorScale, FeatureMask);
			}
		}

		void Draw6DofConstraintImpl(const FRigidTransform3& SpaceTransform, const FPBD6DJointConstraintHandle* ConstraintHandle, FReal ColorScale)
		{
			FVec3 Pa = TGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[1])->P();
			FVec3 Pb = TGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[0])->P();
			FVec3 Xa, Xb, CR;
			FMatrix33 Ra, Rb;
			ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb, CR);
			DrawJointConstraintImpl(SpaceTransform, Pa, Xa, Ra, Pb, Xb, Rb, CR, INDEX_NONE, ColorScale, EDebugDrawJointFeature::Default);
		}

#endif

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					if ((bDrawKinemtatic && !Particle.AsDynamic()) || (bDrawDynamic && Particle.AsDynamic()))
					{
						DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : Particles)
				{
					if ((bDrawKinemtatic && !Particle->AsDynamic()) || (bDrawDynamic && Particle->AsDynamic()))
					{
						DrawParticleShapesImpl(SpaceTransform, Particle, ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					if ((bDrawKinemtatic && !Particle.AsDynamic()) || (bDrawDynamic && Particle.AsDynamic()))
					{
						DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : Particles)
				{
					if ((bDrawKinemtatic && !Particle->AsDynamic()) || (bDrawDynamic && Particle->AsDynamic()))
					{
						DrawParticleTransformImpl(SpaceTransform, Particle, ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleCollisions(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const TPBDCollisionConstraint<float, 3>& Collisions)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const Chaos::TPBDCollisionConstraintHandle<float, 3> * ConstraintHandle : Collisions.GetConstConstraintHandles())
				{
					TVector<const TGeometryParticleHandle<float, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
					if ((ConstrainedParticles[0] == Particle) || (ConstrainedParticles[1] == Particle))
					{
						DrawCollisionImpl(SpaceTransform, ConstraintHandle, 1.0f);
					}
				}
			}
#endif
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const TPBDCollisionConstraint<float, 3>& Collisions, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const Chaos::TPBDCollisionConstraintHandle<float, 3> * ConstraintHandle : Collisions.GetConstConstraintHandles())
				{
					DrawCollisionImpl(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const TPBDCollisionConstraintHandle<float, 3>* ConstraintHandle : ConstraintHandles)
				{
					DrawCollisionImpl(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}


		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, float ColorScale, uint32 FeatureMask)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBDJointConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					DrawJointConstraintImpl(SpaceTransform, ConstraintHandle, ColorScale, FeatureMask);
				}
			}
#endif
		}

		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, float ColorScale, uint32 FeatureMask)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					DrawJointConstraintImpl(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale, FeatureMask);
				}
			}
#endif
		}

		void Draw6DofConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBD6DJointConstraintHandle*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBD6DJointConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					Draw6DofConstraintImpl(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}

		void Draw6DofConstraints(const FRigidTransform3& SpaceTransform, const FPBD6DJointConstraints& Constraints, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					Draw6DofConstraintImpl(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale);
				}
			}
#endif
		}

	}
}
