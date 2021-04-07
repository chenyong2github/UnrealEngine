// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneViewExtension.h: Allow changing the view parameters on the render thread
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "RendererInterface.h"
#include "SceneViewExtensionContext.h"

/**
 *  SCENE VIEW EXTENSIONS
 *  -----------------------------------------------------------------------------------------------
 *
 *  This system lets you hook various aspects of UE4 rendering.
 *  To create a view extension, it is advisable to inherit
 *  from FSceneViewExtensionBase, which implements the
 *  ISceneViewExtension interface.
 *
 *
 *
 *  INHERITING, INSTANTIATING, LIFETIME
 *  -----------------------------------------------------------------------------------------------
 *
 *  In order to inherit from FSceneViewExtensionBase, do the following:
 *
 *      class FMyExtension : public FSceneViewExtensionBase
 *      {
 *          public:
 *          FMyExtension( const FAutoRegister& AutoRegister, FYourParam1 Param1, FYourParam2 Param2 )
 *          : FSceneViewExtensionBase( AutoRegister )
 *          {
 *          }
 *      };
 *
 *  Notice that your first argument must be FAutoRegister, and you must pass it
 *  to FSceneViewExtensionBase constructor. To instantiate your extension and register
 *  it, do the following:
 *
 *      FSceneViewExtensions::NewExtension<FMyExtension>(Param1, Param2);
 *
 *  You should maintain a reference to the extension for as long as you want to
 *  keep it registered.
 *
 *      TSharedRef<FMyExtension,ESPMode::ThreadSafe> MyExtension;
 *      MyExtension = FSceneViewExtensions::NewExtension<FMyExtension>(Param1, Param2);
 *
 *  If you follow this pattern, the cleanup of the extension will be safe and automatic
 *  whenever the `MyExtension` reference goes out of scope. In most cases, the `MyExtension`
 *  variable should be a member of the class owning the extension instance.
 *
 *  The engine will keep the extension alive for the duration of the current frame to allow
 *  the render thread to finish.
 *
 *
 *
 *  OPTING OUT of RUNNING
 *  -----------------------------------------------------------------------------------------------
 *
 *  Each frame, the engine will invoke ISceneVewExtension::IsActiveThisFrame() to determine
 *  if your extension wants to run this frame. Returning false will cause none of the methods
 *  to be called this frame. The IsActiveThisFrame() method will be invoked again next frame.
 *
 *  If you need fine grained control over individual methods, your IsActiveThisFrame should
 *  return `true` and gate each method as needed.
 *
 *
 *
 *  PRIORITY
 *  -----------------------------------------------------------------------------------------------
 *  Extensions are executed in priority order. Higher priority extensions run first.
 *  To determine the priority of your extension, override ISceneViewExtension::GetPriority();
 *
 */

class APlayerController;
class FRHICommandListImmediate;
class FSceneView;
class FSceneViewFamily;
struct FMinimalViewInfo;
struct FSceneViewProjectionData;
class FRDGBuilder;
struct FPostProcessingInputs;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;
class FViewport;


/** This is used to add more flexibility to Post Processing, so that users can subscribe to any after Post Porocessing Pass events. */
FUNC_DECLARE_DELEGATE(FAfterPassCallbackDelegate, FScreenPassTexture /*ReturnSceneColor*/, FRDGBuilder& /*GraphBuilder*/, const FSceneView& /*View*/, const FPostProcessMaterialInputs& /*Inputs*/)
using FAfterPassCallbackDelegateArray = TArray<FAfterPassCallbackDelegate, SceneRenderingAllocator>;


class ISceneViewExtension
{
public:

	enum class EPostProcessingPass : uint32
	{
		MotionBlur,
		Tonemap,
		FXAA,
		VisualizeDepthOfField,
		MAX
	};

public:
	virtual ~ISceneViewExtension() {}

