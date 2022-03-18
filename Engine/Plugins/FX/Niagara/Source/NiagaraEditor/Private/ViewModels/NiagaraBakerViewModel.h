// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "SceneView.h"
#include "Templates/SharedPointer.h"
#include "UObject/GCObject.h"

#include "NiagaraBakerSettings.h"

enum class ENiagaraBakerColorChannel : uint8
{
	Red,
	Green,
	Blue,
	Alpha,
	Num
};

class FNiagaraBakerViewModel : public TSharedFromThis<FNiagaraBakerViewModel>, public FGCObject
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnCurrentOutputChanged);

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

	void SetDisplayTimeFromNormalized(float NormalizeTime);

	class UNiagaraComponent* GetPreviewComponent() const;
	class UNiagaraBakerSettings* GetBakerSettings() const;
	const class UNiagaraBakerSettings* GetBakerGeneratedSettings() const;

	bool RenderView(const FRenderTarget* RenderTarget, FCanvas* Canvas, float WorldTime, int32 iOutputTextureIndex, bool bFillCanvas = false) const;

	bool IsChannelEnabled(ENiagaraBakerColorChannel Channel) const { return bColorChannelEnabled[int(Channel)]; }
	void ToggleChannelEnabled(ENiagaraBakerColorChannel Channel) { bColorChannelEnabled[int(Channel)] = !bColorChannelEnabled[int(Channel)]; }
	void SetChannelEnabled(ENiagaraBakerColorChannel Channel, bool bEnabled) { bColorChannelEnabled[int(Channel)] = bEnabled; }

	bool ShowRealtimePreview() const { return bShowRealtimePreview; }
	void ToggleRealtimePreview() { bShowRealtimePreview = !bShowRealtimePreview; }

	bool ShowBakedView() const { return bShowBakedView; }
	void ToggleBakedView() { bShowBakedView = !bShowBakedView; }

	bool IsCheckerboardEnabled() const { return bCheckerboardEnabled; }
	void ToggleCheckerboardEnabled() { bCheckerboardEnabled = !bCheckerboardEnabled; }

	bool ShowInfoText() const { return bShowInfoText; }
	void ToggleInfoText() { bShowInfoText = !bShowInfoText; }

	void SetCameraViewMode(ENiagaraBakerViewMode ViewMode);
	bool IsCameraViewMode(ENiagaraBakerViewMode ViewMode);

	FText GetCurrentCameraModeText() const;
	FName GetCurrentCameraModeIconName() const;
	FSlateIcon GetCurrentCameraModeIcon() const;

	static FText GetCameraModeText(ENiagaraBakerViewMode Mode);
	static FName GetCameraModeIconName(ENiagaraBakerViewMode Mode);
	static FSlateIcon GetCameraModeIcon(ENiagaraBakerViewMode Mode);

	FVector GetCurrentCameraLocation() const;
	void SetCurrentCameraLocation(const FVector Value);
	FRotator GetCurrentCameraRotation() const;
	void SetCurrentCameraRotation(const FRotator Value) const;

	float GetCameraFOV() const;
	void SetCameraFOV(float InFOV);

	float GetCameraOrbitDistance() const;
	void SetCameraOrbitDistance(float InOrbitDistance);

	float GetCameraOrthoWidth() const;
	void SetCameraOrthoWidth(float InOrthoWidth);

	void ToggleCameraAspectRatioEnabled();
	bool IsCameraAspectRatioEnabled() const;
	float GetCameraAspectRatio() const;
	void SetCameraAspectRatio(float InAspectRatio);

	void AddOutput();
	void RemoveCurrentOutput();
	bool CanRemoveCurrentOutput() const;
	bool IsCurrentOutputIndex(int32 OutputIndex) const { return OutputIndex == CurrentOutputIndex; }
	int32 GetCurrentOutputIndex() const { return CurrentOutputIndex; }
	void SetCurrentOutputIndex(int32 OutputIndex);
	FText GetOutputText(int32 OutputIndex) const;
	FText GetCurrentOutputText() const;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FNiagaraBakerViewModel");
	}

	FOnCurrentOutputChanged OnCurrentOutputChanged;

private:
	class UNiagaraComponent* PreviewComponent = nullptr;
	TSharedPtr<class FAdvancedPreviewScene> AdvancedPreviewScene;

	TWeakPtr<FNiagaraSystemViewModel> WeakSystemViewModel;
	TSharedPtr<class SNiagaraBakerWidget> Widget;

	int32 CurrentOutputIndex = 0;

	bool bShowRealtimePreview = true;
	bool bShowBakedView = true;
	bool bCheckerboardEnabled = true;		//-TODO: Move to Baker Settings?
	bool bShowInfoText = true;				//-TODO: Remove later
	bool bColorChannelEnabled[int(ENiagaraBakerColorChannel::Num)] = {true, true, true, false};
};
