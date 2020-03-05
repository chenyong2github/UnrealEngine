// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/LidarPointCloudFileIO.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloud.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

ULidarPointCloudFileIO* ULidarPointCloudFileIO::Instance = nullptr;

void FLidarPointCloudImportSettings::Serialize(FArchive& Ar)
{
	int32 Dummy;
	bool bDummy;

	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 12)
	{
	}
	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 10)
	{
		Ar << bDummy << Dummy << Dummy;
	}
	else if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 8)
	{
		Ar << bDummy;
	}
}

void FLidarPointCloudFileIOHandler::PrepareImport()
{
	PrecisionCorrectionOffset[0] = 0;
	PrecisionCorrectionOffset[1] = 0;
	PrecisionCorrectionOffset[2] = 0;
	bPrecisionCorrected = false;
}

bool FLidarPointCloudFileIOHandler::ValidateImportSettings(TSharedPtr<FLidarPointCloudImportSettings>& ImportSettings, const FString& Filename)
{
	if (ImportSettings.IsValid())
	{
		if (ImportSettings->IsGeneric())
		{
			// Convert to the specialized settings
			TSharedPtr<FLidarPointCloudImportSettings> NewSettings = GetImportSettings(ImportSettings->Filename);
			NewSettings->bImportAll = ImportSettings->bImportAll;
			ImportSettings = NewSettings;
		}
		else if (!IsSettingsUIDSupported(ImportSettings->GetUID()))
		{
			PC_ERROR("Provided type of ImportSettings does not match the selected importer. Aborting.");
			ImportSettings = nullptr;
		}
	}
	else
	{
		ImportSettings = GetImportSettings(Filename);
	}

	return ImportSettings.IsValid();
}

//////////////////////////////////////////////////////////// File IO

bool SerializePoints(const FString& Filename, FLidarPointCloudImportResults& OutImportResults, bool bLoading)
{
	FString CacheFilename = (Filename + ".tmp");

	TUniquePtr<FArchive> Ar(bLoading ? IFileManager::Get().CreateFileReader(*CacheFilename) : IFileManager::Get().CreateFileWriter(*CacheFilename, 0));
	if (Ar)
	{
		Ar->Serialize(&OutImportResults.OriginalCoordinates, sizeof(FDoubleVector));
		Ar->Serialize(&OutImportResults.Bounds, sizeof(FBox));

		int64 NumPoints = OutImportResults.Points.Num();
		Ar->Serialize(&NumPoints, sizeof(int64));

		if (bLoading)
		{
			OutImportResults.Points.Empty(NumPoints);
			OutImportResults.Points.AddUninitialized(NumPoints);
		}

		FLidarPointCloudPoint* Data = OutImportResults.Points.GetData();

		int64 ProcessedPoints = 0;
		while (ProcessedPoints < NumPoints)
		{
			int32 BatchSize = FMath::Min(50000000LL, NumPoints - ProcessedPoints);

			Ar->Serialize(Data, BatchSize * sizeof(FLidarPointCloudPoint));

			Data += BatchSize;
			ProcessedPoints += BatchSize;
		}

		Ar->Close();
		Ar = nullptr;

		return true;
	}

	return false;
}

bool ULidarPointCloudFileIO::Import(const FString& Filename, TSharedPtr<FLidarPointCloudImportSettings> ImportSettings, FLidarPointCloudImportResults& OutImportResults)
{
	bool bSuccess = false;

	FScopeBenchmarkTimer BenchmarkTimer("Importing");
	
	FLidarPointCloudFileIOHandler* Handler = FindHandlerByFilename(Filename);
	if (Handler && Handler->SupportsImport())
	{
		if (Handler->ValidateImportSettings(ImportSettings, Filename))
		{
			bool bUseCaching = GetDefault<ULidarPointCloudSettings>()->bUseIOCaching;

			// Check for cached file
			if (bUseCaching && SerializePoints(Filename, OutImportResults, true))
			{
				bSuccess = true;
			}
			else
			{
				Handler->PrepareImport();
				bSuccess = Handler->HandleImport(Filename, ImportSettings, OutImportResults);

				if (bUseCaching && bSuccess)
				{
					SerializePoints(Filename, OutImportResults, false);
				}
			}
		}
	}
	else
	{
		PC_ERROR("No registered importer found for file: %s", *Filename);
	}

	if (!bSuccess)
	{
		BenchmarkTimer.bActive = false;
	}

	return bSuccess;
}

