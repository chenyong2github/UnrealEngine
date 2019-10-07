// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

#include "UVGenerationUtils.generated.h"

class FStaticMeshEditor;
class IPropertyHandle;
class IDetailChildrenBuilder;
class FDetailWidgetRow;

UENUM()
enum class EGenerateUVProjectionType : uint8
{
	Box,
	Cylindrical,
	Planar,
};

USTRUCT()
struct FUVGenerationSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, category = "Projection Settings")
	EGenerateUVProjectionType ProjectionType;
	UPROPERTY(EditAnywhere, category = "Projection Settings")
	FVector Position;
	UPROPERTY(EditAnywhere, category = "Projection Settings")
	FRotator Rotation;
	UPROPERTY(EditAnywhere, category = "Projection Settings")
	FVector Size;
	UPROPERTY(EditAnywhere, category = "Projection Settings", meta = (DisplayName = "UV Tiling Scale"))
	FVector2D UVTilingScale = FVector2D::UnitVector;
	UPROPERTY(EditAnywhere, category = "Projection Settings", meta = (DisplayName = "UV Offset"))
	FVector2D UVOffset = FVector2D::ZeroVector;
	
	//ClampMax defined by MAX_MESH_TEXTURE_COORDS
	UPROPERTY(EditAnywhere, category = "Projection Settings", meta = (ClampMin = 0, ClampMax = 7, Tooltip = "The UV channel the projection will be applied to.")) 
	uint8 TargetChannel;

	//Used to get access to the StaticMeshEditor without creating a dependency.
	DECLARE_DELEGATE_RetVal(int32, FOnGetNumberOfUVs)
	FOnGetNumberOfUVs OnGetNumberOfUVs;

	//Using a delegate instead of an event because events assume the owning type is a class.
	DECLARE_MULTICAST_DELEGATE(FOnShapeEditingValuesChanged)
	FOnShapeEditingValuesChanged OnShapeEditingValueChanged;

	FUVGenerationSettings()
		: ProjectionType(EGenerateUVProjectionType::Box)
		, Position(FVector::ZeroVector)
		, Rotation(0)
		, Size(FVector::OneVector)
		, UVTilingScale(FVector2D::UnitVector)
		, UVOffset(FVector2D::ZeroVector)
		, TargetChannel(0)
	{}
};

class FUVGenerationSettingsCustomization : public IPropertyTypeCustomization
{
	enum class EShapeField {
		Size,
		Position,
		Rotation
	};

public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	void OnShapePropertyChanged();

	FUVGenerationSettings* GenerateUVSettings = nullptr;
};

UCLASS(config = EditorPerProjectUserSettings, Transient)
class UUVFlattenMappingSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, category = "Flatten Mapping", meta = (ToolTip = "The UV channel where to generate the flatten mapping", ClampMin = "0", ClampMax = "7")) //Clampmax is from MAX_MESH_TEXTURE_COORDS_MD - 1
	int32 UVChannel = 0;

	UPROPERTY(config, EditAnywhere, category = "Flatten Mapping", meta = (ClampMin = "1", ClampMax = "90"))
	float AngleThreshold = 66.f;

	UPROPERTY(config, EditAnywhere, category = "Flatten Mapping", meta = (ClampMin = "0", ClampMax = "1"))
	float AreaWeight = 0.7f;
};