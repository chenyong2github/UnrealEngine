// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudFileIO.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#if WITH_EDITOR
#include "Widgets/SWidget.h"
#endif

// char[] { 'L', 'A', 'S', 'F' } read as uint32 - 1179861324
#define HEADER_SIGNATURE 1179861324

#include "LidarPointCloudFileIO_LAS.generated.h"

#pragma pack(push)
#pragma pack(1)

/** Contains the common structure data between all formats */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon
{
	FIntVector Location;
	uint16 Intensity;
};

/** Contains the RGB extension structure data compatible with all formats */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB
{
	uint16 Red;
	uint16 Green;
	uint16 Blue;
};

/** Contains the Waveform extension structure data compatible with all formats */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonWaveform
{
	uint8 WavePacketDescriptorIndex;
	uint64 ByteOffsetToWaveformData;
	uint32 WaveformPacketSize;
	float ReturnPointWaveformLocation;
	float ParametricDX;
	float ParametricDY;
	float ParametricDZ;
};

/**
 * LAS - Point Data Record Format 0
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat0 : FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon
{
	uint8 ReturnNumber : 3;
	uint8 NumberOfReturns : 3;
	uint8 ScanDirectionFlag : 1;
	uint8 EdgeOfFlightLine : 1;
	uint8 Classification;
	int8 ScanAngle;
	uint8 UserData;
	uint16 PointSourceID;
};

/**
 * LAS - Point Data Record Format 1
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat1 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat0
{
	double GPSTime;
};

/**
 * LAS - Point Data Record Format 2
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat2 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat0, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB { };

/**
 * LAS - Point Data Record Format 3
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat3 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat1, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB { };

/**
 * LAS - Point Data Record Format 4
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat4 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat1, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonWaveform { };

/**
 * LAS - Point Data Record Format 5
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat5 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat3, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonWaveform { };

/**
 * LAS - Point Data Record Format 6
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat6 : FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommon
{
	uint8 ReturnNumber : 4;
	uint8 NumberOfReturns : 4;
	uint8 ClassificationFlags : 4;
	uint8 ScannerChannel : 2;
	uint8 ScanDirectionFlag : 1;
	uint8 EdgeOfFlightLine : 1;
	uint8 Classification;
	uint8 UserData;
	int16 ScanAngle;
	uint16 PointSourceID;
	double GPSTime;
};

/**
 * LAS - Point Data Record Format 7
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat7 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat6, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonRGB { };

/**
 * LAS - Point Data Record Format 8
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat8 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat7
{
	uint16 NIR;
};

/**
 * LAS - Point Data Record Format 9
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat9 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat6, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonWaveform { };

/**
 * LAS - Point Data Record Format 10
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PointDataRecordFormat10 : FLidarPointCloudFileIO_LAS_PointDataRecordFormat8, FLidarPointCloudFileIO_LAS_PointDataRecordFormatCommonWaveform { };

/**
 * LAS - Public Header Block
 * As per LAS Specification v.1.4 - R14
 */
struct FLidarPointCloudFileIO_LAS_PublicHeaderBlock
{
	/** Base set, 227 bytes */
	uint32 FileSignature;	// Technically, this should be char[4] but uint32 is faster to compare
	uint16 FileSourceID;
	uint16 GlobalEncoding;
	uint32 ProjectID_GUIDData1;
	uint16 ProjectID_GUIDData2;
	uint16 ProjectID_GUIDData3;
	uint8 ProjectID_GUIDData4[8];
	uint8 VersionMajor;
	uint8 VersionMinor;
	char SystemIdentifier[32];
	char GeneratingSoftware[32];
	uint16 FileCreationDayofYear;
	uint16 FileCreationYear;
	uint16 HeaderSize;
	uint32 OffsetToPointData;
	uint32 NumberOfVLRs;
	uint8 PointDataRecordFormat;
	uint16 PointDataRecordLength;
	uint32 LegacyNumberOfPointRecords;
	uint32 LegacyNumberOfPointsByReturn[5];
	FDoubleVector ScaleFactor;
	FDoubleVector Offset;
	
	/*
	 * Order of data changes depending on the file version used
	 * Pre 1.4: MaxX, MinX, MaxY, MinY, MaxZ, MinZ
	 * 1.4+: MaxX, MaxY, MaxZ, MinX, MinY, MinZ
	 */
	double Bounds[6];

