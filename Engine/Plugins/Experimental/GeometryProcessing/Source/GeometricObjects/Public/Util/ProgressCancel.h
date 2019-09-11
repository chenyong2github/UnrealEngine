// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp ProgressCancel

#pragma once


/**
 * FProgressCancel is an obejct that is intended to be passed to long-running
 * computes to do two things:
 * 1) provide progress info back to caller (not implemented yet)
 * 2) allow caller to cancel the computation
 */
class FProgressCancel
{
private:
	bool WasCancelled = false;  // will be set to true if CancelF() ever returns true

public:
	TFunction<bool()> CancelF = []() { return false; };

	/**
	 * @return true if client would like to cancel operation
	 */
	bool Cancelled()
	{
		if (WasCancelled)
		{
			return true;
		}
		WasCancelled = CancelF();
		return WasCancelled;
	}
};
