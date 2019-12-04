// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorDataprepAssetLibrary.h"

#include "DataprepAssetInterface.h"
#include "DataprepCoreUtils.h"

bool UEditorDataprepAssetLibrary::ExecuteDataprep(UDataprepAssetInterface* DataprepAssetInterface, EDataprepReportMethod LogReportingMethod, EDataprepReportMethod ProgressReportingMethod)
{
	if( DataprepAssetInterface )
	{
		TSharedPtr<IDataprepLogger> Logger;
		TSharedPtr<IDataprepProgressReporter> Reporter;

		switch (LogReportingMethod)
		{
		case EDataprepReportMethod::StandartLog:
		case EDataprepReportMethod::SameFeedbackAsEditor:
			Logger = MakeShared<FDataprepCoreUtils::FDataprepLogger>();
			break;
		case EDataprepReportMethod::NoFeedback:
		default:
			break;
		}

		switch (ProgressReportingMethod)
		{
		case EDataprepReportMethod::StandartLog:
			Reporter = MakeShared<FDataprepCoreUtils::FDataprepProgressTextReporter>();
			break;
		case EDataprepReportMethod::SameFeedbackAsEditor:
			Reporter = MakeShared<FDataprepCoreUtils::FDataprepProgressUIReporter>();
			break;
		case EDataprepReportMethod::NoFeedback:
			break;
		default:
			break;
		}

		return FDataprepCoreUtils::ExecuteDataprep( DataprepAssetInterface, Logger, Reporter );
	}

	return false;
}