	/** Added in 1.3, extra 8 bytes */
	uint64 StartOfWaveformDataPacketRecord;

	/** Added in 1.4, extra 140 bytes */
	uint64 StartOfFirstEVLR;
	uint32 NumberOfEVLRs;
	uint64 NumberOfPointRecords;
	uint64 NumberOfPointsByReturn[15];

	FORCEINLINE bool IsValid() { return FileSignature == HEADER_SIGNATURE; }
	FORCEINLINE uint64 GetNumberOfPoints() { return VersionMinor < 4 ? LegacyNumberOfPointRecords : NumberOfPointRecords; }

	FORCEINLINE bool IsLegacyFormat() { return PointDataRecordFormat < 6; }

	FORCEINLINE bool HasRGB()
	{
		switch (PointDataRecordFormat)
		{
		case 2:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
			return true;

		default:
			return false;
		}
	}

	FDoubleVector GetMin() const { return VersionMinor == 4 ? FDoubleVector(Bounds[3], Bounds[4], Bounds[5]) : FDoubleVector(Bounds[1], Bounds[3], Bounds[5]); }
	FDoubleVector GetMax() const { return VersionMinor == 4 ? FDoubleVector(Bounds[0], Bounds[1], Bounds[2]) : FDoubleVector(Bounds[0], Bounds[2], Bounds[4]); }
	FDoubleVector GetOrigin() const { return (GetMax() + GetMin()) / 2; }
	FVector GetExtent() const { return ((GetMax() - GetMin()) / 2).ToVector(); }

	void SetMin(const FDoubleVector& Min)
	{
		if (VersionMinor == 4)
		{
			Bounds[3] = Min.X;
			Bounds[4] = Min.Y;
			Bounds[5] = Min.Z;
		}
		else
		{
			Bounds[1] = Min.X;
			Bounds[3] = Min.Y;
			Bounds[5] = Min.Z;
		}
	}
	void SetMax(const FDoubleVector& Max)
	{
		if (VersionMinor == 4)
		{
			Bounds[0] = Max.X;
			Bounds[1] = Max.Y;
			Bounds[2] = Max.Z;
		}
		else
		{
			Bounds[0] = Max.X;
			Bounds[2] = Max.Y;
			Bounds[4] = Max.Z;
		}
	}

	/** Returns true if the point data contains user-specific extra bytes */
	bool PointDataContainsExtraContent()
	{
		uint16 Size = 0;

		switch (PointDataRecordFormat)
		{
		case 0: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat0); break;
		case 1: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat1); break;
		case 2: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat2); break;
		case 3: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat3); break;
		case 4: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat4); break;
		case 5: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat5); break;
		case 6: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat6); break;
		case 7: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat7); break;
		case 8: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat8); break;
		case 9: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat9); break;
		case 10: Size = sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat10); break;
		}

		return PointDataRecordLength != Size;
	}

	FString GetFormatDescription()
	{
		switch (PointDataRecordFormat)
		{
		case 0: return "Legacy Intensity";
		case 1: return "Legacy Intensity with Time";
		case 2: return "Legacy RGB";
		case 3: return "Legacy RGB with Time";
		case 4: return "Legacy Intensity with Time and Waveform";
		case 5: return "Legacy RGB with Time and Waveform";
		case 6: return "Intensity";
		case 7: return "RGB";
		case 8: return "RGB with NIR";
		case 9: return "Intensity with Waveform";
		case 10: return "RGB with NIR and Waveform";
		default: return "Unknown";
		}
	}

	static uint16 GetRecordLengthByFormat(uint8 Format)
	{
		switch (Format)
		{
		case 0: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat0);
		case 1: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat1);
		case 2: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat2);
		case 3: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat3);
		case 4: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat4);
		case 5: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat5);
		case 6: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat6);
		case 7: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat7);
		case 8: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat8);
		case 9: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat9);
		case 10: return sizeof(FLidarPointCloudFileIO_LAS_PointDataRecordFormat10);
		default:
			return 0;
		}
	}

	/** Generates a new 1.2 compliant header, with pre-populated data. */
	static FLidarPointCloudFileIO_LAS_PublicHeaderBlock Generate(const int64& NumberOfPoints, const FDoubleVector& Min, const FDoubleVector& Max)
	{
		FLidarPointCloudFileIO_LAS_PublicHeaderBlock Header;
		FMemory::Memzero(&Header, sizeof(Header));

		Header.FileSignature = HEADER_SIGNATURE;
		Header.VersionMajor = 1;
		Header.VersionMinor = 2;

		const char* SystemIdentifier = "Unreal Engine 4";
		FMemory::Memcpy(Header.SystemIdentifier, SystemIdentifier, 15);

		const char* GeneratingSoftware = "Point Cloud Plugin";
		FMemory::Memcpy(Header.GeneratingSoftware, GeneratingSoftware, 18);

		FDateTime Date = FDateTime::Now();
		Header.FileCreationDayofYear = Date.GetDayOfYear();
		Header.FileCreationYear = Date.GetYear();

		Header.HeaderSize = 227;
		Header.OffsetToPointData = 227;

		Header.PointDataRecordFormat = 2;
		Header.PointDataRecordLength = GetRecordLengthByFormat(Header.PointDataRecordFormat);
		Header.LegacyNumberOfPointRecords = NumberOfPoints;

		FDoubleVector Size = Max - Min;
		Header.Offset = Min;
		Header.ScaleFactor = FDoubleVector(FMath::Pow(2, FMath::CeilToInt(FMath::Log2(Size.X)) - 31), FMath::Pow(2, FMath::CeilToInt(FMath::Log2(Size.Y)) - 31), FMath::Pow(2, FMath::CeilToInt(FMath::Log2(Size.Z)) - 31));
		Header.SetMin(Min);
		Header.SetMax(Max);

		return Header;
	}
};
#pragma pack(pop)

