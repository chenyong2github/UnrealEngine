// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/LidarPointCloudFileIO_LAS.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloud.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Templates/Atomic.h"

FLidarPointCloudImportSettings_LAS::FLidarPointCloudImportSettings_LAS(const FString& Filename)
	: FLidarPointCloudImportSettings(Filename)
{
	ReadFileHeader(Filename);
}

void FLidarPointCloudImportSettings_LAS::Serialize(FArchive& Ar)
{
	FLidarPointCloudImportSettings::Serialize(Ar);

	int32 Dummy32;
	uint8 Dummy;
	
	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 11)
	{
	}
	else if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 10)
	{
		Ar << Dummy;
	}
	else if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) >= 7)
	{
		Ar << Dummy32 << Dummy32 << Dummy;
	}
}

void FLidarPointCloudImportSettings_LAS::ReadFileHeader(const FString& InFilename)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
	if (Reader)
	{
		int64 TotalSize = Reader->TotalSize();

		// Check the minimum size for the file to be valid
		if (TotalSize > 227)
		{
			// Reserve space for the full public header block
			TArray<uint8> Data;
			Data.AddUninitialized(375);

			// Start by reading the basic set, for LAS file versions prior to 1.4
			Reader->Serialize(Data.GetData(), 227);

			// Populate the basic set
			FMemory::Memcpy(&PublicHeaderBlock, Data.GetData(), 227);

			uint16 BytesRead = 227;

			// Check if the file has valid marker
			if (PublicHeaderBlock.IsValid())
			{
				// Read extra 8 bytes added post 1.2
				if (PublicHeaderBlock.VersionMinor > 2)
				{
					Reader->Serialize(Data.GetData() + BytesRead, 8);
					BytesRead += 8;

					// Read extra 140 bytes added post 1.3
					if (PublicHeaderBlock.VersionMinor > 3)
					{
						Reader->Serialize(Data.GetData() + BytesRead, 140);
						BytesRead += 140;
					}

					// Re-populate the header struct
					FMemory::Memcpy(&PublicHeaderBlock, Data.GetData(), BytesRead);
				}

			}
		}

		Reader->Close();
	}
}

