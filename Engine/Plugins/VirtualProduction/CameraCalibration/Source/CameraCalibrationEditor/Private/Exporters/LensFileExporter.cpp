// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensFileExporter.h"
#include "JsonObjectConverter.h"
#include "CameraCalibrationEditorLog.h"
#include "CameraCalibrationUtils.h"
#include "LensFile.h"
#include "LensData.h"
#include "Models/LensModel.h"


FLensInfoExchange::FLensInfoExchange(const ULensFile* LensFile)
{
	if (LensFile == nullptr)
	{
		return;
	}

	const FLensInfo& LensInfo = LensFile->LensInfo;

	SerialNumber = *LensInfo.LensSerialNumber;
	ModelName = *LensInfo.LensModelName;

	if (const ULensModel* LensModelObject = LensInfo.LensModel->GetDefaultObject<ULensModel>())
	{
		DistortionModel = LensModelObject->GetShortModelName();
	}
}

FLensFileMetadata::FLensFileMetadata(const ULensFile* LensFile)
	: LensInfo{ LensFile }
{
	Type = TEXT("LensFile");
	Version = FLensFileExchange::LensFileVersion;

	if (LensFile == nullptr)
	{
		return;
	}

	// Set the default values for metadata when exporting from the ULensFile
	Name = *LensFile->GetName();

	// Add the UserMetadata
	UserMetadata.Reserve(LensFile->UserMetadata.Num());
	for (const TPair<FString, FString>& UserMetadataPair : LensFile->UserMetadata)
	{
		FLensFileUserMetadataEntry UserMetadataEntry;
		UserMetadataEntry.Name = *UserMetadataPair.Key;
		UserMetadataEntry.Value = *UserMetadataPair.Value;
		UserMetadata.Add(UserMetadataEntry);
	}
}

FLensFileExchange::FLensFileExchange(const ULensFile* LensFile)
	: Metadata{ LensFile }
{
	if (LensFile == nullptr)
	{
		return;
	}

	const FLensInfo& LensInfo = LensFile->LensInfo;
	const FVector2D& Sensor = LensInfo.SensorDimensions;

	SensorDimensions.Width = Sensor.X;
	SensorDimensions.Height = Sensor.Y;
	SensorDimensions.Units = ELensFileUnit::Millimeters;

	ImageDimensions.Width = LensInfo.ImageDimensions.X;
	ImageDimensions.Height = LensInfo.ImageDimensions.Y;

	ExtractFocalLengthTable(LensFile);
	ExtractImageCenterTable(LensFile);
	ExtractNodalOffsetTable(LensFile);
	ExtractEncoderTables(LensFile);
	ExtractDistortionParameters(LensFile);
	ExtractSTMaps(LensFile);
}

void FLensFileExchange::ExtractFocalLengthTable(const ULensFile* LensFile)
{
	// Read the FocalLength table
	FLensFileParameterTable FocalLengthParametersTable;
	FocalLengthParametersTable.ParameterName = GET_MEMBER_NAME_CHECKED(ULensFile, FocalLengthTable);
	FocalLengthParametersTable.Header = { FocusEncoderHeaderName, ZoomEncoderHeaderName, FocalLengthFxHeaderName, FocalLengthFyHeaderName };

	const FFocalLengthTable& LensFileFocalLengthTable = LensFile->FocalLengthTable;

	for (int32 FocusPointIndex = 0; FocusPointIndex < LensFileFocalLengthTable.GetFocusPointNum(); ++FocusPointIndex)
	{
		const FFocalLengthFocusPoint& FocalLengthFocusPoint = LensFileFocalLengthTable.FocusPoints[FocusPointIndex];
		const float Focus = FocalLengthFocusPoint.GetFocus();

		for (int32 FocalLengthInfoIndex = 0; FocalLengthInfoIndex < FocalLengthFocusPoint.GetNumPoints(); ++FocalLengthInfoIndex)
		{
			FFocalLengthInfo FocalLengthInfo;
			if (FocalLengthFocusPoint.GetValue(FocalLengthInfoIndex, FocalLengthInfo))
			{
				const float Zoom = FocalLengthFocusPoint.GetZoom(FocalLengthInfoIndex);
				const float Fx = FocalLengthInfo.FxFy.X;
				const float Fy = FocalLengthInfo.FxFy.Y;

				FocalLengthParametersTable.Data.Add(Focus);
				FocalLengthParametersTable.Data.Add(Zoom);
				FocalLengthParametersTable.Data.Add(Fx);
				FocalLengthParametersTable.Data.Add(Fy);
			}
		}
	}

	CameraParameterTables.Add(FocalLengthParametersTable);
}

