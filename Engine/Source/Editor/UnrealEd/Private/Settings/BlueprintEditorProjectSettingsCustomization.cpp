// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorProjectSettingsCustomization.h"
#include "BlueprintEditorSettings.h"
#include "Settings/BlueprintEditorProjectSettings.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"

#define LOCTEXT_NAMESPACE "FBlueprintEditorProjectSettingsCustomization"

TSharedRef<IDetailCustomization> FBlueprintEditorProjectSettingsCustomization::MakeInstance()
{
	return MakeShared<FBlueprintEditorProjectSettingsCustomization>();
}

FBlueprintEditorProjectSettingsCustomization::~FBlueprintEditorProjectSettingsCustomization()
{
}

void FBlueprintEditorProjectSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& LayoutBuilder)
{
	// Add a custom edit condition on the project-specific 'NamespacesToAlwaysInclude' member to link it to the editor's namespace feature toggle flag (for consistency w/ the editor-specific set).
	static FName PropertyName_NamespacesToAlwaysInclude = GET_MEMBER_NAME_CHECKED(UBlueprintEditorProjectSettings, NamespacesToAlwaysInclude);
	TSharedRef<IPropertyHandle> PropertyHandle_NamespacesToAlwaysInclude = LayoutBuilder.GetProperty(PropertyName_NamespacesToAlwaysInclude);
	if (IDetailPropertyRow* PropertyRow_NamespacesToAlwaysInclude = LayoutBuilder.EditDefaultProperty(PropertyHandle_NamespacesToAlwaysInclude))
	{
		PropertyRow_NamespacesToAlwaysInclude->EditCondition(
			TAttribute<bool>::CreateLambda([]()
			{
				return GetDefault<UBlueprintEditorSettings>()->bEnableNamespaceEditorFeatures;
			}),
			FOnBooleanValueChanged()
		);
	}
}

#undef LOCTEXT_NAMESPACE
