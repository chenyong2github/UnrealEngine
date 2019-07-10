// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Root data container. Render all over basic
class FPicpProjectionOverlayBase
{
public:
	FPicpProjectionOverlayBase()
		: bIsEnabled(false)
	{ }

	virtual ~FPicpProjectionOverlayBase()
	{ }

	void SetEnable(bool bValue)
	{ bIsEnabled = bValue; }
	
	bool IsEnabled() const 
	{ return bIsEnabled; }

private:
	bool bIsEnabled;
};
