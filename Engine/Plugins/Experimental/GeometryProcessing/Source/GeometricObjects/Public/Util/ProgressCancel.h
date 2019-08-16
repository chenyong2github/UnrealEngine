// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp ProgressCancel

#pragma once


/**
 * ICancelSource is an object that provides a cancel function
 */
class ICancelSource
{
public:
	virtual ~ICancelSource() {}

	/**
	 * @return true if object wishes to cancel expensive operation
	 */
	virtual bool Cancelled() = 0;
};


/**
 * FCancelFunction uses a TFunction to implement the ICancelSource interface
 */
class FCancelFunction : public ICancelSource
{
public:
	virtual ~FCancelFunction() {}

	/** function that returns true if object wishes to cancel operation */
	TFunction<bool()> CancelF;

	FCancelFunction(const TFunction<bool()> & cancelF) : CancelF(cancelF)
	{
	}

	/**
	 * @return true if object wishes to cancel expensive operation
	 */
	bool Cancelled() { return CancelF(); }
};



/**
 * FProgressCancel is an obejct that is intended to be passed to long-running
 * computes to do two things:
 * 1) provide progress info back to caller (not ported yet)
 * 2) allow caller to cancel the computation
 */
class FProgressCancel
{
public:
	TSharedPtr<ICancelSource> Source;

	bool WasCancelled = false;  // will be set to true if CancelF() ever returns true

	FProgressCancel(TSharedPtr<ICancelSource> source)
	{
		Source = source;
	}

	/**
	 * @return true if client would like to cancel operation
	 */
	bool Cancelled()
	{
		if (WasCancelled)
		{
			return true;
		}
		WasCancelled = Source->Cancelled();
		return WasCancelled;
	}
};
