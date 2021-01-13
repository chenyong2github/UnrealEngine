// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"

#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Box.h"

namespace Chaos
{
	namespace Utilities
	{
		// Call the lambda with concrete shape type. Unwraps shapes contained in Instanced (e.g., Instanced-Sphere will be called with Sphere. Note that this will effectively discard any instance properties like the margin)
		template <typename Lambda>
		FORCEINLINE_DEBUGGABLE auto CastHelper(const FImplicitObject& Geom, const Lambda& Func)
		{
			const EImplicitObjectType Type = Geom.GetType();
			switch (Type)
			{
			case ImplicitObjectType::Sphere:
			{
				return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>());
			}
			case ImplicitObjectType::Box:
			{
				return Func(Geom.template GetObjectChecked<TBox<FReal, 3>>());
			}
			case ImplicitObjectType::Capsule:
			{
				return Func(Geom.template GetObjectChecked<TCapsule<FReal>>());
			}
			case ImplicitObjectType::Convex:
			{
				return Func(Geom.template GetObjectChecked<FConvex>());
			}
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Sphere:
			{
				return Func(Geom.template GetObjectChecked<TImplicitObjectScaled<TSphere<FReal, 3>>>());
			}
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Box:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TBox<FReal, 3>>>());
			}
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Capsule:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TCapsule<FReal>>>());
			}
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Convex:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<FConvex>>());
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Sphere:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TSphere<FReal, 3>>>().GetInstancedObject()->template GetObjectChecked<TSphere<FReal, 3>>());
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Box:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TBox<FReal, 3>>>().GetInstancedObject()->template GetObjectChecked<TBox<FReal, 3>>());
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Capsule:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TCapsule<FReal>>>().GetInstancedObject()->template GetObjectChecked<TCapsule<FReal>>());
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Convex:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<FConvex>>().GetInstancedObject()->template GetObjectChecked<FConvex>());
			}
			case ImplicitObjectType::Transformed:
			{
				const auto& ImplicitObjectTransformed = (Geom.template GetObjectChecked<TImplicitObjectTransformed<FReal, 3>>());
				return CastHelper(*ImplicitObjectTransformed.GetTransformedObject(), Func);
			}
			default: 
				check(false);
			}
			return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>());	//needed for return type
		}

		// Call the lambda with concrete shape type. Unwraps shapes contained in Instanced (e.g., Instanced-Sphere will be called with Sphere. Note that this will effectively discard any instance properties like the margin)
		template <typename Lambda>
		FORCEINLINE_DEBUGGABLE auto CastHelper(const FImplicitObject& Geom, const FRigidTransform3& TM, const Lambda& Func)
		{
			const EImplicitObjectType Type = Geom.GetType();
			switch (Type)
			{
			case ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);
			case ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked<TBox<FReal, 3>>(), TM);
			case ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked<TCapsule<FReal>>(), TM);
			case ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked<FConvex>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TImplicitObjectScaled<TSphere<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TBox<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TCapsule<FReal>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<FConvex>>(), TM);
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Sphere:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TSphere<FReal, 3>>>().GetInstancedObject()->template GetObjectChecked<TSphere<FReal, 3>>(), TM);
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Box:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TBox<FReal, 3>>>().GetInstancedObject()->template GetObjectChecked<TBox<FReal, 3>>(), TM);
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Capsule:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TCapsule<FReal>>>().GetInstancedObject()->template GetObjectChecked<TCapsule<FReal>>(), TM);
			}
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Convex:
			{
				return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<FConvex>>().GetInstancedObject()->template GetObjectChecked<FConvex>(), TM);
			}
			case ImplicitObjectType::Transformed:
			{
				const auto& ImplicitObjectTransformed = (Geom.template GetObjectChecked<TImplicitObjectTransformed<FReal, 3>>());
				return CastHelper(*ImplicitObjectTransformed.GetTransformedObject(), ImplicitObjectTransformed.GetTransform() * TM, Func);
			}

			default: check(false);
			}
			return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);	//needed for return type
		}

		// Call the lambda with concrete shape type. This version does NOT unwrap shapes contained in Instanced or Scaled.
		template <typename Lambda>
		FORCEINLINE_DEBUGGABLE auto CastHelperNoUnwrap(const FImplicitObject& Geom, const FRigidTransform3& TM, const Lambda& Func)
		{
			const EImplicitObjectType Type = Geom.GetType();
			switch (Type)
			{
			case ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);
			case ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked<TBox<FReal, 3>>(), TM);
			case ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked<TCapsule<FReal>>(), TM);
			case ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked<FConvex>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TImplicitObjectScaled<TSphere<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TBox<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TCapsule<FReal>>>(), TM);
			case ImplicitObjectType::IsScaled | ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<FConvex>>(), TM);
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TSphere<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TBox<FReal, 3>>>(), TM);
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TCapsule<FReal>>>(), TM);
			case ImplicitObjectType::IsInstanced | ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<FConvex>>(), TM);
			case ImplicitObjectType::Transformed:
			{
				const auto& ImplicitObjectTransformed = (Geom.template GetObjectChecked<TImplicitObjectTransformed<FReal, 3>>());
				return CastHelper(*ImplicitObjectTransformed.GetTransformedObject(), ImplicitObjectTransformed.GetTransform() * TM, Func);
			}

			default: check(false);
			}
			return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);	//needed for return type
		}

		inline
		const FImplicitObject* ImplicitChildHelper(const FImplicitObject* ImplicitObject)
		{
			EImplicitObjectType ImplicitType = ImplicitObject->GetType();
			if (ImplicitType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectTransformed<FReal, 3>>()->GetTransformedObject();
			}

			else if (ImplicitType == TImplicitObjectScaled<FConvex>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectScaled<FConvex>>()->GetUnscaledObject();
			}
			else if (ImplicitType == TImplicitObjectScaled<TBox<FReal, 3>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectScaled<TBox<FReal, 3>>>()->GetUnscaledObject();
			}
			else if (ImplicitType == TImplicitObjectScaled<TCapsule<FReal>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectScaled<TCapsule<FReal>>>()->GetUnscaledObject();
			}
			else if (ImplicitType == TImplicitObjectScaled<TSphere<FReal, 3>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectScaled<TSphere<FReal, 3>>>()->GetUnscaledObject();
			}
			else if (ImplicitType == TImplicitObjectScaled<FTriangleMeshImplicitObject>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>()->GetUnscaledObject();
			}

			else if (ImplicitType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectInstanced<FConvex>>()->GetInstancedObject();
			}
			else if (ImplicitType == TImplicitObjectInstanced<TBox<FReal, 3>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectInstanced<TBox<FReal, 3>>>()->GetInstancedObject();
			}
			else if (ImplicitType == TImplicitObjectInstanced<TCapsule<FReal>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectInstanced<TCapsule<FReal>>>()->GetInstancedObject();
			}
			else if (ImplicitType == TImplicitObjectInstanced<TSphere<FReal, 3>>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectInstanced<TSphere<FReal, 3>>>()->GetInstancedObject();
			}
			else if (ImplicitType == TImplicitObjectInstanced<FTriangleMeshImplicitObject>::StaticType())
			{
				return ImplicitObject->template GetObject<const TImplicitObjectInstanced<FTriangleMeshImplicitObject>>()->GetInstancedObject();
			}
			return nullptr;
		}

	} // namespace Utilities

} // namespace Chaos
