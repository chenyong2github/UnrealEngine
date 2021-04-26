// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"

#include "IDetailChildrenBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Layout/Visibility.h"
#include "Types/SlateEnums.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"

class SDisplayClusterConfigurationSearchableComboBox;
class FDisplayClusterConfiguratorBlueprintEditor;
class UDisplayClusterBlueprint;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationViewport;
class IDetailChildrenBuilder;

/**
 * Individual policy parameter data base class.
 */
class FPolicyParameterInfo : public TSharedFromThis<FPolicyParameterInfo>
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(EVisibility, FParameterVisible, FText);
	
public:
	FPolicyParameterInfo(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle,
		const FString* InInitialValue = nullptr);

	virtual ~FPolicyParameterInfo() {}

	/** The friendly display name for this parameter. */
	FText GetParameterDisplayName() const { return FText::FromString(ParamDisplayName); }

	/** The keu used for the parameter map. */
	const FString& GetParameterKey() const { return ParamKey; }

	/** Create the widget representation of this parameter and add it as a child row. */
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) = 0;

	/** This delegate will be used to determine visibility. */
	void SetParameterVisibilityDelegate(FParameterVisible InDelegate);

	/** Retrieve (or add) the parameter information from the viewport data object. */
	FText GetOrAddCustomParameterValueText() const;
protected:
	
	/** Checks the data model if the parameter exists. */
	bool IsParameterAlreadyAdded() const;
	
	/** Update the parameter value in the viewport data object. */
	void UpdateCustomParameterValueText(const FString& NewValue, bool bNotify = true) const;

	/** Visibility of this parameter. Uses OnParameterVisibilityCheck to check. */
	virtual EVisibility IsParameterVisible() const;

protected:
	/** Owning blueprint of this policy. */
	TWeakObjectPtr<UDisplayClusterBlueprint> BlueprintOwnerPtr;

	/** Viewport owning policy. */
	TWeakObjectPtr<UDisplayClusterConfigurationViewport> ConfigurationViewportPtr;

	/** BP editor found from BlueprintOwner. */
	FDisplayClusterConfiguratorBlueprintEditor* BlueprintEditorPtrCached;

	/** Optional initial selected item. */
	TSharedPtr<FString> InitialValue;

	/** Used to determine parameter visibility. */
	FParameterVisible OnParameterVisibilityCheck;

	/** Handle to the owning property being set. */
	TSharedPtr<IPropertyHandle> ParametersHandle;

private:
	/** The display name only for the UI. */
	FString ParamDisplayName;
	
	/** The proper title (key) of this parameter. */
	FString ParamKey;
};

/**
 * Policy info for combo box picker with custom values.
 */
class FPolicyParameterInfoCombo : public FPolicyParameterInfo
{
public:
	DECLARE_DELEGATE_OneParam(FOnItemSelected, const FString&)
	
public:
	FPolicyParameterInfoCombo(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle,
		const TArray<FString>& InValues,
		const FString* InInitialItem = nullptr,
		bool bSort = true);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

	/** The combo box containing all of the options.  */
	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox>& GetCustomParameterValueComboBox() { return CustomParameterValueComboBox; }

	/** The options used in the combo box. */
	TArray< TSharedPtr<FString> >& GetCustomParameterOptions() { return CustomParameterOptions; }

	void SetOnSelectedDelegate(FOnItemSelected InDelegate);

protected:
	/** Widget used for each option. */
	TSharedRef<SWidget> MakeCustomParameterValueComboWidget(TSharedPtr<FString> InItem);
	
	/** When a custom parameter value option has been selected. */
	void OnCustomParameterValueSelected(TSharedPtr<FString> InValue, ESelectInfo::Type SelectInfo);

protected:
	/** Current parameters' value combo box. */
	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> CustomParameterValueComboBox;

	/** List of options for the combo value box. */
	TArray< TSharedPtr<FString> > CustomParameterOptions;

	FOnItemSelected OnItemSelected;
};

/**
 * Policy info for combo box picker for actor components.
 */
class FPolicyParameterInfoComponentCombo final : public FPolicyParameterInfoCombo
{
public:
	FPolicyParameterInfoComponentCombo(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle,
		TSubclassOf<UActorComponent> InComponentClass);

	/** Populate CustomParameterOptions based on this parameter. */
	void CreateParameterValues(ADisplayClusterRootActor* RootActor);

	/** The type of component this parameter represents. */
	TSubclassOf<UActorComponent> GetComponentType() const { return ComponentType; }

private:
	/** A component class to use for creating parameter options. */
	TSubclassOf<UActorComponent> ComponentType;
};

/**
 * Policy info for number value.
 */
