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

		template <typename T, int d>
		void DrawShapesImpl(const TRigidTransform<float, 3>& ShapeTransform, const TImplicitObject<T, d>* Shape, FColor Color)
		{
			// @todo(ccaulfield): handle scale throughout
			switch (Shape->GetType(false))
			{
			case ImplicitObjectType::Sphere:
			{
				const TSphere<T, d>* Sphere = Shape->template GetObject<TSphere<T, d>>();
				const TVector<T, d> P = ShapeTransform.TransformPosition(Sphere->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugSphere(P, Sphere->GetRadius(), 20, Color, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				break;
			}
			case ImplicitObjectType::Box:
			{
				const TBox<T, d>* Box = Shape->template GetObject<TBox<T, d>>();
				const TVector<T, d> P = ShapeTransform.TransformPosition(Box->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugBox(P, (T)0.5 * Box->Extents(), ShapeTransform.GetRotation(), Color, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				break;
			}
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
			{
				const TCapsule<T>* Capsule = Shape->template GetObject<TCapsule<T>>();
				const TVector<T, d> P = ShapeTransform.TransformPosition(Capsule->GetCenter());
				const TRotation<T, d> Q = ShapeTransform.GetRotation() * FRotationMatrix::MakeFromZ(Capsule->GetAxis());
				FDebugDrawQueue::GetInstance().DrawDebugCapsule(P, (T)0.5 * Capsule->GetHeight() + Capsule->GetRadius(), Capsule->GetRadius(), Q, Color, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				break;
			}
			case ImplicitObjectType::Transformed:
			{
				const TImplicitObjectTransformed<T, d>* Transformed = Shape->template GetObject<TImplicitObjectTransformed<T, d>>();
				TRigidTransform<float, 3> TransformedTransform = TRigidTransform<float, 3>(ShapeTransform.TransformPosition(Transformed->GetTransform().GetLocation()), ShapeTransform.GetRotation() * Transformed->GetTransform().GetRotation());
				DrawShapesImpl<T, d>(TransformedTransform, Transformed->GetTransformedObject(), Color);
				break;
			}
			case ImplicitObjectType::Union:
			{
				const TImplicitObjectUnion<T, d>* Union = Shape->template GetObject<TImplicitObjectUnion<T, d>>();
				for (auto& UnionShape : Union->GetObjects())
				{
					DrawShapesImpl<T, d>(ShapeTransform, UnionShape.Get(), Color);
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
			case ImplicitObjectType::Scaled:
				break;
			default:
				break;
			}
		}

		template <typename T, int d>
		void DrawParticleShapesImpl(const TRigidTransform<float, 3>& SpaceTransform, const TGeometryParticleHandle<T, d>* Particle, T ColorScale)
		{
			FColor ShapeColor = Particle->AsDynamic() ? FColor::Yellow : FColor::Orange;
			FColor Color = (((T)0.5 * ColorScale) * ShapeColor).ToFColor(false);
			TVector<T, d> P = SpaceTransform.TransformPosition(Particle->AsDynamic() ? Particle->AsDynamic()->P() : Particle->X());
			TRotation<T, d> Q = SpaceTransform.GetRotation() * (Particle->AsDynamic() ? Particle->AsDynamic()->Q() : Particle->R());

			DrawShapesImpl<T, d>(TRigidTransform<T, d>(P, Q), Particle->Geometry().Get(), Color);
		}

		template <typename T, int d>
		void DrawParticleTransformImpl(const TRigidTransform<float, 3>& SpaceTransform, const TGeometryParticleHandle<T, d>* Particle, T ColorScale)
		{
			FColor R = (ColorScale * FColor::Red).ToFColor(false);
			FColor G = (ColorScale * FColor::Green).ToFColor(false);
			FColor B = (ColorScale * FColor::Blue).ToFColor(false);

			TVector<T, d> P = SpaceTransform.TransformPosition((Particle->AsDynamic()) ? Particle->AsDynamic()->P() : Particle->X());
			TRotation<T, d> Q = SpaceTransform.GetRotation() * ((Particle->AsDynamic()) ? Particle->AsDynamic()->Q() : Particle->R());
			PMatrix<T, d, d> Qm = Q.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(0), DrawScale * ArrowSize, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(1), DrawScale * ArrowSize, G, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(P, P + DrawScale * BodyAxisLen * Qm.GetAxis(2), DrawScale * ArrowSize, B, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
		}

		template <typename T, int d>
		void DrawCollisionImpl(const TRigidTransform<float, 3>& SpaceTransform, const TPBDCollisionConstraintHandle<float, 3>* ConstraintHandle, float ColorScale)
		{
			const TRigidBodyContactConstraint<T, d>& Contact = ConstraintHandle->GetContact();
			if (Contact.Phi > 0)
			{
				ColorScale = ColorScale * (T)0.1;
			}

			TVector<T, d> Location = SpaceTransform.TransformPosition(Contact.Location);
			TVector<T, d> Normal = SpaceTransform.TransformVector(Contact.Normal);

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
			if (ContactPhiWidth > 0 && Contact.Phi < FLT_MAX)
			{
				FColor C2 = (ColorScale * FColor(128, 128, 0)).ToFColor(false);
				FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
				FDebugDrawQueue::GetInstance().DrawDebugCircle(Location - Contact.Phi * Normal, DrawScale * ContactPhiWidth, 12, C2, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
			}
		}

		template<typename T, int d>
		void DrawJointConstraintImpl(const TRigidTransform<float, 3>& SpaceTransform, const TVector<T, d>& InXa, const PMatrix<T, d, d>& Ra, const TVector<T, d>& InXb, const PMatrix<T, d, d>& Rb, const TVector<T, d>& CR, T ColorScale)
		{
			using namespace Chaos::DebugDraw;
			FColor R = (ColorScale * FColor::Red).ToFColor(false);
			FColor G = (ColorScale * FColor::Green).ToFColor(false);
			FColor B = (ColorScale * FColor::Blue).ToFColor(false);
			FColor C = (ColorScale * FColor::Cyan).ToFColor(false);
			FColor M = (ColorScale * FColor::Magenta).ToFColor(false);
			FColor Y = (ColorScale * FColor::Yellow).ToFColor(false);
			TVector<T, d> Xa = SpaceTransform.TransformPosition(InXa);
			TVector<T, d> Xb = SpaceTransform.TransformPosition(InXb);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(0)), DrawScale * ArrowSize, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(1)), DrawScale * ArrowSize, G, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(2)), DrawScale * ArrowSize, B, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(0)), DrawScale * ArrowSize, C, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(1)), DrawScale * ArrowSize, M, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(2)), DrawScale * ArrowSize, Y, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 3 * FontHeight * TVector<T, d>(0, 0, 1), FString::Format(TEXT("T=  {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Twist]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 2 * FontHeight * TVector<T, d>(0, 0, 1), FString::Format(TEXT("S1= {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Swing1]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
			//FDebugDrawQueue::GetInstance().DrawDebugString(Xb + 1 * FontHeight * TVector<T, d>(0, 0, 1), FString::Format(TEXT("S2= {0}"), { FMath::RadiansToDegrees(CR[(int32)E6DJointAngularConstraintIndex::Swing2]) }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
		}

		template<typename T, int d>
		void DrawJointConstraintImpl(const TRigidTransform<float, 3>& SpaceTransform, const TPBDJointConstraintHandle<float, 3>* ConstraintHandle, T ColorScale)
		{
			TVector<TGeometryParticleHandle<T, d>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
			if (ConstrainedParticles[0]->AsDynamic() || ConstrainedParticles[1]->AsDynamic())
			{
				TVector<T, d> Xa, Xb, CR;
				PMatrix<T, d, d> Ra, Rb;
				ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb, CR);
				DrawJointConstraintImpl<T, d>(SpaceTransform, Xa, Ra, Xb, Rb, CR, ColorScale);
			}
		}

		template<typename T, int d>
		void Draw6DofConstraintImpl(const TRigidTransform<float, 3>& SpaceTransform, const TPBD6DJointConstraintHandle<float, 3>* ConstraintHandle, T ColorScale)
		{
			TVector<T, d> Xa, Xb, CR;
			PMatrix<T, d, d> Ra, Rb;
			ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb, CR);
			DrawJointConstraintImpl<T, d>(SpaceTransform, Xa, Ra, Xb, Rb, CR, ColorScale);
		}

#endif

		//
		//
		//

		void DrawParticleShapes(const TRigidTransform<float, 3>& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					if ((bDrawKinemtatic && !Particle.AsDynamic()) || (bDrawDynamic && Particle.AsDynamic()))
					{
						DrawParticleShapesImpl<float, 3>(SpaceTransform, GetHandleHelper<float, 3>(&Particle), ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleShapes(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : Particles)
				{
					if ((bDrawKinemtatic && !Particle->AsDynamic()) || (bDrawDynamic && Particle->AsDynamic()))
					{
						DrawParticleShapesImpl<float, 3>(SpaceTransform, Particle, ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleTransforms(const TRigidTransform<float, 3>& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					if ((bDrawKinemtatic && !Particle.AsDynamic()) || (bDrawDynamic && Particle.AsDynamic()))
					{
						DrawParticleTransformImpl<float, 3>(SpaceTransform, GetHandleHelper<float, 3>(&Particle), ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleTransforms(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinemtatic, bool bDrawDynamic)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : Particles)
				{
					if ((bDrawKinemtatic && !Particle->AsDynamic()) || (bDrawDynamic && Particle->AsDynamic()))
					{
						DrawParticleTransformImpl<float, 3>(SpaceTransform, Particle, ColorScale);
					}
				}
			}
#endif
		}

		void DrawParticleCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const TPBDCollisionConstraint<float, 3>& Collisions)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
				{
					TVector<TGeometryParticleHandle<float, 3>*, 2> ConstrainedParticles = Collisions.GetConstrainedParticles(ConstraintIndex);
					if ((ConstrainedParticles[0] == Particle) || (ConstrainedParticles[1] == Particle))
					{
						const typename TPBDCollisionConstraint<float, 3>::FConstraintHandle* ConstraintHandle = Collisions.GetConstraintHandle(ConstraintIndex);
						DrawCollisionImpl<float, 3>(SpaceTransform, ConstraintHandle, 1.0f);
					}
				}
			}
#endif
		}

		void DrawCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TPBDCollisionConstraint<float, 3>& Collisions, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
				{
					DrawCollisionImpl<float, 3>(SpaceTransform, Collisions.GetConstraintHandle(ConstraintIndex), ColorScale);
				}
			}
#endif
		}

		void DrawCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const TPBDCollisionConstraintHandle<float, 3>* ConstraintHandle : ConstraintHandles)
				{
					DrawCollisionImpl<float, 3>(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}


		void DrawJointConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBDJointConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const TPBDJointConstraintHandle<float, 3>* ConstraintHandle : ConstraintHandles)
				{
					DrawJointConstraintImpl<float, 3>(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}

		void DrawJointConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TPBDJointConstraints<float, 3>& Constraints, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					DrawJointConstraintImpl<float, 3>(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale);
				}
			}
#endif
		}

		void Draw6DofConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBD6DJointConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const TPBD6DJointConstraintHandle<float, 3>* ConstraintHandle : ConstraintHandles)
				{
					Draw6DofConstraintImpl<float, 3>(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}

		void Draw6DofConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TPBD6DJointConstraints<float, 3>& Constraints, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					Draw6DofConstraintImpl<float, 3>(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale);
				}
			}
#endif
		}

	}
}
