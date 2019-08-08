// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class FText;
class UObject;

/**
 * This is the interface that a progress reporter must implement to work with FDataprepProgressTask
 * 
 */
class IDataprepProgressReporter
{
public:
	virtual ~IDataprepProgressReporter() = default;

protected:
	/**
	 * Push a new task on the stack of started tasks
	 * @param InDescription		Text to be displayed for the new task
	 * @param InAmountOfWork	Total amount of work for the new task
	 */
	virtual void PushTask( const FText& InDescription, float InAmountOfWork ) = 0;

	/** Pop out the current task */
	virtual void PopTask() = 0;

	/**
	 * Report foreseen progress on the current task
	 * @param IncrementOfWork	Amount of progress foreseen until the next call
	 * @param InMessage			Message to be displayed along side the reported progress
	 */
	virtual void ReportProgress( float IncrementOfWork, const FText& InMessage ) = 0;

	friend class FDataprepProgressTask;
};

class DATAPREPCORE_API FDataprepProgressTask
{
public:
	/**
	 * Report foreseen progress on the current task
	 * @param InDescription			Description of the task
	 * @param InAmountOfWork		Total amount of work for the task
	 * @param InIncrementOfWork		Amount of incremental work at each step within the task
	 */
	FDataprepProgressTask( IDataprepProgressReporter& InReporter, const FText& InDescription, float InAmountOfWork, float InIncrementOfWork = 1.0f );

	~FDataprepProgressTask();

	/**
	 * Report foreseen incremental amount of work until next call
	 * @param InMessage				Message to be displayed along side the reported progress
	 * @param InIncrementOfWork		Amount of incremental work foreseen during the next step
	 */
	void ReportNextStep( const FText& InMessage, float InIncrementOfWork );

	/**
	 * Report foreseen default incremental amount of work until next call
	 * @param InMessage		Message to be displayed along side the reported progress
	 */
	void ReportNextStep( const FText& InMessage )
	{
		ReportNextStep( InMessage, DefaultIncrementOfWork );
	}

private:
	/** Dataprep progress reporter associated with the task */
	IDataprepProgressReporter& Reporter;

	/** Default incremental amount of work for each step constituting the task */
	float DefaultIncrementOfWork;
};