template<typename T>
class FPolicyParameterInfoNumber final : public FPolicyParameterInfo
{
public:
	FPolicyParameterInfoNumber(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle,
		T InDefaultValue = 0, TOptional<T> InMinValue = TOptional<T>(), TOptional<T> InMaxValue = TOptional<T>()) : FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle)
	{
		MinValue = InMinValue;
		MaxValue = InMaxValue;

		if (IsParameterAlreadyAdded())
		{
			const FText TextValue = GetOrAddCustomParameterValueText();
			NumberValue = static_cast<T>(FCString::Atof(*TextValue.ToString()));
		}
		else
		{
			NumberValue = InDefaultValue;
		}
	}

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override
	{
		InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(GetParameterDisplayName())
			]
			.ValueContent()
			[
				SNew(SSpinBox<T>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.Value_Lambda([this]()
				{
					return NumberValue;
				})
				.OnValueChanged_Lambda([this](T InValue)
				{
					NumberValue = InValue;
					UpdateCustomParameterValueText(FString::SanitizeFloat(static_cast<float>(NumberValue)));
				})
			];
	}
	// ~FPolicyParameterInfo

private:
	TOptional<T> MinValue;
	TOptional<T> MaxValue;
	T NumberValue;
};

/**
 * Policy info for text value.
 */
class FPolicyParameterInfoText final : public FPolicyParameterInfo
{
public:
	FPolicyParameterInfoText(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo
};

/**
 * Policy info for bool value.
 */
class FPolicyParameterInfoBool final : public FPolicyParameterInfo
{
public:
	FPolicyParameterInfoBool(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle
		);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

private:
	ECheckBoxState IsChecked() const;
};

/**
 * Policy info for file picker.
 */
class FPolicyParameterInfoFile final : public FPolicyParameterInfo
{
public:
	FPolicyParameterInfoFile(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle,
		const TArray<FString>& InFileExtensions) : FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle)
	{
		FileExtensions = InFileExtensions;
	}

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

private:
	/** Prompts user to select a file. */
	FString OpenSelectFileDialogue();
	FReply OnChangePathClicked();

private:
	TArray<FString> FileExtensions;
};

/**
 * Policy info for modifying float reference, typically in a vector or matrix.
 */
class FPolicyParameterInfoFloatReference : public FPolicyParameterInfo
{
public:
	FPolicyParameterInfoFloatReference(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle) : FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle)
	{
	}

protected:
	virtual void FormatTextAndUpdateParameter() = 0;
	
	TSharedRef<SWidget> MakeFloatInputWidget(TSharedRef<float>& ProxyValue, const FText& Label, bool bRotationInDegrees,
	                                         const FLinearColor& LabelColor, const FLinearColor& LabelBackgroundColor);

	TOptional<float> OnGetValue(TSharedRef<float> Value) const
	{
		return Value.Get();
	}

	void OnValueCommitted(float NewValue, ETextCommit::Type CommitType, TSharedRef<float> Value);
};

/**
 * Policy info for matrix.
 */
class FPolicyParameterInfoMatrix final : public FPolicyParameterInfoFloatReference
{
public:
	FPolicyParameterInfoMatrix(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

private:
	void CustomizeLocation(FDetailWidgetRow& InDetailWidgetRow);
	void CustomizeRotation(FDetailWidgetRow& InDetailWidgetRow);
	void CustomizeScale(FDetailWidgetRow& InDetailWidgetRow);
	virtual void FormatTextAndUpdateParameter() override;

private:
	mutable TSharedRef<float> CachedTranslationX;
	mutable TSharedRef<float> CachedTranslationY;
	mutable TSharedRef<float> CachedTranslationZ;

	mutable TSharedRef<float> CachedRotationYaw;
	mutable TSharedRef<float> CachedRotationPitch;
	mutable TSharedRef<float> CachedRotationRoll;

	mutable TSharedRef<float> CachedScaleX;
	mutable TSharedRef<float> CachedScaleY;
	mutable TSharedRef<float> CachedScaleZ;
};

/**
 * Policy info for rotator.
 */
class FPolicyParameterInfoRotator final : public FPolicyParameterInfoFloatReference
{
public:
	FPolicyParameterInfoRotator(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

private:
	virtual void FormatTextAndUpdateParameter() override;

private:
	mutable TSharedRef<float> CachedRotationYaw;
	mutable TSharedRef<float> CachedRotationPitch;
	mutable TSharedRef<float> CachedRotationRoll;
};

/**
 * Policy info for frustum angle.
 */
class FPolicyParameterInfoFrustumAngle final : public FPolicyParameterInfoFloatReference
{
public:
	FPolicyParameterInfoFrustumAngle(
		const FString& InDisplayName,
		const FString& InKey,
		UDisplayClusterBlueprint* InBlueprint,
		UDisplayClusterConfigurationViewport* InConfigurationViewport,
		const TSharedPtr<IPropertyHandle>& InParametersHandle);

	// FPolicyParameterInfo
	virtual void CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow) override;
	// ~FPolicyParameterInfo

private:
	virtual void FormatTextAndUpdateParameter() override;

private:
	mutable TSharedRef<float> CachedAngleL;
	mutable TSharedRef<float> CachedAngleR;
	mutable TSharedRef<float> CachedAngleT;
	mutable TSharedRef<float> CachedAngleB;
};