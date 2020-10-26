// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeImportHelper.h"
#include "LandscapeEditorModule.h"
#include "LandscapeDataAccess.h"
#include "LandscapeConfigHelper.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "LandscapeImportHelper"

bool FLandscapeImportHelper::ExtractCoordinates(FString BaseFilename, FIntPoint& OutCoord, FString& OutBaseFilePattern)
{
	//We expect file name in form: <tilename>_x<number>_y<number>
	int32 XPos = BaseFilename.Find(TEXT("_x"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	int32 YPos = BaseFilename.Find(TEXT("_y"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (XPos != INDEX_NONE && YPos != INDEX_NONE && XPos < YPos)
	{
		FString XCoord = BaseFilename.Mid(XPos + 2, YPos - (XPos + 2));
		FString YCoord = BaseFilename.Mid(YPos + 2, BaseFilename.Len() - (YPos + 2));

		if (XCoord.IsNumeric() && YCoord.IsNumeric())
		{
			OutBaseFilePattern = BaseFilename.Mid(0, XPos);
			TTypeFromString<int32>::FromString(OutCoord.X, *XCoord);
			TTypeFromString<int32>::FromString(OutCoord.Y, *YCoord);
			return true;
		}
	}

	return false;
}

void FLandscapeImportHelper::GetMatchingFiles(const FString& FilePathPattern, TArray<FString>& OutFileToImport)
{
	IFileManager::Get().IterateDirectoryRecursively(*FPaths::GetPath(FilePathPattern), [&OutFileToImport, &FilePathPattern](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (!bIsDirectory)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.StartsWith(FilePathPattern))
			{
				OutFileToImport.Add(Filename);
			}
		}
		return true;
	});
}

template<class T>
ELandscapeImportResult GetImportDataInternal(const FLandscapeImportDescriptor& ImportDescriptor, int32 DescriptorIndex, FName LayerName, T DefaultValue, TArray<T>& OutData, FText& OutMessage)
{
	if (DescriptorIndex < 0 || DescriptorIndex >= ImportDescriptor.ImportResolutions.Num())
	{
		OutMessage = LOCTEXT("Import_InvalidDescriptorIndex", "Invalid Descriptor Index");
		return ELandscapeImportResult::Error;
	}
		
	if (!ImportDescriptor.FileDescriptors.Num() || ImportDescriptor.ImportResolutions.Num() != ImportDescriptor.FileResolutions.Num())
	{
		OutMessage = LOCTEXT("Import_InvalidDescriptor", "Invalid Descriptor");
		return ELandscapeImportResult::Error;
	}
	
	int32 TotalWidth = ImportDescriptor.ImportResolutions[DescriptorIndex].Width;
	int32 TotalHeight = ImportDescriptor.ImportResolutions[DescriptorIndex].Height;

	OutData.Reset();
	OutData.SetNumZeroed(TotalWidth * TotalHeight);
	// Initialize All to default value so that non-covered regions have data
	TArray<T> StrideData;
	StrideData.SetNumUninitialized(TotalWidth);
	for (int32 X = 0; X < TotalWidth; ++X)
	{
		StrideData[X] = DefaultValue;
	}
	for (int32 Y = 0; Y < TotalHeight; ++Y)
	{
		FMemory::Memcpy(&OutData[Y * TotalWidth], StrideData.GetData(), sizeof(T) * TotalWidth);
	}

	ELandscapeImportResult Result = ELandscapeImportResult::Success;
	// Import Regions
	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
	const ILandscapeFileFormat<T>* FileFormat = LandscapeEditorModule.GetFormatByExtension<T>(*FPaths::GetExtension(ImportDescriptor.FileDescriptors[0].FilePath, true));
	check(FileFormat);

	int32 FileWidth = ImportDescriptor.FileResolutions[DescriptorIndex].Width;
	int32 FileHeight = ImportDescriptor.FileResolutions[DescriptorIndex].Height;
	
	for (const FLandscapeImportFileDescriptor& FileDescriptor : ImportDescriptor.FileDescriptors)
	{
		FLandscapeImportData<T> ImportData = FileFormat->Import(*FileDescriptor.FilePath, LayerName, ImportDescriptor.FileResolutions[DescriptorIndex]);
		OutMessage = ImportData.ErrorMessage;
		Result = ImportData.ResultCode;
		if (ImportData.ResultCode == ELandscapeImportResult::Error)
		{
			break;
		}
		
		int32 StartX = FileDescriptor.Coord.X * FileWidth;
		int32 StartY = FileDescriptor.Coord.Y * FileHeight;
		
		for (int32 Y = 0; Y < FileHeight; ++Y)
		{		
			int32 DestY = StartY + Y;
			FMemory::Memcpy(&OutData[DestY * TotalWidth + StartX], &ImportData.Data[Y * FileWidth], FileWidth * sizeof(T));
		}
	}

	return Result;
}

template<class T>
ELandscapeImportResult GetImportDescriptorInternal(const FString& FilePath, bool bSingleFile, FName LayerName, FLandscapeImportDescriptor& OutImportDescriptor, FText& OutMessage)
{
	OutImportDescriptor.Reset();
	if (FilePath.IsEmpty())
	{
		OutMessage = LOCTEXT("Import_InvalidPath", "Invalid file");
		return ELandscapeImportResult::Error;
	}
		
	FIntPoint OutCoord;
	FIntPoint MinCoord(INT32_MAX, INT32_MAX); // All coords should be rebased to the min
	FIntPoint MaxCoord(INT32_MIN, INT32_MIN);
	FString OutFileImportPattern;
	FString FilePathPattern;
	TArray<FString> OutFilesToImport;
	if (!bSingleFile && FLandscapeImportHelper::ExtractCoordinates(FPaths::GetBaseFilename(FilePath), OutCoord, OutFileImportPattern))
	{
		FilePathPattern = FPaths::GetPath(FilePath) / OutFileImportPattern;
		FLandscapeImportHelper::GetMatchingFiles(FilePathPattern, OutFilesToImport);
	}
	else
	{
		bSingleFile = true;
		OutFilesToImport.Add(FilePath);
	}

	TArray<FLandscapeFileResolution> ImportResolutions;

	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
	const ILandscapeFileFormat<T>* FileFormat = nullptr;
	for (const FString& ImportFilename : OutFilesToImport)
	{
		const bool bFirst = FileFormat == nullptr;
		const ILandscapeFileFormat<T>* CurrentFileFormat = LandscapeEditorModule.GetFormatByExtension<T>(*FPaths::GetExtension(ImportFilename, true));
		if (FileFormat != nullptr && FileFormat != CurrentFileFormat)
		{
			OutMessage = LOCTEXT("Import_MismatchFileType", "Not all files have the same file type");
			return ELandscapeImportResult::Error;
		}
		FileFormat = CurrentFileFormat;

		if (FileFormat)
		{
			FLandscapeFileInfo FileInfo = FileFormat->Validate(*ImportFilename);
			if (FileInfo.ResultCode == ELandscapeImportResult::Error)
			{
				OutMessage = FileInfo.ErrorMessage;
				return FileInfo.ResultCode;
			}

			FString OutLocalFileImportPattern;
			if (!bSingleFile && !FLandscapeImportHelper::ExtractCoordinates(FPaths::GetBaseFilename(ImportFilename), OutCoord, OutLocalFileImportPattern))
			{
				OutMessage = FText::Format(LOCTEXT("Import_InvalidFilename", "File '{0}' doesn't have proper pattern(ex: {1}_x0_y0.{2})"),FText::FromString(FPaths::GetBaseFilename(ImportFilename)), FText::FromString(OutFileImportPattern), FText::FromString(FPaths::GetExtension(FilePath)));
				return ELandscapeImportResult::Error;
			}
			MinCoord.X = FMath::Min(OutCoord.X, MinCoord.X);
			MinCoord.Y = FMath::Min(OutCoord.Y, MinCoord.Y);
			MaxCoord.X = FMath::Max(OutCoord.X, MaxCoord.X);
			MaxCoord.Y = FMath::Max(OutCoord.Y, MaxCoord.Y);

			FLandscapeImportFileDescriptor FileDescriptor(ImportFilename, OutCoord);
			OutImportDescriptor.FileDescriptors.Add(FileDescriptor);

			if (bFirst)
			{
				// Resolutions will need to match for all files (keep the first one to compare)
				OutImportDescriptor.FileResolutions = MoveTemp(FileInfo.PossibleResolutions);
								
				if (FileInfo.DataScale.IsSet())
				{
					OutImportDescriptor.Scale = FileInfo.DataScale.GetValue();
					OutImportDescriptor.Scale.Z *= LANDSCAPE_INV_ZSCALE;
				}
			}
			else
			{
				if (OutImportDescriptor.FileResolutions != FileInfo.PossibleResolutions)
				{
					OutMessage = LOCTEXT("Import_MismatchResolution", "Not all files have the same resolution");
					return ELandscapeImportResult::Error;
				}
				else if (FileInfo.DataScale.IsSet())
				{
					FVector CurrentScale = FileInfo.DataScale.GetValue();
					CurrentScale.Z *= LANDSCAPE_INV_ZSCALE;

					if (!OutImportDescriptor.Scale.Equals(CurrentScale))
					{
						OutMessage = LOCTEXT("Import_MismatchScale", "Not all files have the same data scale");
						return ELandscapeImportResult::Error;
					}
				}
			}
		}
		else
		{
			OutMessage = LOCTEXT("Import_UnknownFileType", "File type not recognized");
			return ELandscapeImportResult::Error;
		}
	}

	check(OutImportDescriptor.FileDescriptors.Num());
	// Rebase with MinCoord
	for (FLandscapeImportFileDescriptor& FileDescriptor : OutImportDescriptor.FileDescriptors)
	{
		FileDescriptor.Coord -= MinCoord;
	}
	MaxCoord -= MinCoord;

	// Compute Import Total Size
	for (const FLandscapeFileResolution& Resolution : OutImportDescriptor.FileResolutions)
	{
		OutImportDescriptor.ImportResolutions.Add(FLandscapeImportResolution((MaxCoord.X+1)*Resolution.Width, (MaxCoord.Y+1)*Resolution.Height));
	}

	return ELandscapeImportResult::Success;
}

template<class T>
void ExpandImportDataInternal(const TArray<T>& InData, TArray<T>& OutData, const FLandscapeImportResolution& CurrentResolution, const FLandscapeImportResolution& RequiredResolution)
{
	check(InData.Num() == CurrentResolution.Width * CurrentResolution.Height);
	// Center Import data
	const int32 OffsetX = (int32)(RequiredResolution.Width - CurrentResolution.Width) / 2;
	const int32 OffsetY = (int32)(RequiredResolution.Height - CurrentResolution.Height) / 2;

	FIntRect SrcRegion(0, 0, CurrentResolution.Width - 1, CurrentResolution.Height - 1);
	FIntRect DestRegion(-OffsetX, -OffsetY, RequiredResolution.Width - OffsetX - 1, RequiredResolution.Height - OffsetY - 1);
	FLandscapeConfigHelper::ExpandData<T>(InData, OutData, SrcRegion, DestRegion, true);
}

ELandscapeImportResult FLandscapeImportHelper::GetHeightmapImportDescriptor(const FString& FilePath, bool bSingleFile, FLandscapeImportDescriptor& OutImportDescriptor, FText& OutMessage)
{
	return GetImportDescriptorInternal<uint16>(FilePath, bSingleFile, NAME_None, OutImportDescriptor, OutMessage);
}

ELandscapeImportResult FLandscapeImportHelper::GetWeightmapImportDescriptor(const FString& FilePath, bool bSingleFile, FName LayerName, FLandscapeImportDescriptor& OutImportDescriptor, FText& OutMessage)
{
	return GetImportDescriptorInternal<uint8>(FilePath, bSingleFile, LayerName, OutImportDescriptor, OutMessage);
}

ELandscapeImportResult FLandscapeImportHelper::GetHeightmapImportData(const FLandscapeImportDescriptor& ImportDescriptor, int32 DescriptorIndex, TArray<uint16>& OutData, FText& OutMessage)
{
	return GetImportDataInternal<uint16>(ImportDescriptor, DescriptorIndex, NAME_None, LandscapeDataAccess::MidValue, OutData, OutMessage);
}

ELandscapeImportResult FLandscapeImportHelper::GetWeightmapImportData(const FLandscapeImportDescriptor& ImportDescriptor, int32 DescriptorIndex, FName LayerName, TArray<uint8>& OutData, FText& OutMessage)
{
	return GetImportDataInternal<uint8>(ImportDescriptor, DescriptorIndex, LayerName, 0, OutData, OutMessage);
}

void FLandscapeImportHelper::ExpandWeightmapImportData(const TArray<uint8>& InData, TArray<uint8>& OutData, const FLandscapeImportResolution& CurrentResolution, const FLandscapeImportResolution& RequiredResolution)
{
	ExpandImportDataInternal<uint8>(InData, OutData, CurrentResolution, RequiredResolution);
}

void FLandscapeImportHelper::ExpandHeightmapImportData(const TArray<uint16>& InData, TArray<uint16>& OutData, const FLandscapeImportResolution& CurrentResolution, const FLandscapeImportResolution& RequiredResolution)
{
	ExpandImportDataInternal<uint16>(InData, OutData, CurrentResolution, RequiredResolution);
}

void FLandscapeImportHelper::ChooseBestComponentSizeForImport(int32 Width, int32 Height, int32& InOutQuadsPerSection, int32& InOutSectionsPerComponent, FIntPoint& OutComponentCount)
{
	bool bValidSubsectionSizeParam = false;
	bool bValidQuadsPerSectionParam = false;
	check(Width > 0 && Height > 0);
		
	bool bFoundMatch = false;
	// Try to find a section size and number of sections that exactly matches the dimensions of the heightfield
	for (int32 SectionSizesIdx = UE_ARRAY_COUNT(FLandscapeConfig::SubsectionSizeQuadsValues) - 1; SectionSizesIdx >= 0; SectionSizesIdx--)
	{
		for (int32 NumSectionsIdx = UE_ARRAY_COUNT(FLandscapeConfig::NumSectionValues) - 1; NumSectionsIdx >= 0; NumSectionsIdx--)
		{
			int32 ss = FLandscapeConfig::SubsectionSizeQuadsValues[SectionSizesIdx];
			int32 ns = FLandscapeConfig::NumSectionValues[NumSectionsIdx];

			// Check if the passed in values are found in the array of valid values
			bValidSubsectionSizeParam |= (InOutSectionsPerComponent == ns);
			bValidQuadsPerSectionParam |= (InOutQuadsPerSection == ss);

			if (((Width - 1) % (ss * ns)) == 0 && ((Width - 1) / (ss * ns)) <= 32 &&
				((Height - 1) % (ss * ns)) == 0 && ((Height - 1) / (ss * ns)) <= 32)
			{
				bFoundMatch = true;
				InOutQuadsPerSection = ss;
				InOutSectionsPerComponent = ns;
				OutComponentCount.X = (Width - 1) / (ss * ns);
				OutComponentCount.Y = (Height - 1) / (ss * ns);
				break;
			}
		}
		if (bFoundMatch)
		{
			break;
		}
	}

	if (!bFoundMatch)
	{
		if (!bValidSubsectionSizeParam)
		{
			InOutSectionsPerComponent = FLandscapeConfig::NumSectionValues[0];
		}

		if (!bValidQuadsPerSectionParam)
		{
			InOutSectionsPerComponent = FLandscapeConfig::SubsectionSizeQuadsValues[0];
		}

		// if there was no exact match, try increasing the section size until we encompass the whole heightmap
		const int32 CurrentSectionSize = InOutQuadsPerSection;
		const int32 CurrentNumSections = InOutSectionsPerComponent;
		for (int32 SectionSizesIdx = 0; SectionSizesIdx < UE_ARRAY_COUNT(FLandscapeConfig::SubsectionSizeQuadsValues); SectionSizesIdx++)
		{
			if (FLandscapeConfig::SubsectionSizeQuadsValues[SectionSizesIdx] < CurrentSectionSize)
			{
				continue;
			}

			const int32 ComponentsX = FMath::DivideAndRoundUp((Width - 1), FLandscapeConfig::SubsectionSizeQuadsValues[SectionSizesIdx] * CurrentNumSections);
			const int32 ComponentsY = FMath::DivideAndRoundUp((Height - 1), FLandscapeConfig::SubsectionSizeQuadsValues[SectionSizesIdx] * CurrentNumSections);
			if (ComponentsX <= 32 && ComponentsY <= 32)
			{
				bFoundMatch = true;
				InOutQuadsPerSection = FLandscapeConfig::SubsectionSizeQuadsValues[SectionSizesIdx];
				OutComponentCount.X = ComponentsX;
				OutComponentCount.Y = ComponentsY;
				break;
			}
		}
	}

	if (!bFoundMatch)
	{
		// if the heightmap is very large, fall back to using the largest values we support
		const int32 MaxSectionSize = FLandscapeConfig::SubsectionSizeQuadsValues[UE_ARRAY_COUNT(FLandscapeConfig::SubsectionSizeQuadsValues) - 1];
		const int32 MaxNumSubSections = FLandscapeConfig::NumSectionValues[UE_ARRAY_COUNT(FLandscapeConfig::NumSectionValues) - 1];
		const int32 ComponentsX = FMath::DivideAndRoundUp((Width - 1), MaxSectionSize * MaxNumSubSections);
		const int32 ComponentsY = FMath::DivideAndRoundUp((Height - 1), MaxSectionSize * MaxNumSubSections);

		bFoundMatch = true;
		InOutQuadsPerSection = MaxSectionSize;
		InOutSectionsPerComponent = MaxNumSubSections;
		OutComponentCount.X = ComponentsX;
		OutComponentCount.Y = ComponentsY;
	}

	check(bFoundMatch);
}

#undef LOCTEXT_NAMESPACE
