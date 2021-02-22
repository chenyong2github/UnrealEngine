// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CanvasTypes.h"
#include "RenderGraphBuilder.h"

class FCanvasRenderContext
{
public:
	ENGINE_API FCanvasRenderContext(FRDGBuilder& GraphBuilder, const FCanvas& Canvas);
	ENGINE_API FCanvasRenderContext(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FIntRect ViewportRect, FIntRect ScissorRect);

	template <typename ExecuteLambdaType, typename ParameterStructType>
	void AddPass(FRDGEventName&& PassName, const ParameterStructType* PassParameters, ExecuteLambdaType&& ExecuteLambda)
	{
		GraphBuilder.AddPass(Forward<FRDGEventName>(PassName), PassParameters, ERDGPassFlags::Raster,
			[LocalScissorRect = ScissorRect, LocalViewportRect = ViewportRect, LocalExecuteLambda = Forward<ExecuteLambdaType&&>(ExecuteLambda)](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(LocalViewportRect.Min.X, LocalViewportRect.Min.Y, 0.0f, LocalViewportRect.Max.X, LocalViewportRect.Max.Y, 1.0f);

			if (LocalScissorRect.Area() > 0)
			{
				RHICmdList.SetScissorRect(true, LocalScissorRect.Min.X, LocalScissorRect.Min.Y, LocalScissorRect.Max.X, LocalScissorRect.Max.Y);
			}
		
			LocalExecuteLambda(RHICmdList);
		});
	}

	template <typename ExecuteLambdaType>
	void AddPass(FRDGEventName&& PassName, ExecuteLambdaType&& ExecuteLambda)
	{
		FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(RenderTarget, ERenderTargetLoadAction::ELoad);
		AddPass(Forward<FRDGEventName&&>(PassName), PassParameters, Forward<ExecuteLambdaType&&>(ExecuteLambda));
	}

	template <typename T, typename... TArgs>
	T* Alloc(TArgs&&... Args)
	{
		return GraphBuilder.AllocObject<T>(Forward<TArgs&&>(Args)...);
	}

	template <typename T>
	void DeferredRelease(TSharedPtr<T>&& Ptr)
	{
		// Hold the reference until completion of the graph.
		Alloc<TSharedPtr<T>>(Forward<TSharedPtr<T>&&>(Ptr));
	}

	template <typename T>
	void DeferredDelete(const T* Ptr)
	{
		struct FDeleter
		{
			FDeleter(const T* InPtr)
				: Ptr(InPtr)
			{}

			~FDeleter()
			{
				delete Ptr;
			}

			const T* Ptr;
		};

		Alloc<FDeleter>(Ptr);
	}

	FRDGTextureRef GetRenderTarget() const
	{
		return RenderTarget;
	}

	FIntRect GetViewportRect() const
	{
		return ViewportRect;
	}

	FIntRect GetScissorRect() const
	{
		return ScissorRect;
	}

	FRDGBuilder& GraphBuilder;

private:
	FRDGTextureRef RenderTarget;
	FIntRect ViewportRect;
	FIntRect ScissorRect;
};

class FCanvasRenderThreadScope
{
	using RenderCommandFunction = TFunction<void(FCanvasRenderContext&)>;
	using RenderCommandFunctionArray = TArray<RenderCommandFunction>;
public:
	ENGINE_API FCanvasRenderThreadScope(const FCanvas& Canvas);
	ENGINE_API ~FCanvasRenderThreadScope();

	void EnqueueRenderCommand(RenderCommandFunction&& Lambda)
	{
		RenderCommands->Add(MoveTemp(Lambda));
	}

	template <typename ExecuteLambdaType>
	void AddPass(const TCHAR* PassName, ExecuteLambdaType&& Lambda)
	{
		EnqueueRenderCommand(
			[PassName, InLambda = Forward<ExecuteLambdaType&&>(Lambda)]
		(FCanvasRenderContext& RenderContext) mutable
		{
			RenderContext.AddPass(RDG_EVENT_NAME("%s", PassName), Forward<ExecuteLambdaType&&>(InLambda));
		});
	}

	template <typename T>
	void DeferredDelete(const T* Ptr)
	{
		EnqueueRenderCommand([Ptr](FCanvasRenderContext& RenderContext)
		{
			RenderContext.DeferredDelete(Ptr);
		});
	}

private:
	const FCanvas& Canvas;
	RenderCommandFunctionArray* RenderCommands;
};