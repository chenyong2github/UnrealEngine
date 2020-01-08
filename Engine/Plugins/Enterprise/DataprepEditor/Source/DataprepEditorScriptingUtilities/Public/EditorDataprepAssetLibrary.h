// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorDataprepAssetLibrary.generated.h"

class UDataprepAssetInterface;
class IDataprepLogger;
class IDataprepProgressReporter;

UENUM(BlueprintType)
enum class EDataprepReportMethod : uint8
{
	// Report the feedback into the output log only
	StandartLog,

	// Report the feedback the same way that the dataprep asset editor does (might not work while using a commandlet)
	SameFeedbackAsEditor,

	// Don't report the feedback
	NoFeedback,
};

UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Dataprep Core Blueprint Library"))
class DATAPREPEDITORSCRIPTINGUTILITIES_API UEditorDataprepAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Runs the Dataprep asset's producers, execute its recipe and finally runs the consumer to output the results.
	 * @param	DataprepAssetInterface		Dataprep asset to run.
	 * @param	LogReportingMethod		Chose the way the log from the producers, operations and consumer will be reported (this will only affect the log from dataprep).
	 * @param	ProgressReportingMethod		The way that the progress updates will be reported
	 * @return	True if successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint | Dataprep")
	static bool ExecuteDataprep(UDataprepAssetInterface* DataprepAssetInterface, EDataprepReportMethod LogReportingMethod, EDataprepReportMethod ProgressReportingMethod);
};
