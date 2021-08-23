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
class FDisplayClusterConfiguratorBlueprintEditor;
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
class UDisplayClusterConfigurationClusterNode;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfigurationSceneComponentMesh;
class UDisplayClusterScreenComponent;
class ADisplayClusterRootActor;

template<typename NumericType>
class SSpinBox;

template<typename OptionType>
class SComboBox;

/**
 * Base UCLASS Detail Customization
 */
class FDisplayClusterConfiguratorDetailCustomization
	: public IDetailCustomization
{
public:
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

	ADisplayClusterRootActor* GetRootActor() const;
	UDisplayClusterConfigurationData* GetConfigData() const;

public:
	template<typename TDetailCustomizationType>
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<TDetailCustomizationType>();
	}

protected:
	void AddCustomInfoRow(IDetailCategoryBuilder* InCategory, TAttribute<FText> NameContentAttribute, TAttribute<FText> ValueContentAttribute);

	/** True while the details is customizing a blueprint editor menu. */
	bool IsRunningForBlueprintEditor() const { return ToolkitPtr.IsValid(); }

protected:
	TWeakPtr<FDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr = nullptr;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorPtr;
};

/**
 * Main configuration data container Detail Customization
 */
class FDisplayClusterConfiguratorDataDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	using Super = FDisplayClusterConfiguratorDetailCustomization;

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */
};

/**
 * Cluster Detail Customization
 */
class FDisplayClusterConfiguratorClusterDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	using Super = FDisplayClusterConfiguratorDetailCustomization;

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

protected:
	TSharedPtr<IPropertyHandle> ClusterNodesHandle;
};

class FDisplayClusterConfiguratorViewportDetailCustomization final
	: public FDisplayClusterConfiguratorDetailCustomization
{
public:
	using Super = FDisplayClusterConfiguratorDetailCustomization;

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	/** End IDetailCustomization interface */

private:
	void ResetCameraOptions();
	TSharedRef<SWidget> CreateCustomCameraWidget();
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

struct FDisplayClusterConfiguratorAspectRatioPresetSize
{
public:
	FText DisplayName;
	FVector2D Size;

	FDisplayClusterConfiguratorAspectRatioPresetSize() :
		DisplayName(FText::GetEmpty()),
		Size(FVector2D::ZeroVector)
	{ }

	FDisplayClusterConfiguratorAspectRatioPresetSize(FText InDisplayName, FVector2D InSize) :
		DisplayName(InDisplayName),
		Size(InSize)
	{ }

	bool operator==(const FDisplayClusterConfiguratorAspectRatioPresetSize& Other) const
	{
		return (DisplayName.EqualTo(Other.DisplayName)) && (Size == Other.Size);
	}

	double GetAspectRatio() const { return (double)Size.X / (double)Size.Y; }

public:
	static const TArray<FDisplayClusterConfiguratorAspectRatioPresetSize> CommonPresets;
	static const int32 DefaultPreset;
};

/**
 * Screen Component Detail Customization
 */
class FDisplayClusterConfiguratorScreenDetailCustomization final
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FDisplayClusterConfiguratorScreenDetailCustomization>();
	}
	
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	FText GetPresetsComboBoxSelectedText() const;
	FText GetPresetDisplayText(const TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>& Preset) const;
	FText GetSelectedPresetDisplayText(const TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>& Preset) const;
	void OnSelectedPresetChanged(TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize> SelectedPreset, ESelectInfo::Type SelectionType);
	void GetAspectRatioAndSetDefaultValueForPreset(const FDisplayClusterConfiguratorAspectRatioPresetSize& Preset, FVector2D* OutAspectRatio = nullptr);

	void OnSizePropertyChanged();
private:
	UDisplayClusterScreenComponent* ScreenComponentPtr = nullptr;
	TArray<TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>> PresetItems;
	TSharedPtr<SComboBox<TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>>> PresetsComboBox;
	TSharedPtr<IPropertyHandle> SizeHandlePtr;
	bool bIsCustomAspectRatio = false;
};

/**
 * Base USTRUCT Type Customization
 */
class FDisplayClusterConfiguratorTypeCustomization
	: public IPropertyTypeCustomization
{
public:
	FDisplayClusterConfiguratorTypeCustomization() : EditingObject(nullptr)
	{}

	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	//~ IPropertyTypeCustomization interface end

	template<typename TTypeCustomizationType>
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<TTypeCustomizationType>();
	}

protected:
	void RefreshBlueprint();
	void ModifyBlueprint();
	ADisplayClusterRootActor* FindRootActor() const;

protected:
	TWeakObjectPtr<UObject> EditingObject;
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	TWeakPtr<IPropertyUtilities> PropertyUtilities;
};

/**
 * Cluster Sync Type Customization
 */
class FDisplayClusterConfiguratorClusterSyncTypeCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
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
protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

protected:
	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> ParametersHandle;
	TSharedPtr<IPropertyHandle> IsCustomHandle;
	
	IDetailChildrenBuilder* ChildBuilder = nullptr;
};

/**
 * Render Sync Type Customization
 */