void FLensFileExchange::ExtractImageCenterTable(const ULensFile* LensFile)
{
	// Read the Image Center parameter table
	FLensFileParameterTable ImageCenterParametersTable;
	ImageCenterParametersTable.ParameterName = GET_MEMBER_NAME_CHECKED(ULensFile, ImageCenterTable);
	ImageCenterParametersTable.Header = { FocusEncoderHeaderName, ZoomEncoderHeaderName, ImageCenterCxHeaderName, ImageCenterCyHeaderName };

	const FImageCenterTable& LensFileImageCenterTable = LensFile->ImageCenterTable;

	for (int32 ImageCenterFocusPointIndex = 0; ImageCenterFocusPointIndex < LensFileImageCenterTable.GetFocusPointNum(); ++ImageCenterFocusPointIndex)
	{
		const FImageCenterFocusPoint& ImageCenterFocusPoint = LensFileImageCenterTable.FocusPoints[ImageCenterFocusPointIndex];
		const float Focus = ImageCenterFocusPoint.GetFocus();

		for (int32 ImageCenterInfoIndex = 0; ImageCenterInfoIndex < ImageCenterFocusPoint.GetNumPoints(); ++ImageCenterInfoIndex)
		{
			const float Zoom = ImageCenterFocusPoint.GetZoom(ImageCenterInfoIndex);

			FImageCenterInfo ImageCenterInfo;
			if (ImageCenterFocusPoint.GetPoint(Zoom, ImageCenterInfo))
			{
				const float Cx = ImageCenterInfo.PrincipalPoint.X;
				const float Cy = ImageCenterInfo.PrincipalPoint.Y;

				ImageCenterParametersTable.Data.Add(Focus);
				ImageCenterParametersTable.Data.Add(Zoom);
				ImageCenterParametersTable.Data.Add(Cx);
				ImageCenterParametersTable.Data.Add(Cy);
			}
		}
	}

	CameraParameterTables.Add(ImageCenterParametersTable);
}

void FLensFileExchange::ExtractNodalOffsetTable(const ULensFile* LensFile)
{
	// Read the NodalOffset parameter table
	FLensFileParameterTable NodalOffsetParametersTable;
	NodalOffsetParametersTable.ParameterName = GET_MEMBER_NAME_CHECKED(ULensFile, NodalOffsetTable);
	NodalOffsetParametersTable.Header = {
		FocusEncoderHeaderName,
		ZoomEncoderHeaderName,
		NodalOffsetQxHeaderName,
		NodalOffsetQyHeaderName,
		NodalOffsetQzHeaderName,
		NodalOffsetQwHeaderName,
		NodalOffsetTxHeaderName,
		NodalOffsetTyHeaderName,
		NodalOffsetTzHeaderName
	};

	const FNodalOffsetTable& LensFileNodalOffsetTable = LensFile->NodalOffsetTable;

	for (int32 NodalOffsetFocusPointIndex = 0; NodalOffsetFocusPointIndex < LensFileNodalOffsetTable.GetFocusPointNum(); ++NodalOffsetFocusPointIndex)
	{
		const FNodalOffsetFocusPoint& NodalOffsetFocusPoint = LensFileNodalOffsetTable.FocusPoints[NodalOffsetFocusPointIndex];
		const float Focus = NodalOffsetFocusPoint.GetFocus();

		for (int32 NodalOffsetPointIndex = 0; NodalOffsetPointIndex < NodalOffsetFocusPoint.GetNumPoints(); ++NodalOffsetPointIndex)
		{
			const float Zoom = NodalOffsetFocusPoint.GetZoom(NodalOffsetPointIndex);

			FNodalPointOffset NodalOffsetInfo;
			if (NodalOffsetFocusPoint.GetPoint(Zoom, NodalOffsetInfo))
			{
				NodalOffsetParametersTable.Data.Add(Focus);
				NodalOffsetParametersTable.Data.Add(Zoom);

				FTransform NodalOffsetTransform{ NodalOffsetInfo.RotationOffset, NodalOffsetInfo.LocationOffset };
				FCameraCalibrationUtils::ConvertUnrealToOpenCV(NodalOffsetTransform);

				const FQuat RotationOffset = NodalOffsetTransform.GetRotation();
				const FVector LocationOffset = NodalOffsetTransform.GetTranslation();

				NodalOffsetParametersTable.Data.Add(RotationOffset.X);
				NodalOffsetParametersTable.Data.Add(RotationOffset.Y);
				NodalOffsetParametersTable.Data.Add(RotationOffset.Z);
				NodalOffsetParametersTable.Data.Add(RotationOffset.W);
				NodalOffsetParametersTable.Data.Add(LocationOffset.X);
				NodalOffsetParametersTable.Data.Add(LocationOffset.Y);
				NodalOffsetParametersTable.Data.Add(LocationOffset.Z);
			}
		}

		CameraParameterTables.Add(NodalOffsetParametersTable);
	}
}

