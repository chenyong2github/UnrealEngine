// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DecoratorBase/Decorator.h"
#include "DecoratorInterfaces/IEvaluate.h"
#include "DecoratorInterfaces/IHierarchy.h"
#include "DecoratorInterfaces/IUpdate.h"

namespace UE::AnimNext
{
	/**
	 * FBlendTwoWayDecorator
	 * 
	 * A decorator that can blend two inputs.
	 */
	struct FBlendTwoWayDecorator : FDecorator, IEvaluate, IUpdate, IHierarchy
	{
		DECLARE_ANIM_DECORATOR(FBlendTwoWayDecorator, 0x96a81d1e, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorHandle Children[2];
			double BlendWeight = 0.0;
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorPtr Children[2];
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IEvaluate impl
		virtual void PostEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override;

		// IUpdate impl
		virtual void PreUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override;

		// IHierarchy impl
		virtual void GetChildren(FExecutionContext& Context, const TDecoratorBinding<IHierarchy>& Binding, FChildrenArray& Children) const override;
	};
}
