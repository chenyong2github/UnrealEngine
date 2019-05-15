// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HttpConnectionContext.h"
#include "CoreMinimal.h"

FHttpConnectionContext::FHttpConnectionContext()
{
	ErrorBuilder.SetAutoEmitLineTerminator(true);
}

FHttpConnectionContext::~FHttpConnectionContext()
{
}

void FHttpConnectionContext::AddElapsedIdleTime(float DeltaTime)
{
	ElapsedIdleTime += DeltaTime;
}

float FHttpConnectionContext::GetElapsedIdleTime() const
{
	return ElapsedIdleTime;
}

const FString& FHttpConnectionContext::GetErrorStr() const
{
	return ErrorBuilder;
}

void FHttpConnectionContext::AddError(const TCHAR* ErrorCode)
{
	ErrorBuilder.Append(ErrorCode);
	ErrorBuilder.AppendChar(TCHAR(' '));
}