bool ULidarPointCloudFileIO_LAS::HandleImport(const FString& Filename, TSharedPtr<FLidarPointCloudImportSettings> ImportSettings, FLidarPointCloudImportResults &OutImportResults)
{
	if (!ValidateImportSettings(ImportSettings, Filename))
	{
		return false;
	}

	FLidarPointCloudImportSettings_LAS* Settings = (FLidarPointCloudImportSettings_LAS*)ImportSettings.Get();
	FLidarPointCloudFileIO_LAS_PublicHeaderBlock Header = Settings->PublicHeaderBlock;

	// Return immediately if header is invalid
	if (!Header.IsValid())
	{
		return false;
	}

	bool bSuccess = false;

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
	if (Reader)
	{
		int64 TotalSize = Reader->TotalSize();
		int64 TotalPointsToRead = Header.GetNumberOfPoints();

		if (TotalPointsToRead > 0)
		{
			OutImportResults.SetMaxProgressCounter(TotalPointsToRead);
			
			bool bHasIntensityData = false;
			bool bUse16BitIntensity = true;
			bool bUse16BitRGB = true;

			const int64 MaxBufferSize = GetDefault<ULidarPointCloudSettings>()->MaxImportBufferSize;

			// Calculate max buffer size
			const int64 MaxPointsToRead = FMath::Min(TotalPointsToRead, MaxBufferSize / Header.PointDataRecordLength);

			// Detect bit depth
			if(GetDefault<ULidarPointCloudSettings>()->bEnable8BitLASDetection)
			{
				bUse16BitIntensity = false;
				bUse16BitRGB = false;

				// Set the correct position for the reader
				Reader->Seek(Header.OffsetToPointData);

				// Calculate the amount of data to read
				int64 PointsToRead = FMath::Min((int64)GetDefault<ULidarPointCloudSettings>()->MaxNumberOfPointsToScanLAS, TotalPointsToRead);

				// Create data buffer
				TArray<uint8> Data;
				Data.AddUninitialized(PointsToRead * Header.PointDataRecordLength);

				// Read the data
				Reader->Serialize(Data.GetData(), Data.Num());

				for(uint8* DataPtr = Data.GetData(), *DataEnd = DataPtr + Data.Num(); DataPtr != DataEnd; DataPtr += Header.PointDataRecordLength)
				{
					FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon* Record = (FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon*)DataPtr;

					bHasIntensityData = bHasIntensityData || Record->Intensity > 0;
					bUse16BitIntensity = bUse16BitIntensity || Record->Intensity > 255;

					if(!bUse16BitRGB)
					{
						FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB* RecordRGB = nullptr;

						switch (Header.PointDataRecordFormat)
						{
						case 2: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat2*)DataPtr; break;
						case 3: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat3*)DataPtr; break;
						case 5: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat5*)DataPtr; break;
						case 7: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat7*)DataPtr; break;
						case 8: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat8*)DataPtr; break;
						case 10: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat10*)DataPtr; break;
						}

						bUse16BitRGB = RecordRGB && (RecordRGB->Red > 255 || RecordRGB->Green > 255 || RecordRGB->Blue > 255);
					}
				}
			}

			// Read Data
			{
				// Set the correct position for the reader
				Reader->Seek(Header.OffsetToPointData);

				int64 PointsRead = 0;

				// Clear any existing data
				OutImportResults.Points.Empty(TotalPointsToRead);
				OutImportResults.ClassificationsImported.Empty();

				const float IntensityMultiplier = 1.0f / (bUse16BitIntensity ? 65535.0f : 255.0f);
				const float RGBMultiplier = 1.0f / (bUse16BitRGB ? 65535.0f : 255.0f);

				const float ImportScale = GetDefault<ULidarPointCloudSettings>()->ImportScale;

				FThreadSafeBool bFirstPointSet = false;

				// Multi-threading
				FCriticalSection CoordsLock;
				FCriticalSection PointsLock;
				TArray<TFuture<void>> ThreadResults;
				FLidarPointCloudDataBufferManager BufferManager(MaxPointsToRead * Header.PointDataRecordLength);

				// Stream the data
				while (PointsRead < TotalPointsToRead && !OutImportResults.IsCancelled())
				{
					FLidarPointCloudDataBuffer* Buffer = BufferManager.GetFreeBuffer();

					// Data should never be null
					check(Buffer->GetData());

					// Calculate the amount of data to read
					int64 PointsToRead = FMath::Min(MaxPointsToRead, TotalPointsToRead - PointsRead);

					// Read the data
					Reader->Serialize(Buffer->GetData(), PointsToRead * Header.PointDataRecordLength);

					ThreadResults.Add(Async(EAsyncExecution::ThreadPool, [PointsToRead, Buffer, &ImportScale, &bHasIntensityData, &IntensityMultiplier, &RGBMultiplier, &Header, &OutImportResults, &Settings, &bFirstPointSet, &CoordsLock, &PointsLock]
						{
							uint8* Data = Buffer->GetData();

							TArray<FLidarPointCloudPoint> Points;
							Points.Reserve(PointsToRead);

							FBox _Bounds(EForceInit::ForceInit);

							TArray<uint8> Classifications;

							// Parse the data
							for (int64 j = 0; j < PointsToRead && !OutImportResults.IsCancelled(); j++)
							{
								FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon* Record = (FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon*)Data;

								FVector ProcessedLocation;
								float Intensity = bHasIntensityData ? Record->Intensity * IntensityMultiplier : 1.0f;
								float R = 1.0f;
								float G = 1.0f;
								float B = 1.0f;
								uint8 Classification = Header.IsLegacyFormat() ? ((FLidarPointCloudFileIO_LAS_PointDataRecordFormat0*)Data)->Classification : ((FLidarPointCloudFileIO_LAS_PointDataRecordFormat6*)Data)->Classification;

								Classifications.AddUnique(Classification);

								// Extract location information
								{
									// Calculate the actual location of the point, convert to UU and flip the Y axis
									FDoubleVector Location = (Header.ScaleFactor * Record->Location + Header.Offset) * ImportScale;
									Location.Y = -Location.Y;

									if (!bFirstPointSet)
									{
										FScopeLock Lock(&CoordsLock);

										if (!bFirstPointSet)
										{
											OutImportResults.OriginalCoordinates = Location;
											bFirstPointSet = true;
										}
									}

									// Shift to protect from precision loss
									Location -= OutImportResults.OriginalCoordinates;

									_Bounds += Location.ToVector();

									// Convert location to floats
									ProcessedLocation = Location.ToVector();
								}

								// Extract color information
								{
									FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB* RecordRGB = nullptr;

									switch (Header.PointDataRecordFormat)
									{
									case 2: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat2*)Data; break;
									case 3: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat3*)Data; break;
									case 5: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat5*)Data; break;
									case 7: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat7*)Data; break;
									case 8: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat8*)Data; break;
									case 10: RecordRGB = (FLidarPointCloudFileIO_LAS_PointDataRecordFormat10*)Data; break;
									}

									if (RecordRGB)
									{
										R = RecordRGB->Red * RGBMultiplier;
										G = RecordRGB->Green * RGBMultiplier;
										B = RecordRGB->Blue * RGBMultiplier;
									}
								}

								Points.Emplace(ProcessedLocation, R, G, B, Intensity, Classification);

								// Increment the data pointer
								Data += Header.PointDataRecordLength;
							}

							// Data Sync
							{
								FScopeLock Lock(&PointsLock);

								OutImportResults.AddPointsBulk(Points);
								OutImportResults.Bounds += _Bounds;

								for (uint8& Classification : Classifications)
								{
									OutImportResults.ClassificationsImported.AddUnique(Classification);
								}
							}

							Buffer->MarkAsFree();
						}));

					PointsRead += PointsToRead;
				}

				// Sync threads
				for (const TFuture<void>& ThreadResult : ThreadResults)
				{
					ThreadResult.Get();
				}

				// Make sure to progress the counter to the end before returning
				OutImportResults.IncrementProgressCounter(TotalPointsToRead);
			}
			
			bSuccess = !OutImportResults.IsCancelled();
		}

		// Free memory
		Reader->Close();
	}

	return bSuccess;
}

