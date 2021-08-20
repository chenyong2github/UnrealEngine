// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp ProgressCancel

#pragma once

#include "Templates/Function.h"
#include "Containers/Array.h"
#include "Internationalization/Text.h"
#include "Misc/DateTime.h"


class FProgressCancel;


namespace UE
{
namespace Geometry
{


/**
 * FGeometryError represents an error code/message emitted by a geometry operation.
 * The intention of Errors is that they are fatal, IE if an operation emits Errors then
 * it did not complete successfully. If that is not the case, use a FGeometryWarning instead.
 */
struct FGeometryError
{
	int32 ErrorCode = 0;
	FText Message;
	FDateTime Timestamp;
	TArray<unsigned char> CustomData;

	FGeometryError() 
	{ 
		Timestamp = FDateTime::Now();
	}

	FGeometryError(int32 Code, const FText& MessageIn) : ErrorCode(Code), Message(MessageIn) 
	{
		Timestamp = FDateTime::Now();
	}
};

/**
 * FGeometryWarning represents a warning code/message emitted by a geometry operation.
 * The intention of Warnings is that they are non-fatal, IE an operation might successfully
 * complete but still emit Warnings
 */
struct FGeometryWarning
{
	int32 WarningCode = 0;
	FText Message;
	FDateTime Timestamp;
	TArray<unsigned char> CustomData;


	FGeometryWarning()
	{
		Timestamp = FDateTime::Now();
	}

	FGeometryWarning(int32 Code, const FText& MessageIn) : WarningCode(Code), Message(MessageIn)
	{
		Timestamp = FDateTime::Now();
	}
};


/**
 * EGeometryResultType is a generic result-code for use by geometry operations.
 */
enum class EGeometryResultType
{
	/** The Geometry Operation successfully completed */
	Success = 0,
	/** The Geometry Operation is in progress (this is intended for use in cases where there is a background computation that can be queried for incremental status/etc) */
	InProgress = 1,
	/** The Geometry Operation was Cancelled and did not complete */
	Cancelled = 2,
	/** The Geometry Operation completed but was not fully successful (eg some sub-computation failed and was gracefully handled, etc) */
	PartialResult = 3,
	/** The Geometry Operation failed and no valid result was produced */
	Failure = 4
};


/**
 * FGeometryResult represents a combined "success/failure/etc" state for a geometry operation
 * along with a set of Error and Warning codes/messages. 
 */
struct FGeometryResult
{
	EGeometryResultType Result;
	TArray<FGeometryError> Errors;
	TArray<FGeometryWarning> Warnings;


	FGeometryResult()
	{
		Result = EGeometryResultType::Success;
	}

	FGeometryResult(EGeometryResultType ResultType) 
	{
		Result = ResultType;
	}

	void UpdateResultType(EGeometryResultType NewType)
	{
		Result = NewType;
	}

	void SetFailed() { Result = EGeometryResultType::Failure; }
	void SetCancelled() { Result = EGeometryResultType::Cancelled; }

	void SetSuccess() { Result = EGeometryResultType::Success; }

	/**
	 * Set to Success/Failure based on bSuccess, or Cancelled if the (optional) FProgressCancel indicates that it was Cancelled
	 */
	inline void SetSuccess(bool bSuccess, FProgressCancel* Progress = nullptr );

	/**
	 * Set state of the Result to Failure, and append a FGeometryError with the given ErrorMessage and ResultCode
	 */
	inline void SetFailed(FText ErrorMessage, int ResultCode = 0);

	/**
	 * Test if the given Progress has been cancelled, if so, set the Result to Cancelled 
	 * @return true if cancelled, false if not cancelled
	 */
	inline bool CheckAndSetCancelled( FProgressCancel* Progress );

	/**
	 * Append an Error to the result
	 */
	void AddError(FGeometryError Error) 
	{ 
		Errors.Add(Error); 
	}
	
	/**
	 * Append a Warning to the result
	 */
	void AddWarning(FGeometryWarning Warning)
	{ 
		Warnings.Add(Warning); 
	}

	/**
	 * @return true if the geometry operation failed
	 */
	bool HasFailed() const { return Result == EGeometryResultType::Failure; }

	/**
	 * @return true if the geometry operation has some result (ie was a Success or returned a PartialResult)
	 */
	bool HasResult() const { return Result == EGeometryResultType::Success || Result == EGeometryResultType::PartialResult; }

	static FGeometryResult Failed() { return FGeometryResult(EGeometryResultType::Failure); }
	static FGeometryResult Cancelled() { return FGeometryResult(EGeometryResultType::Cancelled); }
};



} // end namespace UE::Geometry
} // end namespace UE





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


public:

	enum class EMessageLevel
	{
		// Note: Corresponds to EToolMessageLevel in InteractiveToolsFramework/ToolContextInterfaces.h

		/** Development message goes into development log */
		Internal = 0,
		/** User message should appear in user-facing log */
		UserMessage = 1,
		/** Notification message should be shown in a non-modal notification window */
		UserNotification = 2,
		/** Warning message should be shown in a non-modal notification window with panache */
		UserWarning = 3,
		/** Error message should be shown in a modal notification window */
		UserError = 4
	};

	struct FMessageInfo
	{
		FText MessageText;
		EMessageLevel MessageLevel;
		FDateTime Timestamp;
	};

	void AddWarning(const FText& MessageText, EMessageLevel MessageLevel)
	{
		Warnings.Add(FMessageInfo{ MessageText , MessageLevel, FDateTime::Now() });
	}

	TArray<FMessageInfo> Warnings;
};




void UE::Geometry::FGeometryResult::SetSuccess(bool bSuccess, FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		Result = EGeometryResultType::Cancelled;
	}
	else
	{
		Result = (bSuccess) ? EGeometryResultType::Success : EGeometryResultType::Failure;
	}
}


void UE::Geometry::FGeometryResult::SetFailed(FText ErrorMessage, int ResultCode)
{
	Result = EGeometryResultType::Failure;
	Errors.Add(FGeometryError(ResultCode, ErrorMessage));
}


bool UE::Geometry::FGeometryResult::CheckAndSetCancelled(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		Result = EGeometryResultType::Cancelled;
		return true;
	}
	return false;
}