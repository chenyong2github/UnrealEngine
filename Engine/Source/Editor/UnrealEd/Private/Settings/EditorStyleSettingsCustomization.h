// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Styling/StyleColors.h"

class IDetailLayoutBuilder;
class STextComboBox;
class IDetailPropertyRow;

#if ALLOW_THEMES

DECLARE_DELEGATE_OneParam(FOnThemeEditorClosed, bool)

class FStyleColorListCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
private:
	void OnResetColorToDefault(TSharedPtr<IPropertyHandle> Handle, EStyleColor Color);
	bool IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> Handle, EStyleColor Color);
};

class FEditorStyleSettingsCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	
	void RefreshComboBox();
private:
	void GenerateThemeOptions(TSharedPtr<FString>& OutSelectedTheme);

	void MakeThemePickerRow(IDetailPropertyRow& PropertyRow);
	FReply OnDeleteThemeClicked();
	FReply OnDuplicateAndEditThemeClicked();
	FReply OnEditThemeClicked();
	FString GetTextLabelForThemeEntry(TSharedPtr<FString> Entry);
	void OnThemePicked(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OpenThemeEditorWindow(FOnThemeEditorClosed OnThemeEditorClosed);
	bool IsThemeEditingEnabled() const;
private:
	TArray<TSharedPtr<FString>> ThemeOptions;
	TSharedPtr<STextComboBox> ComboBox;
};
#endif