bool ULidarPointCloudFileIO::Export(const FString& Filename, ULidarPointCloud* AssetToExport)
{
	if (AssetToExport)
	{
		FScopeBenchmarkTimer Timer("Exporting");

		FLidarPointCloudFileIOHandler* Handler = ULidarPointCloudFileIO::FindHandlerByFilename(Filename);
		if (Handler && Handler->SupportsExport() && Handler->HandleExport(Filename, AssetToExport))
		{
			return true;
		}
		else
		{
			Timer.bActive = false;
		}
	}

	return false;
}

TSharedPtr<FLidarPointCloudImportSettings> ULidarPointCloudFileIO::GetImportSettings(const FString& Filename)
{
	FLidarPointCloudFileIOHandler *Handler = FindHandlerByFilename(Filename);
	if (Handler && Handler->SupportsImport())
	{
		return Handler->GetImportSettings(Filename);
	}

	return nullptr;
}

TArray<FString> ULidarPointCloudFileIO::GetSupportedImportExtensions()
{
	TArray<FString> Extensions;

	for (const TPair<FString, FLidarPointCloudFileIOHandler*>& Handler : Instance->RegisteredHandlers)
	{
		if (Handler.Value->SupportsImport())
		{
			Extensions.Add(Handler.Key);
		}
	}

	return Extensions;
}

TArray<FString> ULidarPointCloudFileIO::GetSupportedExportExtensions()
{
	TArray<FString> Extensions;

	for (const TPair<FString, FLidarPointCloudFileIOHandler*>& Handler : Instance->RegisteredHandlers)
	{
		if (Handler.Value->SupportsExport())
		{
			Extensions.Add(Handler.Key);
		}
	}

	return Extensions;
}

void ULidarPointCloudFileIO::RegisterHandler(FLidarPointCloudFileIOHandler* Handler, const TArray<FString>& Extensions)
{
	for (const FString& Extension : Extensions)
	{
		Instance->RegisteredHandlers.Emplace(Extension, Handler);
	}
}

FLidarPointCloudFileIOHandler* ULidarPointCloudFileIO::FindHandlerByType(const FString& Type)
{
	FLidarPointCloudFileIOHandler** Handler = Instance->RegisteredHandlers.Find(Type);
	return Handler ? *Handler : nullptr;
}

void ULidarPointCloudFileIO::SerializeImportSettings(FArchive& Ar, TSharedPtr<FLidarPointCloudImportSettings>& ImportSettings)
{
	if (Ar.IsLoading())
	{
		FString FilePath;
		Ar << FilePath;

		// If there are no ImportSettings data, do not try to read anything
		if (FilePath.IsEmpty())
		{
			return;
		}

		FLidarPointCloudFileIOHandler* Handler = FindHandlerByFilename(FilePath);
		
		// The importer for this file format is no longer available - no way to proceed
		check(Handler);
		
		ImportSettings = Handler->GetImportSettings(FilePath);
		ImportSettings->Serialize(Ar);
	}
	else
	{
		if (ImportSettings.IsValid())
		{
			Ar << ImportSettings->Filename;
			ImportSettings->Serialize(Ar);
		}
		else
		{
			// If the ImportSettings is invalid, write 0-length FString to indicate it for loading
			FString FilePath = "";
			Ar << FilePath;
		}
	}
}

ULidarPointCloudFileIO::ULidarPointCloudFileIO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULidarPointCloud::StaticClass();
	PreferredFormatIndex = 0;

	// This will assign an Instance pointer
	if (!Instance)
	{
		Instance = this;
	}

	// Requested by UExporter
	else
	{
		FormatExtension.Append(ULidarPointCloudFileIO::GetSupportedExportExtensions());
		for (int32 i = 0; i < FormatExtension.Num(); i++)
		{
			FormatDescription.Add(TEXT("Point Cloud"));
		}
	}
}

bool ULidarPointCloudFileIO::SupportsObject(UObject* Object) const
{
	bool bSupportsObject = false;

	// Fail, if no exporters are registered
	if (Super::SupportsObject(Object) && GetSupportedExportExtensions().Num() > 0)
	{
		ULidarPointCloud* PointCloud = Cast<ULidarPointCloud>(Object);
		if (PointCloud)
		{
			bSupportsObject = PointCloud->GetNumPoints() > 0;
		}
	}

	return bSupportsObject;
}

bool ULidarPointCloudFileIO::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex /*= 0*/, uint32 PortFlags /*= 0*/)
{
	Export(CurrentFilename, Cast<ULidarPointCloud>(Object));

	// Return false to avoid overwriting the data
	return false;
}