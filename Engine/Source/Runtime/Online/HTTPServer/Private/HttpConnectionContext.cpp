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

void FHttpConnectionContext::AddError(const FString& ErrorCode)
{
	ErrorBuilder.Append(ErrorCode);
	ErrorBuilder.AppendChar(TCHAR(' '));
}
