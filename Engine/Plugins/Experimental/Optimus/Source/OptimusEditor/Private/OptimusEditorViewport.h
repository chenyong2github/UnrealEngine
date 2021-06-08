// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "AdvancedPreviewScene.h"

class USkeletalMeshSkinCacheDataProvider;
class USkeletalMeshReadDataProvider;
class IOptimusEditor;
class UComputeGraphComponent;
class UMeshComponent;
class FOptimusEditor;
class FOptimusEditorViewportClient;

class SOptimusEditorViewport 
	: public SEditorViewport
	, public FGCObject
	, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SOptimusEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FOptimusEditor> InEditor);
	~SOptimusEditorViewport();

	/// @brief Sets the asset to show in the preview window, either a Static Mesh or a Skeletal 
	/// Mesh.
	/// @param InAsset The asset to show in the preview window. 
	/// @return true if the asset was of the correct type and we were able to successfully 
	/// set it as the preview asset.
	bool SetPreviewAsset(UObject* InAsset);

	void SetOwnerTab(const TSharedRef<SDockTab>& OwnerTab);


	UComputeGraphComponent *GetComputeGraphComponent() const
	{
		return ComputeGraphComponent;
	}
	

	TSharedRef<FAdvancedPreviewScene> GetAdvancedPreviewScene() const
	{
		return AdvancedPreviewScene.ToSharedRef();
	}

	// FGCObject overrides
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	// ICommonEditorViewportToolbarInfoProvider override
	TSharedRef<class SEditorViewport> GetViewportWidget() override;
	TSharedPtr<FExtender> GetExtenders() const override;
	void OnFloatingButtonClicked() override { /* Do nothing */ }

protected:
	// SEditorViewport overrides
	TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

	bool IsVisible() const override;

private:
	// The tab in which this viewport resides. This is used to determine visibility for the 
	// preview viewport.
	TWeakPtr<SDockTab> OwnerTab;

	TWeakPtr<IOptimusEditor> EditorOwner;

	TSharedPtr<FOptimusEditorViewportClient> EditorViewportClient;

	TSharedPtr<FAdvancedPreviewScene> AdvancedPreviewScene;

	UComputeGraphComponent* ComputeGraphComponent = nullptr;
	USkeletalMeshReadDataProvider* SkeletalMeshReadDataProvider = nullptr;
	USkeletalMeshSkinCacheDataProvider *SkeletalMeshSkinCacheDataProvider = nullptr;
	UMeshComponent* PreviewMeshComponent = nullptr;
	UMaterialInterface* PreviewMaterial = nullptr;
};
