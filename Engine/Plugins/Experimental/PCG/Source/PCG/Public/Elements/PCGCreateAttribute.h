// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPoint.h"
#include "PCGSettings.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "PCGCreateAttribute.generated.h"

class FPCGMetadataAttributeBase;
class UPCGMetadata;
class UPCGParamData;

/**
* Node to create an attribute from a Spatial data or a ParamData.
* If no input is provided, the node behave as a "CreateParamData" and will output a ParamData
* with the given attribute set.
* 
* Note: This need to be updated if we ever add new types.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGCreateAttributeSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif // WITH_EDITOR

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual FName AdditionalTaskName() const override;
	//~End UPCGSettings interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bKeepExistingAttributes = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName OutputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bFromSourceParam = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "bFromSourceParam", EditConditionHides))
	FName SourceParamAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ValidEnumValues = "Float, Double, Integer32, Integer64, Vector2, Vector, Vector4, Quaternion, Transform, String, Boolean, Rotator, Name", EditCondition = "!bFromSourceParam", EditConditionHides))
	EPCGMetadataTypes Type = EPCGMetadataTypes::Double;

	// All different types
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Float && !bFromSourceParam", EditConditionHides))
	float FloatValue = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Integer32 && !bFromSourceParam", EditConditionHides))
	int32 Int32Value = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Double && !bFromSourceParam", EditConditionHides))
	double DoubleValue = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Integer64 && !bFromSourceParam", EditConditionHides))
	int64 IntValue = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Vector2 && !bFromSourceParam", EditConditionHides))
	FVector2D Vector2Value = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Vector && !bFromSourceParam", EditConditionHides))
	FVector VectorValue = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Vector4 && !bFromSourceParam", EditConditionHides))
	FVector4 Vector4Value = FVector4::Zero();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Quaternion && !bFromSourceParam", EditConditionHides))
	FQuat QuatValue = FQuat::Identity;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Transform && !bFromSourceParam", EditConditionHides))
	FTransform TransformValue = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::String && !bFromSourceParam", EditConditionHides))
	FString StringValue = "";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Boolean && !bFromSourceParam", EditConditionHides))
	bool BoolValue = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Rotator && !bFromSourceParam", EditConditionHides))
	FRotator RotatorValue = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Type == EPCGMetadataTypes::Name && !bFromSourceParam", EditConditionHides))
	FName NameValue = NAME_None;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGCreateAttributeElement : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
	/* Create (or clear) an attribute named by OutputAttributeName and depending on the selected type. Value can be overridden by params. Default value will be set to the specified value. */
	FPCGMetadataAttributeBase* ClearOrCreateAttribute(const UPCGCreateAttributeSettings* Settings, UPCGMetadata* Metadata, const UPCGParamData* Params = nullptr, const FName* OutputAttributeNameOverride = nullptr) const;

	/* Set an entry defined by EntryKey (or create one if it is invalid) in the Attribute depending on the selected type. Value can be overridden by params. */
	PCGMetadataEntryKey SetAttribute(const UPCGCreateAttributeSettings* Settings, FPCGMetadataAttributeBase* Attribute, UPCGMetadata* Metadata, PCGMetadataEntryKey EntryKey, const UPCGParamData* Params = nullptr) const;
};