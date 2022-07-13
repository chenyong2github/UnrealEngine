// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphEditorModelActor.h"
#include "GeometryCacheComponent.h"
#include "Animation/DebugSkelMeshComponent.h"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	FNeuralMorphEditorModelActor::FNeuralMorphEditorModelActor(const FConstructSettings& Settings)
		: FMLDeformerEditorActor(Settings)
	{
	}

	void FNeuralMorphEditorModelActor::SetVisibility(bool bIsVisible)
	{
		FMLDeformerEditorActor::SetVisibility(bIsVisible);

		if (GeomCacheComponent && bIsVisible != GeomCacheComponent->IsVisible())
		{
			GeomCacheComponent->SetVisibility(bIsVisible, true);
		}
	}

	bool FNeuralMorphEditorModelActor::IsVisible() const
	{
		if (SkeletalMeshComponent)
		{
			return SkeletalMeshComponent->IsVisible();
		}

		if (GeomCacheComponent)
		{
			return GeomCacheComponent->IsVisible();
		}

		return true;
	}

	bool FNeuralMorphEditorModelActor::HasVisualMesh() const
	{
		if (SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh)
		{
			return true;
		}

		if (GeomCacheComponent && GeomCacheComponent->GetGeometryCache())
		{
			return true;
		}

		return false;
	}

	void FNeuralMorphEditorModelActor::SetPlayPosition(float TimeInSeconds, bool bAutoPause)
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetPosition(TimeInSeconds);
			if (bAutoPause)
			{
				SkeletalMeshComponent->bPauseAnims = true;
			}
		}

		if (GeomCacheComponent)
		{
			GeomCacheComponent->SetManualTick(true);
			GeomCacheComponent->TickAtThisTime(TimeInSeconds, false, false, false);
		}
	}

	float FNeuralMorphEditorModelActor::GetPlayPosition() const
	{
		if (SkeletalMeshComponent)
		{
			return SkeletalMeshComponent->GetPosition();
		}

		if (GeomCacheComponent)
		{
			return GeomCacheComponent->GetAnimationTime();
		}

		return 0.0f;
	}

	void FNeuralMorphEditorModelActor::SetPlaySpeed(float PlaySpeed)
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetPlayRate(PlaySpeed);
		}

		if (GeomCacheComponent)
		{
			GeomCacheComponent->SetPlaybackSpeed(PlaySpeed);
		}
	}

	void FNeuralMorphEditorModelActor::Pause(bool bPaused)
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->bPauseAnims = bPaused;
		}

		if (GeomCacheComponent)
		{
			GeomCacheComponent->SetManualTick(bPaused);
		}
	}

	FBox FNeuralMorphEditorModelActor::GetBoundingBox() const
	{
		if (SkeletalMeshComponent)
		{
			return SkeletalMeshComponent->Bounds.GetBox();
		}

		if (GeomCacheComponent)
		{
			return GeomCacheComponent->Bounds.GetBox();
		}

		FBox Box;
		Box.Init();
		return Box;
	}
}	// namespace UE::NeuralMorphModel
