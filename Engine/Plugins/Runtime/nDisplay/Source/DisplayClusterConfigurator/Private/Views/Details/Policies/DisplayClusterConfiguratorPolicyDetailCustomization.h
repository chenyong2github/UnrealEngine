// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/Details/DisplayClusterConfiguratorDetailCustomization.h"

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Types/SlateEnums.h"


class FPolicyParameterInfo;
class UDisplayClusterBlueprint;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationViewport;

/**
 * Projection Type Customization
 */
class FDisplayClusterConfiguratorProjectionCustomization final
	: public FDisplayClusterConfiguratorPolymorphicEntityCustomization
{
protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

	// Projection Policy Selection
protected:
	EVisibility GetCustomRowsVisibility() const;
	TSharedRef<SWidget> MakeProjectionPolicyOptionComboWidget(TSharedPtr<FString> InItem);
	void ResetProjectionPolicyOptions();
	void AddProjectionPolicyRow();
	void AddCustomPolicyRow();
	void OnProjectionPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo);
	FText GetSelectedProjectionPolicyText() const;
	FText GetCustomPolicyText() const;
	/** Return either custom policy or selected policy. */
	const FString& GetCurrentPolicy() const;
	bool IsCustomTypeInConfig() const;
	void OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType);
	bool IsPolicyIdenticalAcrossEditedObjects(bool bRequireCustomPolicy = false) const;
private:
	TSharedPtr<FString>	CustomOption;
	TArray< TSharedPtr<FString> > ProjectionPolicyOptions;
	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> ProjectionPolicyComboBox;
	TSharedPtr<SEditableTextBox> CustomPolicyRow;
	FString CurrentSelectedPolicy;
	FString CustomPolicy;
	bool bIsCustomPolicy = false;
	// End Projection Policy Selection

	// Custom Parameters Selection
private:
	/** Given a policy create all parameters. */
	void BuildParametersForPolicy(const FString& Policy);

	void CreateSimplePolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateCameraPolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateMeshPolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateDomePolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateVIOSOPolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateEasyBlendPolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateManualPolicy(UDisplayClusterBlueprint* Blueprint);
	void CreateMPCDIPolicy(UDisplayClusterBlueprint* Blueprint);

private:
	/** All custom parameters for the selected policy. */
	TArray<TSharedPtr<FPolicyParameterInfo>> CustomPolicyParameters;
	// End Custom Parameters Selection
	
private:
	TArray<TWeakObjectPtr<UDisplayClusterConfigurationViewport>> ConfigurationViewports;
	TWeakObjectPtr<UDisplayClusterConfigurationViewport> ConfigurationViewportPtr;
};