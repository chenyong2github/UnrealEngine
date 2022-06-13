// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Timespan.h"

class FTelemetryStopwatch
{
public:
	FTelemetryStopwatch(const FString& InAction, const FString& InProject);
	~FTelemetryStopwatch();

	FTimespan Stop(const FString& InResult);
	FTimespan GetElapsed() const;

private:
	const FString Action;
	const FString Project;
	const FDateTime StartTime;
	FString Result;
	FDateTime EndTime;
};

class FTelemetryWriter
{
public:
	FTelemetryWriter(const FString& InSqlConnectionString, const FString& InLogFileName);
	~FTelemetryWriter();
};
