// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "AudioParameterControllerInterface.h"
#include "Components/Widget.h"
#include "Delegates/Delegate.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditorSettings.h"
#include "MetasoundDataReference.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundPrimitives.h"
#include "MetasoundVertex.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "MetasoundEditorGraphInputNodes.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetaSound;


UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInputNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputNode() = default;

	UPROPERTY()
	TObjectPtr<UMetasoundEditorGraphInput> Input;

public:
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const;

	virtual FMetasoundFrontendClassName GetClassName() const;

	virtual FGuid GetNodeID() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetNodeTitleIcon() const override;

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override;
#endif // WITH_EDITORONLY_DATA

protected:
	virtual void SetNodeID(FGuid InNodeID) override;


	friend class Metasound::Editor::FGraphBuilder;
};

// Broken out to be able to customize and swap enum behavior for basic integer literal behavior
USTRUCT()
struct FMetasoundEditorGraphInputBoolRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	bool Value = false;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBool : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBool() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputBoolRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBoolArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBoolArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputBoolRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

// Broken out to be able to customize and swap enum behavior for basic integer literal behavior
USTRUCT()
struct FMetasoundEditorGraphInputIntRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	int32 Value = 0;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputInt : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputInt() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputIntRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputIntArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputIntRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

// For input widget
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetasoundInputValueChangedEvent, float);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetasoundRangeChangedEvent, FVector2D);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetasoundInputClampDefaultChangedEvent, bool);

UENUM()
enum class EMetasoundInputWidget : uint8
{
	None,
	Slider, 
	RadialSlider UMETA(DisplayName = "Knob"),
};

UENUM()
enum class EMetasoundInputWidgetValueType : uint8
{
	Linear, 
	Frequency UMETA(DisplayName = "Frequency (Log)"),
	Volume
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputFloat : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, Category = DefaultValue)
	float Default = 0.f;

public:
	virtual ~UMetasoundEditorGraphInputFloat() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue, meta=(EditCondition = "InputWidgetType == EMetasoundInputWidget::None", EditConditionHides))
	bool ClampDefault = false;

	UPROPERTY(EditAnywhere, Category = DefaultValue, meta = (EditCondition = "ClampDefault || InputWidgetType != EMetasoundInputWidget::None", EditConditionHides))
	FVector2D Range = FVector2D(FMath::Min(0.0f, Default), FMath::Max(0.0f, Default));

	UPROPERTY(EditAnywhere, Category = Widget, meta=(DisplayName="Widget Type"))
	EMetasoundInputWidget InputWidgetType = EMetasoundInputWidget::None;
	
	UPROPERTY(EditAnywhere, Category = Widget, meta=(DisplayName="Orientation", EditCondition = "InputWidgetType == EMetasoundInputWidget::Slider", EditConditionHides))
	TEnumAsByte<EOrientation> InputWidgetOrientation = EOrientation::Orient_Horizontal;

	UPROPERTY(EditAnywhere, Category = Widget, meta=(DisplayName="Value Type", EditCondition="InputWidgetType != EMetasoundInputWidget::None", EditConditionHides))
	EMetasoundInputWidgetValueType InputWidgetValueType = EMetasoundInputWidgetValueType::Linear;

	FOnMetasoundInputValueChangedEvent OnDefaultValueChanged;
	FOnMetasoundRangeChangedEvent OnRangeChanged;
	FOnMetasoundInputClampDefaultChangedEvent OnClampInputChanged;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	void SetDefault(const float InDefault);
	float GetDefault();
	FVector2D GetRange();
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputFloatArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputFloatArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<float> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputString : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputString() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FString Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputStringArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputStringArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FString> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

// Broken out to be able to customize and swap AllowedClass based on provided object proxy
USTRUCT()
struct FMetasoundEditorGraphInputObjectRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	UObject* Object = nullptr;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputObject : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObject() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputObjectRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputObjectArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObjectArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputObjectRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override;
	virtual EMetasoundFrontendLiteralType GetLiteralType() const override;
	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override;
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterControllerInterface>& InParameterInterface) const override;
};