// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/GenerateTextLocalizationReportCommandlet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/LocKeyFuncs.h"
#include "LocTextHelper.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenerateTextLocalizationReportCommandlet, Log, All);

UGenerateTextLocalizationReportCommandlet::UGenerateTextLocalizationReportCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UGenerateTextLocalizationReportCommandlet::Main(const FString& Params)
{
	// Parse command line - we're interested in the param vals
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Set config file.
	const FString* ParamVal = ParamVals.Find(FString(TEXT("Config")));

	if ( ParamVal )
	{
		GatherTextConfigPath = *ParamVal;
	}
	else
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No config specified."));
	}

	// Set config section.
	ParamVal = ParamVals.Find(FString(TEXT("Section")));

	if ( ParamVal )
	{
		SectionName = *ParamVal;
	}
	else
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}

	// Common settings
	FString SourcePath;
	FString DestinationPath;

	// Settings for generating/appending to word count report file
	bool bWordCountReport = false;

	// Settings for generating loc conflict report file
	bool bConflictReport = false;
	
	// Get source path.
	if( !( GetPathFromConfig( *SectionName, TEXT("SourcePath"), SourcePath, GatherTextConfigPath ) ) )
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No source path specified."));
		return -1;
	}

	// Get destination path.
	if( !( GetPathFromConfig( *SectionName, TEXT("DestinationPath"), DestinationPath, GatherTextConfigPath ) ) )
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No destination path specified."));
		return -1;
	}

	// Get the timestamp from the commandline, if not provided we will use the current time.
	const FString* TimeStampParamVal = ParamVals.Find(FString(TEXT("TimeStamp")));
	if ( TimeStampParamVal && !TimeStampParamVal->IsEmpty() )
	{
		CmdlineTimeStamp = *TimeStampParamVal;
	}

	GetBoolFromConfig( *SectionName, TEXT("bWordCountReport"), bWordCountReport, GatherTextConfigPath );
	GetBoolFromConfig( *SectionName, TEXT("bConflictReport"), bConflictReport, GatherTextConfigPath );

	if( bWordCountReport )
	{
		if( !ProcessWordCountReport( SourcePath, DestinationPath ) )
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("Failed to generate word count report."));
			return -1;
		}
	}

	if( bConflictReport )
	{
		if( !ProcessConflictReport( DestinationPath) )
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("Failed to generate localization conflict report."));
			return -1;
		}
	}

	return 0;
}

bool UGenerateTextLocalizationReportCommandlet::ProcessWordCountReport(const FString& SourcePath, const FString& DestinationPath)
{
	FDateTime Timestamp = FDateTime::Now();
	if (!CmdlineTimeStamp.IsEmpty())
	{
		FDateTime::Parse(*CmdlineTimeStamp, Timestamp);
	}

	// Get manifest name.
	FString ManifestName;
	if( !( GetStringFromConfig( *SectionName, TEXT("ManifestName"), ManifestName, GatherTextConfigPath ) ) )
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No manifest name specified."));
		return false;
	}

	// Get archive name.
	FString ArchiveName;
	if( !( GetStringFromConfig( *SectionName, TEXT("ArchiveName"), ArchiveName, GatherTextConfigPath ) ) )
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No archive name specified."));
		return false;
	}

	// Get report name.
	FString WordCountReportName;
	if( !( GetStringFromConfig( *SectionName, TEXT("WordCountReportName"), WordCountReportName, GatherTextConfigPath ) ) )
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No word count report name specified."));
		return false;
	}

	// Get cultures to generate.
	TArray<FString> CulturesToGenerate;
	GetStringArrayFromConfig( *SectionName, TEXT("CulturesToGenerate"), CulturesToGenerate, GatherTextConfigPath );

	for(int32 i = 0; i < CulturesToGenerate.Num(); ++i)
	{
		if( FInternationalization::Get().GetCulture( CulturesToGenerate[i] ).IsValid() )
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Verbose, TEXT("Specified culture is not a valid runtime culture, but may be a valid base language: %s"), *(CulturesToGenerate[i]));
		}
	}

	// Load the manifest and all archives
	FLocTextHelper LocTextHelper(SourcePath, ManifestName, ArchiveName, FString(), CulturesToGenerate, MakeShareable(new FLocFileSCCNotifies(SourceControlInfo)));
	{
		FText LoadError;
		if (!LocTextHelper.LoadAll(ELocTextHelperLoadFlags::LoadOrCreate, &LoadError))
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("%s"), *LoadError.ToString());
			return false;
		}
	}

	const FString ReportFilePath = (DestinationPath / WordCountReportName);

	FText ReportSaveError;
	if (!LocTextHelper.SaveWordCountReport(Timestamp, ReportFilePath, &ReportSaveError))
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("%s"), *ReportSaveError.ToString());
		return false;
	}

	return true;
}

