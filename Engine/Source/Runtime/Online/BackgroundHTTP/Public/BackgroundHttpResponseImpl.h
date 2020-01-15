// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IBackgroundHttpResponse.h"

/**
 * Contains implementation of some common functions that don't vary between implementation
 */
class FBackgroundHttpResponseImpl 
	: public IBackgroundHttpResponse
{
public:
	FBackgroundHttpResponseImpl();
	virtual ~FBackgroundHttpResponseImpl() {}

	//IHttpBackgroundResponse
	virtual int32 GetResponseCode() const;
	virtual const FString& GetTempContentFilePath() const;

protected:	
	FString TempContentFilePath;
	int32 ResponseCode;
};
