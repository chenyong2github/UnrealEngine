// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneViewExtension.h"
#include "Engine/Engine.h"

//
// FSceneViewExtensionBase
//

FSceneViewExtensionBase::~FSceneViewExtensionBase()
{
	// The engine stores view extensions by TWeakPtr<ISceneViewExtension>,
	// so they will be automatically unregistered when removed.
}

bool FSceneViewExtensionBase::IsActiveThisFrameInContext(FSceneViewExtensionContext& Context) const
{
	// Go over any existing activation lambdas
	for (const FSceneViewExtensionIsActiveFunctor& IsActiveFunction : IsActiveThisFrameFunctions)
	{
		TOptional<bool> IsActive = IsActiveFunction(this, Context);

		// If the function does not return a definive answer, try the next one.
		if (IsActive.IsSet())
		{
			return IsActive.GetValue();
		}
	}

	// Fall back to the validation based on the viewport.
	return IsActiveThisFrame(Context.Viewport);
}



//
// FSceneViewExtensions
//

void FSceneViewExtensions::RegisterExtension(const TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe>& RegisterMe)
{
	if (ensure(GEngine))
	{
		auto& KnownExtensions = GEngine->ViewExtensions->KnownExtensions;
		// Compact the list of known extensions.
		for (int32 i = 0; i < KnownExtensions.Num(); )
		{
			if (KnownExtensions[i].IsValid())
			{
				i++;
			}
			else
			{
				KnownExtensions.RemoveAtSwap(i);
			}
		}

		KnownExtensions.AddUnique(RegisterMe);
	}
}

// @todo viewext : We should cache all the active extensions in OnStartFrame somewhere
const TArray<TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe>> FSceneViewExtensions::GatherActiveExtensions(FViewport* InViewport /* = nullptr */) const
{
	FSceneViewExtensionContext Context(InViewport);
	return GatherActiveExtensions(Context);
}

const TArray<TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe>> FSceneViewExtensions::GatherActiveExtensions(FSceneViewExtensionContext& InContext) const
{
	TArray<TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe>> ActiveExtensions;
	ActiveExtensions.Reserve(KnownExtensions.Num());

	for (auto& ViewExtPtr : KnownExtensions)
	{
		auto ViewExt = ViewExtPtr.Pin();
		if (ViewExt.IsValid() && ViewExt->IsActiveThisFrameInContext(InContext))
		{
			ActiveExtensions.Add(ViewExt.ToSharedRef());
		}
	}

	struct SortPriority
	{
		bool operator () (const TSharedPtr<class ISceneViewExtension, ESPMode::ThreadSafe>& A, const TSharedPtr<class ISceneViewExtension, ESPMode::ThreadSafe>& B) const
		{
			return A->GetPriority() > B->GetPriority();
		}
	};

	Sort(ActiveExtensions.GetData(), ActiveExtensions.Num(), SortPriority());

	return ActiveExtensions;
}

