// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Types/SlateEnums.h"
#include "UObject/WeakObjectPtr.h"

class FDisplayClusterConfiguratorDetailCustomization;
class FDisplayClusterConfiguratorToolkit;
class IDetailCategoryBuilder;
class IPropertyHandle;
class SDisplayClusterConfigurationSearchableComboBox;
class SEditableTextBox;
class SWidget;
class UDisplayClusterConfigurationData;
class UDisplayClusterConfigurationCluster;
class UDisplayClusterConfigurationInput;
class UDisplayClusterConfigurationScene;
class UDisplayClusterConfigurationSceneComponent;
class UDisplayClusterConfigurationSceneComponentMesh;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfigurationSceneComponentMesh;

template<typename NumericType>
class SSpinBox;

#define CONSTRUCT_CUSTOMIZATION( ChildClass, ParentClass ) \
	typedef ParentClass Super;\
	ChildClass(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr) \
		: ParentClass(InToolkitPtr) \
		{}

/**
 * Base UCLASS Detail Customization
 */
class FDisplayClusterConfiguratorDetailCustomization
	: public IDetailCustomization
{
public:
	FDisplayClusterConfiguratorDetailCustomization(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr);

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

public:
	template<typename TDetailCustomizationType>
	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr)
	{
		return MakeShared<TDetailCustomizationType>(InToolkitPtr);
	}

protected:
	void AddCustomInfoRow(IDetailCategoryBuilder* InCategory, TAttribute<FText> NameContentAttribute, TAttribute<FText> ValueContentAttribute);

protected:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	IDetailLayoutBuilder* LayoutBuilder;

	IDetailCategoryBuilder* NDisplayCategory;
};

/**
 * Main configuration data container Detail Customization
 */
class FDisplayClusterConfiguratorDataDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorDataDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */
};

/**
 * Scene Detail Customization
 */
class FDisplayClusterConfiguratorSceneDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

private:
	TWeakObjectPtr<UDisplayClusterConfigurationScene> ConfigurationScenePtr;
};

/**
 * Cluster Detail Customization
 */
class FDisplayClusterConfiguratorClusterDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorClusterDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */
};

class FDisplayClusterConfiguratorViewportDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorViewportDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

private:
	void ResetCameraOptions();

	void AddCameraRow();

	TSharedRef<SWidget> MakeCameraOptionComboWidget(TSharedPtr<FString> InItem);

	void OnCameraSelected(TSharedPtr<FString> InCamera, ESelectInfo::Type SelectInfo);

	FText GetSelectedCameraText() const;

private:
	TArray< TSharedPtr< FString > >	CameraOptions;

	TSharedPtr<IPropertyHandle> CameraHandle;

	TSharedPtr<FString>	NoneOption;

	TWeakObjectPtr<UDisplayClusterConfigurationViewport> ConfigurationViewportPtr;

	TWeakObjectPtr<UDisplayClusterConfigurationData> ConfigurationDataPtr;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> CameraComboBox;
};

/**
 * Input Detail Customization
 */
class FDisplayClusterConfiguratorInputDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorInputDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

private:
	TWeakObjectPtr<UDisplayClusterConfigurationInput> ConfigurationInputPtr;
};

/**
 * Base Scene Component Detail Customization
 */
class FDisplayClusterConfiguratorSceneComponentDetailCustomization
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneComponentDetailCustomization, FDisplayClusterConfiguratorDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

	virtual void ResetTrackerIdOptions();

protected:

	TSharedRef<SWidget> MakeTrackerIdOptionComboWidget(TSharedPtr<FString> InItem);

	void OnTrackerIdSelected(TSharedPtr<FString> ITrackerId, ESelectInfo::Type SelectInfo);

	void AddTrackerIdRow();

	FText GetSelectedTrackerIdText() const;

	EVisibility GetLocationAndRotationVisibility() const;

protected:
	TWeakObjectPtr<UDisplayClusterConfigurationSceneComponent> SceneComponenPtr;

	TArray< TSharedPtr< FString > >	TrackerIdOptions;

	TSharedPtr<FString>	NoneOption;

	TSharedPtr<IPropertyHandle> TrackerIdHandle;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> TrackerIdComboBox;
};

/**
 * Xform Component Detail Customization
 */
class FDisplayClusterConfiguratorSceneComponentXformDetailCustomization final
	: public FDisplayClusterConfiguratorSceneComponentDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneComponentXformDetailCustomization, FDisplayClusterConfiguratorSceneComponentDetailCustomization)
};

/**
 * Screen Component Detail Customization
 */
class FDisplayClusterConfiguratorSceneComponentScreenDetailCustomization final
	: public FDisplayClusterConfiguratorSceneComponentDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneComponentScreenDetailCustomization, FDisplayClusterConfiguratorSceneComponentDetailCustomization)
};

/**
 * Camera Component Detail Customization
 */
class FDisplayClusterConfiguratorSceneComponentCameraDetailCustomization final
	: public FDisplayClusterConfiguratorSceneComponentDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneComponentCameraDetailCustomization, FDisplayClusterConfiguratorSceneComponentDetailCustomization)
};

/**
 * Mesh Component Detail Customization
 */
class FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization final
	: public FDisplayClusterConfiguratorSceneComponentDetailCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization, FDisplayClusterConfiguratorSceneComponentDetailCustomization)

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

private:
	void OnAssetValueChanged();

