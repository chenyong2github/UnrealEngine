// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "SceneView.h"
#include "Templates/SharedPointer.h"
#include "UObject/GCObject.h"

#include "NiagaraBakerSettings.h"

class FNiagaraBakerViewModel : public TSharedFromThis<FNiagaraBakerViewModel>, public FGCObject
{
public:
	struct FDisplayData
	{
		int32		NumFrames = 0;
		float		StartSeconds = 0.0f;
		float		DurationSeconds = 0.0f;
		float		NormalizedTime = 0.0f;
		float		FrameTime = 0.0f;
		int32		FrameIndex = 0;
	};

	FNiagaraBakerViewModel();
	~FNiagaraBakerViewModel();

	void Initialize(TWeakPtr<class FNiagaraSystemViewModel> WeakSystemViewModel);

	TSharedPtr<class SWidget> GetWidget();

	void RenderBaker();

	void RefreshView();

	int32 GetPreviewTextureIndex() const { return PreviewTextureIndex; }
	void SetPreviewTextureIndex(int32 InPreviewTextureIndex) { PreviewTextureIndex = InPreviewTextureIndex; }

	void SetDisplayTimeFromNormalized(float NormalizeTime);

	class UNiagaraComponent* GetPreviewComponent() const;
	class UNiagaraBakerSettings* GetBakerSettings() const;
	const class UNiagaraBakerSettings* GetBakerGeneratedSettings() const;

	const struct FNiagaraBakerTextureSettings* GetPreviewTextureSettings() const;

	bool RenderView(const FRenderTarget* RenderTarget, FCanvas* Canvas, float WorldTime, int32 iOutputTextureIndex, bool bFillCanvas = false) const;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	class UNiagaraComponent* PreviewComponent = nullptr;
	TSharedPtr<class FAdvancedPreviewScene> AdvancedPreviewScene;

	TWeakPtr<FNiagaraSystemViewModel> WeakSystemViewModel;
	TSharedPtr<class SNiagaraBakerWidget> Widget;

	int32 PreviewTextureIndex = 0;
};
