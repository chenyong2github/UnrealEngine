// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaEditorModelActor.h"
#include "GeometryCacheComponent.h"
#include "Animation/DebugSkelMeshComponent.h"

namespace UE::LegacyVertexDeltaModel
{
	using namespace UE::MLDeformer;

	FLegacyVertexDeltaEditorModelActor::FLegacyVertexDeltaEditorModelActor(const FConstructSettings& Settings)
		: FMLDeformerEditorActor(Settings)
	{
	}

	FLegacyVertexDeltaEditorModelActor::~FLegacyVertexDeltaEditorModelActor()
	{
		if (GeomCacheComponent)
		{
			Actor->RemoveOwnedComponent(GeomCacheComponent);
		}
	}

	void FLegacyVertexDeltaEditorModelActor::SetVisibility(bool bIsVisible)
	{
		FMLDeformerEditorActor::SetVisibility(bIsVisible);

		if (GeomCacheComponent && bIsVisible != GeomCacheComponent->IsVisible())
		{
			GeomCacheComponent->SetVisibility(bIsVisible, true);
		}
	}

	bool FLegacyVertexDeltaEditorModelActor::IsVisible() const
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

	void FLegacyVertexDeltaEditorModelActor::SetPlayPosition(float TimeInSeconds, bool bAutoPause)
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

	float FLegacyVertexDeltaEditorModelActor::GetPlayPosition() const
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

	void FLegacyVertexDeltaEditorModelActor::SetPlaySpeed(float PlaySpeed)
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

	void FLegacyVertexDeltaEditorModelActor::Pause(bool bPaused)
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

	FBox FLegacyVertexDeltaEditorModelActor::GetBoundingBox() const
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
}	// namespace UE::LegacyVertexDeltaModel