	/**
     * Called on game thread when creating the view family.
     */
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) = 0;

	/**
	 * Called on game thread when creating the view.
	 */
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) = 0;

	/**
	* Called when creating the viewpoint, before culling, in case an external tracking device needs to modify the base location of the view
	*/
	virtual void SetupViewPoint(APlayerController* Player, FMinimalViewInfo& InViewInfo) {}

    /**
	 * Called when creating the view, in case non-stereo devices need to update projection matrix.
	 */
	virtual void SetupViewProjectionMatrix(FSceneViewProjectionData& InOutProjectionData) {}

    /**
     * Called on game thread when view family is about to be rendered.
     */
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) = 0;

    /**
     * Called on render thread at the start of rendering.
     */
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) = 0;

	/**
     * Called on render thread at the start of rendering, for each view, after PreRenderViewFamily_RenderThread call.
     */
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) = 0;

	/**
	 * Called right after Base Pass rendering finished
	 */
	virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {};

	/**
	 * Called right before Post Processing rendering begins
	 */
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) {};

	/**
	* This will be called at the beginning of post processing to make sure that each view extension gets a chance to subscribe to an after pass event.
	*/
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) {};

	/**
	 * Allows to render content after the 3D content scene, useful for debugging
	 */
	virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}

	/**
	 * Allows to render content after the 3D content scene, useful for debugging
	 */
	virtual void PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}

	/**
     * Called to determine view extensions priority in relation to other view extensions, higher comes first
     */
	virtual int32 GetPriority() const { return 0; }

	/**
	 * Returning false disables the extension for the current frame. This will be queried each frame to determine if the extension wants to run.
	 */
	UE_DEPRECATED(4.27, "Deprecated. Please use IsActiveThisFrame by passing an FSceneViewExtensionContext parameter")
	virtual bool IsActiveThisFrame(class FViewport* InViewport) const { return true; }

	/**
	 * Called right before late latching on all view extensions
	 */
	virtual void PreLateLatchingViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {};

	/**
	 * Called to apply late latching per viewFamily
	 */
	virtual void LateLatchingViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {};

	/**
	 * Called to apply late latching per view
	 */
	virtual void LateLatchingView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily, FSceneView& View) {};

	/**
	 * Returning false disables the extension for the current frame in the given context. This will be queried each frame to determine if the extension wants to run.
	 */
	virtual bool IsActiveThisFrame(const FSceneViewExtensionContext& Context) const { return IsActiveThisFrame_Internal(Context); }

	/**
     * Returning false disables the extension for the current frame in the given context. This will be queried each frame to determine if the extension wants to run.
     */
	UE_DEPRECATED(4.27, "Deprecated. Please use IsActiveThisFrame_Internal instead.")
	virtual bool IsActiveThisFrameInContext(FSceneViewExtensionContext& Context) const { return IsActiveThisFrame(Context); }
protected:
	/**
	 * Called if no IsActive functors returned a definitive answer to whether this extension should be active this frame.
	 */
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const { return true; }
};



/**
 * Used to ensure that all extensions are constructed
 * via FSceneViewExtensions::NewExtension<T>(Args).
 */
class FAutoRegister
{
	friend class FSceneViewExtensions;
	FAutoRegister(){}
};


/** Inherit from this class to make a view extension. */
class ENGINE_API FSceneViewExtensionBase : public ISceneViewExtension, public TSharedFromThis<FSceneViewExtensionBase, ESPMode::ThreadSafe>
{
public:
	FSceneViewExtensionBase(const FAutoRegister&) {}
	virtual ~FSceneViewExtensionBase();

	// Array of Functors that can be used to activate an extension for the current frame and given context.
	TArray<FSceneViewExtensionIsActiveFunctor> IsActiveThisFrameFunctions;

	// Determines if the extension should be active for the current frame and given context.
	virtual bool IsActiveThisFrame(const FSceneViewExtensionContext& Context) const override final;
protected:
	// Temporary override so that old behaviour still functions. Will be removed along with IsActiveThisFrame(FViewport*).
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
};

/** Scene View Extension which is enabled for all Viewports/Scenes which have the same world. */
class ENGINE_API FWorldSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FWorldSceneViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld);
protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
private:
	/** The world of this view extension. */
	TWeakObjectPtr<UWorld> World;
};

using FSceneViewExtensionRef = TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe>;

/**
 * Repository of all registered scene view extensions.
 */
class ENGINE_API FSceneViewExtensions
{
	friend class FSceneViewExtensionBase;

public:
	/**
	 * Create a new extension of type ExtensionType.
	 */
	template<typename ExtensionType, typename... TArgs>
	static TSharedRef<ExtensionType, ESPMode::ThreadSafe> NewExtension( TArgs&&... Args )
	{
		TSharedRef<ExtensionType, ESPMode::ThreadSafe> NewExtension = MakeShareable(new ExtensionType( FAutoRegister(), Forward<TArgs>(Args)... ));
		RegisterExtension(NewExtension);
		return NewExtension;
	}

	/**
	 * Executes a function on each view extension which is active in a given context.
	 */
	static void ForEachActiveViewExtension(
		const TArray<TWeakPtr<ISceneViewExtension, ESPMode::ThreadSafe>>& InExtensions, 
		const FSceneViewExtensionContext& InContext,
		const TFunctionRef<void(const FSceneViewExtensionRef&)>& Func);

	/**
	 * Gathers all ViewExtensions that want to be active for a given viewport (@see ISceneViewExtension::IsActiveThisFrame()).
	 * The list is sorted by priority (@see ISceneViewExtension::GetPriority())
	 */
	UE_DEPRECATED(4.27, "Deprecated. Please use GatherActiveExtensions by passing an FSceneViewExtensionContext parameter")
	const TArray<FSceneViewExtensionRef> GatherActiveExtensions(class FViewport* InViewport = nullptr) const;

	/**
     * Gathers all ViewExtensions that want to be active in a given context (@see ISceneViewExtension::IsActiveThisFrame()).
     * The list is sorted by priority (@see ISceneViewExtension::GetPriority())
     */
	const TArray<FSceneViewExtensionRef> GatherActiveExtensions(const FSceneViewExtensionContext& InContext) const;

private:
	static void RegisterExtension(const FSceneViewExtensionRef& RegisterMe);
	TArray< TWeakPtr<ISceneViewExtension, ESPMode::ThreadSafe> > KnownExtensions;
};