void FLensFileExchange::ExtractEncoderTables(const ULensFile* LensFile)
{
	// Read the Encoder Tables from the LensFile
	FLensFileParameterTable FocusEncordersTable;
	FLensFileParameterTable IrisEncodersTable;

	FocusEncordersTable.ParameterName = GET_MEMBER_NAME_CHECKED(FEncodersTable, Focus);
	FocusEncordersTable.Header = { FocusEncoderHeaderName, FocusCMHeaderName };

	IrisEncodersTable.ParameterName = GET_MEMBER_NAME_CHECKED(FEncodersTable, Iris);
	IrisEncodersTable.Header = { IrisEncoderHeaderName, IrisFstopHeaderName };

	const FEncodersTable& LensFileEncodersTable = LensFile->EncodersTable;

	FocusEncordersTable.Data.Reserve(LensFileEncodersTable.GetNumFocusPoints() * 2);
	IrisEncodersTable.Data.Reserve(LensFileEncodersTable.GetNumIrisPoints() * 2);

	for (int32 FocusPointIndex = 0; FocusPointIndex < LensFileEncodersTable.GetNumFocusPoints(); ++FocusPointIndex)
	{
		const float FocusInput = LensFileEncodersTable.GetFocusInput(FocusPointIndex);
		const float FocusValue = LensFileEncodersTable.GetFocusValue(FocusPointIndex);

		FocusEncordersTable.Data.Add(FocusInput);
		FocusEncordersTable.Data.Add(FocusValue);
	}

	for (int32 IrisPointIndex = 0; IrisPointIndex < LensFileEncodersTable.GetNumIrisPoints(); ++IrisPointIndex)
	{
		const float IrisInput = LensFileEncodersTable.GetIrisInput(IrisPointIndex);
		const float IrisValue = LensFileEncodersTable.GetIrisValue(IrisPointIndex);

		IrisEncodersTable.Data.Add(IrisInput);
		IrisEncodersTable.Data.Add(IrisValue);
	}

	EncoderTables.Add(FocusEncordersTable);
	EncoderTables.Add(IrisEncodersTable);
}

void FLensFileExchange::ExtractDistortionParameters(const ULensFile* LensFile)
{
	// Read the Distortion parameter table
	FLensFileParameterTable DistortionParametersTable;
	DistortionParametersTable.ParameterName = GET_MEMBER_NAME_CHECKED(ULensFile, DistortionTable);

	if (const ULensModel* LensModelObject = LensFile->LensInfo.LensModel->GetDefaultObject<ULensModel>())
	{
		const int32 NumDistortionParams = LensModelObject->GetNumParameters();
		DistortionParametersTable.Header.Reserve(NumDistortionParams + 2);

		// Add the Focus and Zoom encoders headers that are common to all camera parameters tables
		DistortionParametersTable.Header.Add(FocusEncoderHeaderName);
		DistortionParametersTable.Header.Add(ZoomEncoderHeaderName);

		for (const FText& ParamName : LensModelObject->GetParameterDisplayNames())
		{
			DistortionParametersTable.Header.Add(*ParamName.ToString());
		}

		const FDistortionTable& LensFileDistortionTable = LensFile->DistortionTable;
		DistortionParametersTable.Data.Reserve(LensFileDistortionTable.GetFocusPointNum());

		for (int32 DistortionFocusPointIndex = 0; DistortionFocusPointIndex < LensFileDistortionTable.GetFocusPointNum(); ++DistortionFocusPointIndex)
		{
			const FDistortionFocusPoint& DistortionFocusPoint = LensFileDistortionTable.FocusPoints[DistortionFocusPointIndex];
			const float Focus = DistortionFocusPoint.GetFocus();

			for (int32 DistortionPointIndex = 0; DistortionPointIndex < DistortionFocusPoint.GetNumPoints(); ++DistortionPointIndex)
			{
				const float Zoom = DistortionFocusPoint.GetZoom(DistortionPointIndex);

				FDistortionInfo DistortionInfo;
				if (DistortionFocusPoint.GetPoint(Zoom, DistortionInfo))
				{
					if (DistortionInfo.Parameters.Num() == NumDistortionParams)
					{
						// Copy the parameters to the exchange struct
						DistortionParametersTable.Data.Add(Focus);
						DistortionParametersTable.Data.Add(Zoom);

						for (const float ParamValue : DistortionInfo.Parameters)
						{
							DistortionParametersTable.Data.Add(ParamValue);
						}
					}
					else
					{
						UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Expected %d parameters for zoom %f but %d are available."), NumDistortionParams, Zoom, DistortionInfo.Parameters.Num());
					}
				}
			}
		}
	}

	CameraParameterTables.Add(DistortionParametersTable);
}