bool ULidarPointCloudFileIO_LAS::HandleExport(const FString& Filename, ULidarPointCloud* PointCloud)
{
	FArchive* Ar = IFileManager::Get().CreateFileWriter(*Filename, 0);
	if (!Ar)
	{
		return false;
	}

	FDoubleVector Min = PointCloud->GetBounds().Min;
	FDoubleVector Max = PointCloud->GetBounds().Max;

	// Flip Y
	float MaxY = Max.Y;
	Max.Y = -Min.Y;
	Min.Y = -MaxY;

	const float ExportScale = GetDefault<ULidarPointCloudSettings>()->ExportScale;

	// Convert to meters
	Min *= ExportScale;
	Max *= ExportScale;

	FLidarPointCloudFileIO_LAS_PublicHeaderBlock Header = FLidarPointCloudFileIO_LAS_PublicHeaderBlock::Generate(PointCloud->GetNumPoints(), Min, Max);

	Ar->Serialize(&Header, Header.HeaderSize);
	
	FDoubleVector Size = Max - Min;
	FDoubleVector ForwardScale(FMath::Pow(2, 31 - FMath::CeilToInt(FMath::Log2(Size.X))), FMath::Pow(2, 31 - FMath::CeilToInt(FMath::Log2(Size.Y))), FMath::Pow(2, 31 - FMath::CeilToInt(FMath::Log2(Size.Z))));

	int64 NumProcessedPoints = 0;
	TArray<FLidarPointCloudPoint*> Points;

	int64 MaxBatchSize = GetDefault<ULidarPointCloudSettings>()->ExportBatchSize;

	FLidarPointCloudFileIO_LAS_PointDataRecordFormat2* PointRecord = new FLidarPointCloudFileIO_LAS_PointDataRecordFormat2();
	FMemory::Memzero(PointRecord, Header.PointDataRecordLength);

	while (NumProcessedPoints < PointCloud->GetNumPoints())
	{
		int64 BatchSize = FMath::Min(MaxBatchSize, PointCloud->GetNumPoints() - NumProcessedPoints);
		PointCloud->GetPoints(Points, NumProcessedPoints, BatchSize);

		for (FLidarPointCloudPoint* Point : Points)
		{
			FDoubleVector Location = (PointCloud->LocationOffset + Point->Location) * ExportScale;
			Location.Y = -Location.Y;

			PointRecord->Location = (ForwardScale * (Location - Min)).ToIntVector();
			PointRecord->Intensity = (Point->Color.A << 8) + Point->Color.A;
			PointRecord->Red = (Point->Color.R << 8) + Point->Color.R;
			PointRecord->Green = (Point->Color.G << 8) + Point->Color.G;
			PointRecord->Blue = (Point->Color.B << 8) + Point->Color.B;
			PointRecord->Classification = Point->ClassificationID;

			Ar->Serialize(PointRecord, Header.PointDataRecordLength);
		}

		NumProcessedPoints += BatchSize;
	}

	delete PointRecord;
	delete Ar;
	return true;
}
