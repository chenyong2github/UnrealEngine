// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveVector.h"
#include "Engine/DataTable.h"

#include "DatasmithDeltaGenImportData.generated.h"

UENUM(BlueprintType)
enum class EObjectSetDataType : uint8
{
	None,
	Translation,
	Rotation,
	Scaling,
	Visibility,
	Center,
};

UENUM(BlueprintType)
enum class EDeltaGenVarDataVariantSwitchType : uint8
{
	Unsupported,
	Camera,
	Geometry,
	Package,
	SwitchObject,
	ObjectSet
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataGeometryVariant
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FName> VisibleMeshes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FName> HiddenMeshes;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataCameraVariant
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FVector Location;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FRotator Rotation;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	//FVector RotationAxis;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	//float RotationAngle;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	//FVector RotationEuler;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataPackageVariant
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<int32> SelectedVariants; // variant id for variant selected in  each variant set(TargetVariantSet)
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataSwitchObjectVariant
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	int32 Selection;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataObjectSetVariantValue
{
	GENERATED_USTRUCT_BODY()

	FDeltaGenVarDataObjectSetVariantValue()
		: TargetNodeNameSanitized(NAME_None)
		, DataType(EObjectSetDataType::None)
		, Data()
	{}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FName TargetNodeNameSanitized;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	EObjectSetDataType DataType;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<uint8> Data;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataObjectSetVariant
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataObjectSetVariantValue> Values;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitchCamera
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataCameraVariant> Variants;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitchGeometry
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FName> TargetNodes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataGeometryVariant> Variants;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitchPackage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FString> TargetVariantSets;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataPackageVariant> Variants;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitchSwitchObject
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FName TargetSwitchObject;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataSwitchObjectVariant> Variants;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitchObjectSet
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenVarDataObjectSetVariant> Variants;
};

USTRUCT(BlueprintType)
struct FDeltaGenVarDataVariantSwitch : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	EDeltaGenVarDataVariantSwitchType Type;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TMap<int32, int32> VariantIDToVariantIndex;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TMap<int32, FString> VariantIDToVariantName;



	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FDeltaGenVarDataVariantSwitchCamera Camera;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FDeltaGenVarDataVariantSwitchGeometry Geometry;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FDeltaGenVarDataVariantSwitchSwitchObject SwitchObject;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FDeltaGenVarDataVariantSwitchPackage Package;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FDeltaGenVarDataVariantSwitchObjectSet ObjectSet;
};

class FDeltaGenVarData
{
public:

	TArray<FDeltaGenVarDataVariantSwitch> VariantSwitches;
};

USTRUCT(BlueprintType)
struct FDeltaGenPosDataState : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	// Name of the actual state
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	// Maps Actor name to whether it's on or off (visibility)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TMap<FString, bool> States;

	// Maps Actor name to a switch choice (index of the child that is visible)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TMap<FName, int> Switches;

	// Maps Actor name to a material name
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TMap<FString, FString> Materials;
};

USTRUCT(BlueprintType)
struct FDeltaGenPosData
{
	GENERATED_USTRUCT_BODY()

	TArray<FDeltaGenPosDataState> States;
};

UENUM(BlueprintType)
enum class EDeltaGenTmlDataAnimationTrackType : uint8
{
	Unsupported = 0,
	Translation = 1,
	Rotation = 2,
	RotationDeltaGenEuler = 4,
	Scale = 8,
	Center = 16
};

ENUM_CLASS_FLAGS(EDeltaGenTmlDataAnimationTrackType);

USTRUCT(BlueprintType)
struct FDeltaGenTmlDataAnimationTrack
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	EDeltaGenTmlDataAnimationTrackType Type;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<float> Keys;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FVector4> Values;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	bool Zeroed;
};

USTRUCT(BlueprintType)
struct FDeltaGenTmlDataTimelineAnimation
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FName TargetNode;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenTmlDataAnimationTrack> Tracks;
};

USTRUCT(BlueprintType)
struct FDeltaGenTmlDataTimeline : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenTmlDataTimelineAnimation> Animations;
};

USTRUCT(BlueprintType)
struct FDeltaGenAnimationsData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=DeltaGen)
	TArray<FDeltaGenTmlDataTimeline> Timelines;
};