private:
	TWeakObjectPtr<UDisplayClusterConfigurationSceneComponentMesh> SceneComponentMeshPtr;

	TSharedPtr<IPropertyHandle> AssetHandle;
};

/**
 * Base USTRUCT Type Customization
 */
class FDisplayClusterConfiguratorTypeCustomization
	: public IPropertyTypeCustomization
{
public:
	FDisplayClusterConfiguratorTypeCustomization(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr)
		: ToolkitPtr(InToolkitPtr)
	{}

	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	//~ IPropertyTypeCustomization interface end

	template<typename TTypeCustomizationType>
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr)
	{
		return MakeShared<TTypeCustomizationType>(InToolkitPtr);
	}

protected:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};

/**
 * Cluster Sync Type Customization
 */
class FDisplayClusterConfiguratorClusterSyncTypeCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorClusterSyncTypeCustomization, FDisplayClusterConfiguratorTypeCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	TSharedPtr<IPropertyHandle> RenderSyncPolicyHandle;

	TSharedPtr<IPropertyHandle> InputSyncPolicyHandle;

};

/**
 * Polymorphic Entity Type Customization
 */
class FDisplayClusterConfiguratorPolymorphicEntityCustomization
	: public FDisplayClusterConfiguratorTypeCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorPolymorphicEntityCustomization, FDisplayClusterConfiguratorTypeCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

protected:
	TSharedPtr<IPropertyHandle> TypeHandle;

	TSharedPtr<IPropertyHandle> ParametersHandle;

	IDetailChildrenBuilder* ChildBuilder;
};

/**
 * Render Sync Type Customization
 */
class FDisplayClusterConfiguratorRenderSyncPolicyCustomization final
	: public FDisplayClusterConfiguratorPolymorphicEntityCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorRenderSyncPolicyCustomization, FDisplayClusterConfiguratorPolymorphicEntityCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	EVisibility GetCustomRowsVisibility() const;

	EVisibility GetNvidiaPolicyRowsVisibility() const;

	void ResetRenderSyncPolicyOptions();

	void AddRenderSyncPolicyRow();

	void AddNvidiaPolicyRows();

	void AddCustomPolicyRow();

	TSharedRef<SWidget> MakeRenderSyncPolicyOptionComboWidget(TSharedPtr<FString> InItem);

	void OnRenderSyncPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo);

	FText GetSelectedRenderSyncPolicyText() const;

	FText GetCustomPolicyText() const;

	bool IsCustomTypeInConfig() const;

	void OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType);


private:
	TSharedPtr<FString>	NvidiaOption;

	TSharedPtr<FString>	CustomOption;

	TWeakObjectPtr<UDisplayClusterConfigurationCluster> ConfigurationClusterPtr;

	TArray< TSharedPtr< FString > >	RenderSyncPolicyOptions;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> RenderSyncPolicyComboBox;

	TSharedPtr<SSpinBox<int32>> SwapGroupSpinBox;

	TSharedPtr<SSpinBox<int32>> SwapBarrierSpinBox;

	int32 SwapGroupValue;

	int32 SwapBarrierValue;

	TSharedPtr<SEditableTextBox> CustomPolicyRow;

	bool bIsCustomPolicy;

	FString CustomPolicy;

private:
	const static FString SwapGroupName;

	const static FString SwapBarrierName;
};

/**
 * Input Sync Type Customization
 */
class FDisplayClusterConfiguratorInputSyncPolicyCustomization final
	: public FDisplayClusterConfiguratorPolymorphicEntityCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorInputSyncPolicyCustomization, FDisplayClusterConfiguratorPolymorphicEntityCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	void ResetInputSyncPolicyOptions();

	void AddInputSyncPolicyRow();

	TSharedRef<SWidget> MakeInputSyncPolicyOptionComboWidget(TSharedPtr<FString> InItem);

	void OnInputSyncPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo);

	FText GetSelectedInputSyncPolicyText() const;

private:
	TWeakObjectPtr<UDisplayClusterConfigurationCluster> ConfigurationClusterPtr;

	TArray< TSharedPtr< FString > >	InputSyncPolicyOptions;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> InputSyncPolicyComboBox;
};

/**
 * Projection Type Customization
 */
class FDisplayClusterConfiguratorProjectionCustomization final
	: public FDisplayClusterConfiguratorPolymorphicEntityCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FDisplayClusterConfiguratorProjectionCustomization, FDisplayClusterConfiguratorPolymorphicEntityCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	EVisibility GetCustomRowsVisibility() const;

	void ResetProjectionPolicyOptions();

	void AddProjectionPolicyRow();

	void AddCustomPolicyRow();

	TSharedRef<SWidget> MakeProjectionPolicyOptionComboWidget(TSharedPtr<FString> InItem);

	void OnProjectionPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo);

	FText GetSelectedProjectionPolicyText() const;

	FText GetCustomPolicyText() const;

	bool IsCustomTypeInConfig() const;

	void OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType);

private:
	TSharedPtr<FString>	CustomOption;

	TWeakObjectPtr<UDisplayClusterConfigurationViewport> ConfigurationViewportPtr;

	TArray< TSharedPtr< FString > >	ProjectionPolicyOptions;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> ProjectionPolicyComboBox;

	TSharedPtr<SEditableTextBox> CustomPolicyRow;

	bool bIsCustomPolicy;

	FString CustomPolicy;
};

#undef CONSTRUCT_CUSTOMIZATION