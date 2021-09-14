// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Exporters/Exporter.h"
#include "LensFileExporter.generated.h"


// TODO: Move the Structs to another file

UENUM()
enum class ELensFileUnit
{
	Millimeters,
	Normalized
};

UENUM()
enum class ENodalOffsetCoordinateSystem
{
	OpenCV
};

USTRUCT()
struct FLensInfoExchange
{
	GENERATED_BODY()

	FLensInfoExchange(const class ULensFile* LensFile = nullptr);

	UPROPERTY()
	FName SerialNumber;

	UPROPERTY()
	FName ModelName;

	UPROPERTY()
	FName DistortionModel;
};

USTRUCT()
struct FLensFileUserMetadataEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName Value;
};

USTRUCT()
struct FLensFileMetadata
{
	GENERATED_BODY()

	FLensFileMetadata(const class ULensFile* LensFile = nullptr);

	UPROPERTY()
	FName Type;

	UPROPERTY()
	FName Version;

	UPROPERTY()
	FLensInfoExchange LensInfo;

	UPROPERTY()
	FName Name;

	UPROPERTY()
	ENodalOffsetCoordinateSystem NodalOffsetCoordinateSystem = ENodalOffsetCoordinateSystem::OpenCV;

	UPROPERTY()
	ELensFileUnit FxFyUnits = ELensFileUnit::Normalized;

	UPROPERTY()
	ELensFileUnit CxCyUnits = ELensFileUnit::Normalized;

	UPROPERTY()
	TArray<FLensFileUserMetadataEntry> UserMetadata;
};


USTRUCT()
struct FLensFileSensorDimensions
{
	GENERATED_BODY()

	UPROPERTY()
	float Width = 0.0f;

	UPROPERTY()
	float Height = 0.0f;

	UPROPERTY()
	ELensFileUnit Units = ELensFileUnit::Normalized;
};

USTRUCT()
struct FLensFileImageDimensions
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;
};

USTRUCT()
struct FLensFileParameterTable
{
	GENERATED_BODY()

	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	TArray<FName> Header;

	UPROPERTY()
	TArray<float> Data;
};

USTRUCT()
struct FLensFileExchange
{
	GENERATED_BODY()

	FLensFileExchange(const class ULensFile* LensFile = nullptr);

	UPROPERTY()
	FLensFileMetadata Metadata;

	UPROPERTY()
	FLensFileSensorDimensions SensorDimensions;

	UPROPERTY()
	FLensFileImageDimensions ImageDimensions;

	UPROPERTY()
	TArray<FLensFileParameterTable> CameraParameterTables;

	UPROPERTY()
	TArray<FLensFileParameterTable> EncoderTables;

	constexpr static TCHAR LensFileVersion[] = TEXT("0.0.0");

	constexpr static TCHAR FocusEncoderHeaderName[] = TEXT("FocusEncoder");
	constexpr static TCHAR ZoomEncoderHeaderName[] = TEXT("ZoomEncoder");
	constexpr static TCHAR IrisEncoderHeaderName[] = TEXT("IrisEncoder");

	constexpr static TCHAR FocusCMHeaderName[] = TEXT("FocusCM");
	constexpr static TCHAR IrisFstopHeaderName[] = TEXT("IrisFstop");

	constexpr static TCHAR FocalLengthFxHeaderName[] = TEXT("Fx");
	constexpr static TCHAR FocalLengthFyHeaderName[] = TEXT("Fy");

	constexpr static TCHAR ImageCenterCxHeaderName[] = TEXT("Cx");
	constexpr static TCHAR ImageCenterCyHeaderName[] = TEXT("Cy");

	constexpr static TCHAR NodalOffsetQxHeaderName[] = TEXT("Qx");
	constexpr static TCHAR NodalOffsetQyHeaderName[] = TEXT("Qy");
	constexpr static TCHAR NodalOffsetQzHeaderName[] = TEXT("Qz");
	constexpr static TCHAR NodalOffsetQwHeaderName[] = TEXT("Qw");

	constexpr static TCHAR NodalOffsetTxHeaderName[] = TEXT("Tx");
	constexpr static TCHAR NodalOffsetTyHeaderName[] = TEXT("Ty");
	constexpr static TCHAR NodalOffsetTzHeaderName[] = TEXT("Tz");

private:
	void ExtractFocalLengthTable(const class ULensFile* LensFile);
	void ExtractImageCenterTable(const class ULensFile* LensFile);
	void ExtractNodalOffsetTable(const class ULensFile* LensFile);
	void ExtractEncoderTables(const class ULensFile* LensFile);
	void ExtractDistortionParameters(const class ULensFile* LensFile);
	void ExtractSTMaps(const class ULensFile* LensFile);
};

UCLASS()
class ULensFileExporter : public UExporter
{
	GENERATED_UCLASS_BODY()

	//~ Begin UExporter interface
	bool ExportText(const class FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags/* =0 */) override;
	//~ End UExporter interface
};