// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "IDetailCustomization.h"

class IDetailsView;
class IPropertyHandle;
class SScrollBox;
class FFractureToolContext;
class FGeometryCollection;
class SGeometryCollectionOutliner;
class SGeometryCollectionHistogram;
class AGeometryCollectionActor;
class UGeometryCollectionComponent;
class UGeometryCollection;
class FFractureEditorModeToolkit;
class UFractureActionTool;
class UFractureModalTool;

namespace GeometryCollection
{
enum class ESelectionMode: uint8;
}

namespace Chaos
{
	template<class T, int d>
	class TParticles;
}

class FFractureViewSettingsCustomization : public IDetailCustomization
{
public:
	FFractureViewSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

class FHistogramSettingsCustomization : public IDetailCustomization
{
public:
	FHistogramSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

class FOutlinerSettingsCustomization : public IDetailCustomization
{
public:
	FOutlinerSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

class FRACTUREEDITOR_API FFractureEditorModeToolkit : public FModeToolkit, public FGCObject
{
public:

	using FGeometryCollectionPtr = TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>;

	FFractureEditorModeToolkit();
	~FFractureEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	void ExecuteAction(UFractureActionTool* InActionTool);
	bool CanExecuteAction(UFractureActionTool* InActionTool) const;

	void SetActiveTool(UFractureModalTool* InActiveTool);
	UFractureModalTool* GetActiveTool() const;
	bool IsActiveTool(UFractureModalTool* InActiveTool);

	void SetOutlinerComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents);
	void SetBoneSelection(UGeometryCollectionComponent* InRootComponent, const TArray<int32>& InSelectedBones, bool bClearCurrentSelection);

	// View Settings
	float GetExplodedViewValue() const;
	int32 GetLevelViewValue() const;
	bool GetShowBoneColors() const;
	void OnSetExplodedViewValue(float NewValue);
	void OnSetLevelViewValue(int32 NewValue);
	void OnToggleShowBoneColors();
	void OnSetShowBoneColors(bool NewValue);

	void OnExplodedViewValueChanged();
	void OnLevelViewValueChanged();

	// Update any View Property Changes 
	void OnObjectPostEditChange( UObject* Object, FPropertyChangedEvent& PropertyChangedEvent );

	TSharedRef<SWidget> GetLevelViewMenuContent(TSharedRef<IPropertyHandle> PropertyHandle);
	TSharedRef<SWidget> GetViewMenuContent();

	void ViewUpOneLevel();
	void ViewDownOneLevel();

	// Modal Command Callback
	FReply OnModalClicked();
	bool CanExecuteModal() const;

	// Filter callbacks
	FReply ResetHistogramSelection();
	bool CanResetFilter() const;

	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);

	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	int32 GetLevelCount();

	FText GetStatisticsSummary() const;

	/** Returns the number of Mode specific tabs in the mode toolbar **/ 
	const static TArray<FName> PaletteNames;
	virtual void GetToolPaletteNames( TArray<FName>& InPaletteName ) const { InPaletteName = PaletteNames; }
	virtual FText GetToolPaletteDisplayName(FName PaletteName) const; 

	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);
	virtual void OnToolPaletteChanged(FName PaletteName) override;

	/** Modes Panel Header Information **/
	virtual FText GetActiveToolDisplayName() const;
	virtual FText GetActiveToolMessage() const;

	void UpdateExplodedVectors(UGeometryCollectionComponent* GeometryCollectionComponent) const;

	void RegenerateOutliner();
	void RegenerateHistogram();

	TSharedPtr<SWidget> ExplodedViewWidget;
	TSharedPtr<SWidget> LevelViewWidget;
	TSharedPtr<SWidget> ShowBoneColorsWidget;

protected:
	static bool IsGeometryCollectionSelected();
	static bool IsSelectedActorsInEditorWorld();	

private:
	static void UpdateGeometryComponentAttributes(UGeometryCollectionComponent* Component);
	static void UpdateVolumes(FGeometryCollectionPtr GeometryCollection, const Chaos::TParticles<float, 3>& MassSpaceParticles, int32 TransformIndex);

	void OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void OnHistogramBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void BindCommands();

private:
	UFractureModalTool* ActiveTool;

	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<IDetailsView> ViewSettingsDetailsView;
	TSharedPtr<IDetailsView> HistogramDetailsView;
	TSharedPtr<IDetailsView> OutlinerDetailsView;
	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<SGeometryCollectionOutliner> OutlinerView;
	TSharedPtr<SGeometryCollectionHistogram> HistogramView;

};
