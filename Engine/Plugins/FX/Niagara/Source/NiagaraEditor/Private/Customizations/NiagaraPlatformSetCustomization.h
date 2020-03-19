// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "NiagaraPlatformSet.h"
#include "Layout/Visibility.h"
#include "Widgets/Views/STreeView.h"

class FDetailWidgetRow;
class IPropertyHandleArray;
class ITableRow;
class SMenuAnchor;
class STableViewBase;
class SWrapBox;

enum class ECheckBoxState : uint8;

struct FNiagaraDeviceProfileViewModel
{
	class UDeviceProfile* Profile;
	TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>> Children;
};

class FNiagaraPlatformSetCustomization : public IPropertyTypeCustomization
{
public:
	/** @return A new instance of this class */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraPlatformSetCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	/** IPropertyTypeCustomization interface end */
private:

	void CreateDeviceProfileTree();

	TSharedRef<SWidget> GenerateDeviceProfileTreeWidget(int32 EffectsQuality);
	TSharedRef<ITableRow> OnGenerateDeviceProfileTreeRow(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, const TSharedRef<STableViewBase>& OwnerTable, int32 EffectsQuality);
	void OnGetDeviceProfileTreeChildren(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, TArray< TSharedPtr<FNiagaraDeviceProfileViewModel> >& OutChildren, int32 EffectsQuality);
	FReply RemoveDeviceProfile(UDeviceProfile* Profile, int32 EffectsQuality);
	TSharedRef<SWidget> GetEffectsQualityMenuContents(int32 EffectsQuality) const;

	EVisibility GetDeviceProfileErrorVisibility(UDeviceProfile* Profile, int32 EffectsQuality) const;
	FText GetDeviceProfileErrorToolTip(UDeviceProfile* Profile, int32 EffectsQuality) const;

	FSlateColor GetEffectsQualityButtonTextColor(int32 EffectsQuality) const;
	EVisibility GetEffectsQualityErrorVisibility(int32 EffectsQuality) const;
	FText GetEffectsQualityErrorToolTip(int32 EffectsQuality) const;

	const FSlateBrush* GetProfileMenuButtonImage(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const;
	EVisibility GetProfileMenuButtonVisibility(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const;
	bool GetProfileMenuItemEnabled(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const;
	FText GetProfileMenuButtonToolTip(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const;
	FReply OnProfileMenuButtonClicked(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, int32 EffectsQuality, bool bReopenMenu);

	TSharedRef<SWidget> GetCurrentDeviceProfileSelectionWidget(TSharedPtr<FNiagaraDeviceProfileViewModel> ProfileView);
	TSharedRef<SWidget> OnGenerateDeviceProfileSelectionWidget(TSharedPtr<ENiagaraPlatformSelectionState> InItem);

	FText GetCurrentText() const;
	FText GetTooltipText() const;

	void GenerateEffectsQualitySelectionWidgets();
	TSharedRef<SWidget> GenerateAdditionalDevicesWidgetForEQ(int32 EffectsQuality);

	bool IsTreeActiveForEQ(const TSharedPtr<FNiagaraDeviceProfileViewModel>& Tree, int32 EffectsQualityMask) const;
	void FilterTreeForEQ(const TSharedPtr<FNiagaraDeviceProfileViewModel>& SourceTree, TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredTree, int32 EffectsQualityMask);

	ECheckBoxState IsEQChecked(int32 EffectsQuality) const;
	void EQCheckStateChanged(ECheckBoxState CheckState, int32 EffectsQuality);

	FReply ToggleMenuOpenForEffectsQuality(int32 EffectsQuality);

	void UpdateCachedConflicts();
	void InvalidateSiblingConflicts() const;
	void OnPropertyValueChanged();

private:
	TArray<FNiagaraPlatformSetConflictInfo> CachedConflicts;
	TSharedPtr<IPropertyHandleArray> PlatformSetArray;
	int32 PlatformSetArrayIndex;

	TArray<TSharedPtr<SMenuAnchor>> EffectsQualityMenuAnchors;
	TArray<TSharedPtr<SWidget>> EffectsQualityMenuContents;
	TSharedPtr<SWrapBox> EffectsQualityWidgetBox;

	TSharedPtr<IPropertyHandle> PropertyHandle;
	struct FNiagaraPlatformSet* TargetPlatformSet;
	class UNiagaraSystem* BaseSystem;

	TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>> FullDeviceProfileTree;
	TArray<TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>>> FilteredDeviceProfileTrees;

	TArray<TSharedPtr<ENiagaraPlatformSelectionState>> PlatformSelectionStates;

	TSharedPtr<STreeView<TSharedPtr<FNiagaraDeviceProfileViewModel>>> DeviceProfileTreeWidget;

	struct FNiagaraSystemScalabilitySettingsArray* SystemScalabilitySettings;
	struct FNiagaraEmitterScalabilitySettingsArray* EmitterScalabilitySettings;
	struct FNiagaraSystemScalabilityOverrides* SystemScalabilityOverrides;
	struct FNiagaraEmitterScalabilityOverrides* EmitterScalabilityOverrides;
};