class FDisplayClusterConfiguratorRenderSyncPolicyCustomization final
	: public FDisplayClusterConfiguratorPolymorphicEntityCustomization
{
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

	int32 GetPolicyTypeIndex(const FString& Type) const;

	void OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType);
	
	void AddToParameterMap(const FString& Key, const FString& Value);
	void RemoveFromParameterMap(const FString& Key);
private:
	TSharedPtr<FString>	NvidiaOption;

	TSharedPtr<FString>	CustomOption;

	TWeakObjectPtr<UDisplayClusterConfigurationCluster> ConfigurationClusterPtr;

	TArray< TSharedPtr< FString > >	RenderSyncPolicyOptions;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> RenderSyncPolicyComboBox;

	TSharedPtr<SSpinBox<int32>> SwapGroupSpinBox;

	TSharedPtr<SSpinBox<int32>> SwapBarrierSpinBox;

	int32 SwapGroupValue = 0;

	int32 SwapBarrierValue = 0;

	TSharedPtr<SEditableTextBox> CustomPolicyRow;

	bool bIsCustomPolicy = false;

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

class FDisplayClusterConfiguratorExternalImageTypeCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	TWeakObjectPtr<UDisplayClusterConfigurationClusterNode> ClusterNodePtr;
	TSharedPtr<IPropertyHandle> ImagePathHandle;
};


/**
 * Component Ref Type Customization (Prevents struct from expanding)
 */
class FDisplayClusterConfiguratorComponentRefCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end
};

/**
 * Viewport or Cluster Node Array Selection
 */
class FDisplayClusterConfiguratorNodeSelection : public TSharedFromThis<FDisplayClusterConfiguratorNodeSelection>
{
public:
	static const FName NAME_ElementToolTip;

	enum EOperationMode
	{
		Viewports,
		ClusterNodes
	};

	FDisplayClusterConfiguratorNodeSelection(EOperationMode InMode, ADisplayClusterRootActor* InRootActor, FDisplayClusterConfiguratorBlueprintEditor* InToolkitPtr);

	~FDisplayClusterConfiguratorNodeSelection()
	{
		OptionsComboBox.Reset();
		Options.Reset();
	}

	ADisplayClusterRootActor* GetRootActor() const;
	UDisplayClusterConfigurationData* GetConfigData() const;

	void CreateArrayBuilder(const TSharedRef<IPropertyHandle>& InPropertyHandle, IDetailChildrenBuilder& InChildBuilder);

	FDisplayClusterConfiguratorNodeSelection& IsEnabled(const TAttribute<bool>& InIsEnabled)
	{
		IsEnabledAttr = InIsEnabled;
		return *this;
	}

	static EOperationMode GetOperationModeFromProperty(FProperty* Property);

protected:
	void GenerateSelectionWidget(TSharedRef<IPropertyHandle> PropertyHandle, int32 ArrayIndex, IDetailChildrenBuilder& ChildrenBuilder);
	void ResetOptions();
	TSharedRef<SWidget> MakeOptionComboWidget(TSharedPtr<FString> InItem);
	void OnOptionSelected(TSharedPtr<FString> InValue, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InPropertyHandle);
	FText GetSelectedOptionText(TSharedRef<IPropertyHandle> InPropertyHandle) const;

private:
	TAttribute<bool> IsEnabledAttr;

	TSharedPtr<SDisplayClusterConfigurationSearchableComboBox> OptionsComboBox;
	TArray<TSharedPtr<FString>> Options;

	TWeakPtr<FDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr = nullptr;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorPtr;

	EOperationMode OperationMode = ClusterNodes;
};

/**
* OCIO Profiles
*/
class FDisplayClusterConfiguratorOCIOProfileCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
public:
	virtual ~FDisplayClusterConfiguratorOCIOProfileCustomization() override
	{
		NodeSelection.Reset();
	}
	
protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	TSharedPtr<FDisplayClusterConfiguratorNodeSelection> NodeSelection;
	FDisplayClusterConfiguratorNodeSelection::EOperationMode Mode = FDisplayClusterConfiguratorNodeSelection::EOperationMode::Viewports;

	bool bIsDefaultDetailsDisplay = false;
};


/**
* Per viewport Color Grading 
*/
class FDisplayClusterConfiguratorPerViewportColorGradingCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
public:
	virtual ~FDisplayClusterConfiguratorPerViewportColorGradingCustomization() override
	{
		NodeSelection.Reset();
	}

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	TSharedPtr<FDisplayClusterConfiguratorNodeSelection> NodeSelection;
};

/**
* Per Node Color Grading
*/
class FDisplayClusterConfiguratorPerNodeColorGradingCustomization final
	: public FDisplayClusterConfiguratorTypeCustomization
{
public:
	virtual ~FDisplayClusterConfiguratorPerNodeColorGradingCustomization() override
	{
		NodeSelection.Reset();
	}

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	TSharedPtr<FDisplayClusterConfiguratorNodeSelection> NodeSelection;
};


#undef CONSTRUCT_CUSTOMIZATION