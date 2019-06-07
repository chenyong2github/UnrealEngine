// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class FText;
class UObject;

/**
 * This is the interface that a progress reporter must implement to work with the Dataprep core functionalities
 */
class IDataprepProgressReporter
{
public:
	/**
	 * Records the progress of an operation
	 * @param Progress The progress of the made since the last call. (1.0 = 100% and 0.0 = 0%)
	 * @param InObject The object that reported the progress
	 */
	virtual void ReportProgress(float Progress, const UObject& InObject) = 0;

	/**
	 * Records the progress of an operation
	 * @param Progress The progress of the made since the last call. (1.0 = 100% and 0.0 = 0%)
	 * @param Message This allow the user to change the displayed message.
	 * @param InObject The object that reported the progress
	 */
	virtual void ReportProgressWithMessage(float Progress, const FText& InMessage, const UObject& InObject) = 0;

	virtual ~IDataprepProgressReporter() = default;
};