void FLensFileExchange::ExtractSTMaps(const ULensFile* LensFile)
{
	if (LensFile->STMapTable.GetFocusPointNum() > 0)
	{
		// TODO: Extract the STMaps from the LensFile to create a new FLensFileParameterTable
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("STMaps are not yet supported when exporting a LensFile"));
	}
}

ULensFileExporter::ULensFileExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULensFile::StaticClass();
	bText = true;
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("ulens"));
	FormatDescription.Add(TEXT("Unreal LensFile"));
}

bool ULensFileExporter::ExportText(const class FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags)
{
	const ULensFile* LensFile = Cast<ULensFile>(Object);
	if (LensFile == nullptr)
	{
		return false;
	}

	FLensFileExchange LensFileExchange{ LensFile };

	FJsonObjectConverter::CustomExportCallback CustomExportCallback;
	CustomExportCallback.BindLambda([](FProperty* Property, const void* Value) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonValue> Result = nullptr;

		if (FStructProperty* PropertyAsStruct = CastField<FStructProperty>(Property))
		{
			// If this is a FLensFileParameterTable we need to do a custom export
			if (PropertyAsStruct->Struct->IsChildOf(FLensFileParameterTable::StaticStruct()))
			{
				// Cast the property to our known struct type
				const FLensFileParameterTable* ParameterTable = (const FLensFileParameterTable*)Value;

				// Use the reflection system to get the FProperty representing the fields of the table
				FProperty* ParameterNameProperty = ParameterTable->StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FLensFileParameterTable, ParameterName));
				FProperty* HeaderProperty = ParameterTable->StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FLensFileParameterTable, Header));
				FProperty* DataProperty = ParameterTable->StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FLensFileParameterTable, Data));

				// Convert the Header and Data field to Strings
				FString HeaderValuesStr;
				const int32 NumHeaders = ParameterTable->Header.Num();
				for (int32 HeaderIndex = 0; HeaderIndex < NumHeaders; ++HeaderIndex)
				{
					const FName& Header = ParameterTable->Header[HeaderIndex];
					HeaderValuesStr.Append(Header.ToString());
					if (HeaderIndex < (NumHeaders - 1))
					{
						HeaderValuesStr.Append(TEXT(", "));
					}
				}

				FString DataStr;
				const int32 NumDataElements = ParameterTable->Data.Num();
				for (int32 DataIndex = 0; DataIndex < NumDataElements; ++DataIndex)
				{
					const float DataElement = ParameterTable->Data[DataIndex];
					DataStr.Append(FString::SanitizeFloat(DataElement));
					if (DataIndex < (NumDataElements - 1))
					{
						DataStr.Append(((DataIndex + 1) % NumHeaders == 0) ? TEXT("; ") : TEXT(", "));
					}
				}

				// Convert the ParameterNameProperty to a Json representation
				TSharedPtr<FJsonValue> ParameterNameJson = FJsonObjectConverter::UPropertyToJsonValue(ParameterNameProperty, &ParameterTable->ParameterName);

				// Json Object that will hold the serialized FLensFileParameterTable
				TSharedPtr<FJsonObject> ParameterTableJson = MakeShared<FJsonObject>();
				ParameterTableJson->SetField(FJsonObjectConverter::StandardizeCase(ParameterNameProperty->GetName()), ParameterNameJson);
				ParameterTableJson->SetStringField(FJsonObjectConverter::StandardizeCase(HeaderProperty->GetName()), HeaderValuesStr);
				ParameterTableJson->SetStringField(FJsonObjectConverter::StandardizeCase(DataProperty->GetName()), DataStr);

				Result = MakeShared<FJsonValueObject>(ParameterTableJson);
			}
		}

		return Result;
	});

	const int64 CheckFlags = 0;
	const int64 SkipFlags = 0;
	const int32 Indent = 0;
	const bool bPrettyPrint = true;
	FString SerializedJson;
	if (FJsonObjectConverter::UStructToJsonObjectString<FLensFileExchange>(LensFileExchange, SerializedJson, CheckFlags, SkipFlags, Indent, &CustomExportCallback, bPrettyPrint))
	{
		Ar.Log(SerializedJson);

		return true;
	}
	else
	{
		return false;
	}
}