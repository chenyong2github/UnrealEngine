// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/HttpRequestImpl.h"

/**
 * Contains implementation of some common functions that don't vary between implementations of different platforms
 */
class HTTP_API FHttpRequestCommon : public FHttpRequestImpl
{
public:
	// IHttpRequest
	virtual EHttpRequestStatus::Type GetStatus() const override;

protected:
	/**
	 * Check if this request is valid or allowed, before actually process the request
	 */
	bool PreCheck() const;

protected:
	/** Current status of request being processed */
	EHttpRequestStatus::Type CompletionStatus = EHttpRequestStatus::NotStarted;
};
