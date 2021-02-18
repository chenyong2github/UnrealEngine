// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "SceneView.h"
#include "Templates/SharedPointer.h"
#include "UObject/GCObject.h"

#include "NiagaraFlipbookSettings.h"

class FNiagaraFlipbookViewModel : public TSharedFromThis<FNiagaraFlipbookViewModel>, public FGCObject
{
public:
	struct FGeneratedData
	{
		UTexture2D*	Texture = nullptr;
		int			TotalFrames = 0;
		FIntPoint	FrameSize = FIntPoint::ZeroValue;
		FIntPoint	FramesPerDimension = FIntPoint::ZeroValue;
		float		DurationSeconds = 0.0f;
	};

	struct FDisplayData
	{
		int32		NumFrames = 0;
		float		StartSeconds = 0.0f;
		float		DurationSeconds = 0.0f;
		float		NormalizedTime = 0.0f;
		float		FrameTime = 0.0f;
		int32		FrameIndex = 0;
	};

	FNiagaraFlipbookViewModel();
	~FNiagaraFlipbookViewModel();

	void Initialize(TWeakPtr<class FNiagaraSystemViewModel> WeakSystemViewModel);

	TSharedPtr<class SWidget> GetWidget();

	void RenderFlipbook();

	void RefreshView();

	int32 GetPreviewTextureIndex() const { return PreviewTextureIndex; }
	void SetPreviewTextureIndex(int32 InPreviewTextureIndex) { PreviewTextureIndex = InPreviewTextureIndex; }
	const FNiagaraFlipbookTextureSettings* GetPreviewTexture() const { return GeneratedTextures.IsValidIndex(PreviewTextureIndex) ? &GeneratedTextures[PreviewTextureIndex] : nullptr; }

	bool IsGeneratedDataValid() const { return GeneratedNumFrames > 0; }
	int32 GetGeneratedNumFrames() const { return GeneratedNumFrames; }
	float GetGeneratedStartSeconds() const { return GeneratedStartSeconds; }
	float GetGeneratedDurationSeconds() const { return GeneratedDurationSeconds; }
	FIntPoint GetGeneratedFramesPerDimension() const { return GeneratedFramesPerDimension; }
	TConstArrayView<FNiagaraFlipbookTextureSettings> GetGeneratedTextures() const { return MakeArrayView(GeneratedTextures); }

	FDisplayData GetDisplayDataFromAbsoluteTime(float AbsoluteTime) const;
	FDisplayData GetDisplayDataFromRelativeTime(float RelativeTime) const;

	void SetDisplayTimeFromNormalized(float NormalizeTime);

	class UNiagaraComponent* GetPreviewComponent() const;
	class UNiagaraFlipbookSettings* GetFlipbookSettings() const;

	bool RenderView(const FRenderTarget* RenderTarget, FCanvas* Canvas, float WorldTime, int32 iOutputTextureIndex, bool bFillCanvas = false) const;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	class UNiagaraComponent* PreviewComponent = nullptr;
	TSharedPtr<class FAdvancedPreviewScene> AdvancedPreviewScene;

	TWeakPtr<FNiagaraSystemViewModel> WeakSystemViewModel;
	TSharedPtr<class SNiagaraFlipbookWidget> Widget;

	int32 PreviewTextureIndex = 0;

	int32 GeneratedNumFrames = 0;
	float GeneratedStartSeconds = 0.0f;
	float GeneratedDurationSeconds = 0.0f;
	FIntPoint GeneratedFramesPerDimension = FIntPoint(0, 0);
	TArray<FNiagaraFlipbookTextureSettings> GeneratedTextures;
};