struct FLidarPointCloudImportSettings_LAS : public FLidarPointCloudImportSettings
{
	FLidarPointCloudFileIO_LAS_PublicHeaderBlock PublicHeaderBlock;

public:
	FLidarPointCloudImportSettings_LAS(const FString& Filename);
	virtual bool IsFileCompatible(const FString& InFilename) const override { return true; }
	virtual void Serialize(FArchive& Ar) override;

	virtual FString GetUID() const override { return "FLidarPointCloudImportSettings_LAS"; }

	virtual void SetNewFilename(const FString& NewFilename) override
	{
		FLidarPointCloudImportSettings::SetNewFilename(NewFilename);
		ReadFileHeader(NewFilename);
	}

	virtual TSharedPtr<FLidarPointCloudImportSettings> Clone(const FString& NewFilename = "") override
	{
		TSharedPtr<FLidarPointCloudImportSettings_LAS> NewSettings(new FLidarPointCloudImportSettings_LAS(NewFilename.IsEmpty() ? Filename : NewFilename));
		NewSettings->bImportAll = bImportAll;
		return NewSettings;
	}

private:
	/** Reads and parses header information about the given file. */
	void ReadFileHeader(const FString& InFilename);

	friend class ULidarPointCloudFileIO_LAS;
};

/**
 * Inherits from UBlueprintFunctionLibrary to allow exposure to Blueprint Library in the same class.
 */
UCLASS()
class LIDARPOINTCLOUDRUNTIME_API ULidarPointCloudFileIO_LAS : public UBlueprintFunctionLibrary, public FLidarPointCloudFileIOHandler
{
	GENERATED_BODY()

public:
	virtual bool SupportsImport() const override { return true; }
	virtual bool SupportsExport() const override { return true; }
	
	virtual TSharedPtr<FLidarPointCloudImportSettings> GetImportSettings(const FString& Filename) override { return TSharedPtr<FLidarPointCloudImportSettings>(new FLidarPointCloudImportSettings_LAS(Filename)); }

	virtual bool HandleImport(const FString& Filename, TSharedPtr<FLidarPointCloudImportSettings> ImportSettings, FLidarPointCloudImportResults& OutImportResults) override;
	virtual bool HandleExport(const FString& Filename, class ULidarPointCloud* PointCloud) override;

	ULidarPointCloudFileIO_LAS() { ULidarPointCloudFileIO::RegisterHandler(this, { "LAS" }); }
};

#undef HEADER_SIGNATURE