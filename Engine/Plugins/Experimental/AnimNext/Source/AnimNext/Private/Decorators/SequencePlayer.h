// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "DecoratorBase/Decorator.h"
#include "DecoratorInterfaces/IEvaluate.h"
#include "DecoratorInterfaces/ITimeline.h"
#include "DecoratorInterfaces/IUpdate.h"

namespace UE::AnimNext
{
	class UAnimSequence;

	/**
	 * FSequencePlayerDecorator
	 * 
	 * A decorator that can play an animation sequence.
	 */
	struct FSequencePlayerDecorator : FDecorator, IEvaluate, ITimeline, IUpdate
	{
		DECLARE_ANIM_DECORATOR(FSequencePlayerDecorator, 0xa628ad12, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			TObjectPtr<UAnimSequence> AnimSeq;

			double PlayRate = 1.0;
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			double CurrentTime = 0.0;
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IEvaluate impl
		virtual void PreEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override;

		// ITimeline impl
		virtual double GetPlayRate(FExecutionContext& Context, const TDecoratorBinding<ITimeline>& Binding) const override;

		// IUpdate impl
		virtual void PreUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override;
	};
}