bool UGenerateTextLocalizationReportCommandlet::ProcessConflictReport(const FString& DestinationPath)
{
	// Get report name.
	FString ConflictReportName;
	if (!GetStringFromConfig(*SectionName, TEXT("ConflictReportName"), ConflictReportName, GatherTextConfigPath))
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("No conflict report name specified."));
		return false;
	}
	if (ConflictReportName.Contains(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("Conflict report name has a file extension. Please remove the file extension from the name. EConflictReportFormat is used to determine the file extension."));
		return false;
	}
	EConflictReportFormat ConflictReportFormat = EConflictReportFormat::None;
	FString ConflictReportFormatString;
	if (!GConfig->GetString(*SectionName, TEXT("ConflictReportFormat"), ConflictReportFormatString, GatherTextConfigPath))
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Display, TEXT("Conflict report format not specified. Conflict report will default to CSV format."));
		ConflictReportFormat = EConflictReportFormat::CSV;
	}
	else
	{
		ConflictReportFormatString.TrimStartAndEndInline();
		static const TCHAR* TxtEnumString = TEXT("EConflictReportFormat::Txt");
		static const TCHAR* CSVEnumString = TEXT("EConflictReportFormat::CSV");
		if (ConflictReportFormatString.Equals(TxtEnumString))
		{
			ConflictReportFormat = EConflictReportFormat::Txt;
		}
		else if (ConflictReportFormatString.Equals(CSVEnumString))
		{
			ConflictReportFormat = EConflictReportFormat::CSV;
		}
		else
		{
			// @TODOLocalization: Consider alternatives to defaulting to CSV.
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Warning, TEXT("Specified conflict report format %s in \'%s\' not supported. Defaulting to CSV format."), *ConflictReportFormatString, *GatherTextConfigPath);
			ConflictReportFormat = EConflictReportFormat::CSV;
		}
	}
	switch (ConflictReportFormat)
	{
		case EConflictReportFormat::CSV:
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Display, TEXT("Conflict report format will be in CSV."));
			static const TCHAR* CSVExtension = TEXT(".csv");
			ConflictReportName += CSVExtension;
			break;
		}
		case EConflictReportFormat::Txt:
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Display, TEXT("Conflict report formatwill be in in txt format."));
			static const TCHAR* TxtExtension = TEXT(".txt");
			ConflictReportName += TxtExtension;
			break;
		}
		default:
		{
			UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("Unsupported conflict report format %s detected in %s. Unable to create the appropriate report name and extension."), *ConflictReportFormatString, *GatherTextConfigPath);
			return false;
		}
	}
	const FString ReportFilePath = (DestinationPath / ConflictReportName);

	FText ReportSaveError;
	if (!GatherManifestHelper->SaveConflictReport(ReportFilePath, ConflictReportFormat , &ReportSaveError))
	{
		UE_LOG(LogGenerateTextLocalizationReportCommandlet, Error, TEXT("%s"), *ReportSaveError.ToString());
		return false;
	}

	return true;
}
