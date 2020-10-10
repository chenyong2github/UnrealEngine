// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"


class FDMXPIEManager :
	public TSharedFromThis<FDMXPIEManager>
{
public:
	/** Constructor */
	FDMXPIEManager();

	/** Destructor */
	virtual ~FDMXPIEManager();

private:
	/** Called when play in editor starts */
	void OnBeginPIE(const bool bIsSimulating);

	/** Called when play in editor ends */
	void OnEndPIE(const bool bIsSimulating);

	/** Clears DMX Buffers */
	void ZeroAllBuffers();
};
