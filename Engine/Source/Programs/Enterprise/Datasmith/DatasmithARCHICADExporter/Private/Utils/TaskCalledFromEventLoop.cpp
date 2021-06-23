// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskCalledFromEventLoop.h"
#include "../ResourcesIDs.h"
#include "Error.h"

#include <atomic>

BEGIN_NAMESPACE_UE_AC

enum : GSType
{
	UEDirectLinkTask = 'DLTk'
};
enum : Int32
{
	CmdDoTask = 1
};

class FTaskParameters
{
  public:
	FTaskCalledFromEventLoop::ERetainType RetainType;
	union
	{
		TWeakPtr< FTaskCalledFromEventLoop >*   WeakPtr;
		TSharedRef< FTaskCalledFromEventLoop >* SharedRef;
	} u;
};

static std::atomic< int32 > SPendingTaskCount(0);

// Run the task if it's not already deleted
GSErrCode FTaskCalledFromEventLoop::DoTasks(GSHandle ParamHandle)
{
	if (ParamHandle)
	{
		FTaskParameters& TaskParameters = reinterpret_cast< FTaskParameters& >(**ParamHandle);
		if (TaskParameters.RetainType == FTaskCalledFromEventLoop::kSharedRef)
		{
			TaskParameters.u.SharedRef->Get().Run();
		}
		else
		{
			TSharedPtr< FTaskCalledFromEventLoop > TaskPtr = TaskParameters.u.WeakPtr->Pin();
			if (TaskPtr.IsValid())
			{
				TaskPtr->Run();
			}
		}
	}
	return ErrParam;
}

// Run the task if it's not already deleted
GSErrCode FTaskCalledFromEventLoop::DoTasksCallBack(GSHandle ParamHandle, GSPtr /* OutResultData */,
												   bool /* bSilentMode */)
{
	GSErrCode GSErr = TryFunctionCatchAndLog("DoTasks", [&ParamHandle]() -> GSErrCode { return DoTasks(ParamHandle); });
	DeleteParamHandle(ParamHandle);
	return GSErr;
}

// Schedule InTask to be executed on next event.
void FTaskCalledFromEventLoop::CallTaskFromEventLoop(const TSharedRef< FTaskCalledFromEventLoop >& InTask,
												   ERetainType									InRetainType)
{
	++SPendingTaskCount;
	API_ModulID mdid;
	Zap(&mdid);
	mdid.developerID = kEpicGamesDevId;
	mdid.localID = kDatasmithExporterId;
	GSHandle		 ParamHandle = BMAllocateHandle(sizeof(FTaskParameters), 0, 0);
	FTaskParameters& TaskParameters = reinterpret_cast< FTaskParameters& >(**ParamHandle);
	TaskParameters.RetainType = InRetainType;
	if (InRetainType == kSharedRef)
	{
		TaskParameters.u.SharedRef = new TSharedRef< FTaskCalledFromEventLoop >(InTask);
	}
	else
	{
		TaskParameters.u.WeakPtr = new TWeakPtr< FTaskCalledFromEventLoop >(InTask);
	}
	GSErrCode err = ACAPI_Command_CallFromEventLoop(&mdid, UEDirectLinkTask, CmdDoTask, ParamHandle, false, nullptr);
	if (err != NoError)
	{
		UE_AC_DebugF("FTaskCalledFromEventLoop::CallFromEventLoop - ACAPI_Command_CallFromEventLoop error %d\n", err);

		// Clean up
		DeleteParamHandle(ParamHandle);
	}
}

// Register the task service
GSErrCode FTaskCalledFromEventLoop::Register()
{
	GSErrCode GSErr = ACAPI_Register_SupportedService(UEDirectLinkTask, CmdDoTask);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FTaskCalledFromEventLoop::Register - Error %d\n", GSErr);
	}
	return GSErr;
}

// Initialize
GSErrCode FTaskCalledFromEventLoop::Initialize()
{
	GSErrCode GSErr = ACAPI_Install_ModulCommandHandler(UEDirectLinkTask, CmdDoTask, DoTasksCallBack);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FTaskCalledFromEventLoop::Initialize - Error %d\n", GSErr);
	}
	return GSErr;
}

// Uninitialize the task service
void FTaskCalledFromEventLoop::Uninitialize()
{
	int PendingTaskCount = SPendingTaskCount;
	if (PendingTaskCount != 0)
	{
		UE_AC_DebugF("FTaskCalledFromEventLoop::Uninitialize - Pending tasks %d\n", PendingTaskCount);
	}
}

void FTaskCalledFromEventLoop::DeleteParamHandle(GSHandle ParamHandle)
{
	if (ParamHandle != nullptr)
	{
		--SPendingTaskCount;
		FTaskParameters& TaskParameters = reinterpret_cast< FTaskParameters& >(**ParamHandle);
		if (TaskParameters.RetainType == FTaskCalledFromEventLoop::kSharedRef)
		{
			delete TaskParameters.u.SharedRef;
		}
		else
		{
			delete TaskParameters.u.WeakPtr;
		}
		GSErrCode GSErr = ACAPI_Goodies(APIAny_FreeMDCLParameterListID, ParamHandle, nullptr);
		if (GSErr != NoError)
		{
			UE_AC_DebugF(
				"FTaskCalledFromEventLoop::DeleteParamHandle - APIAny_FreeMDCLParameterListID return error %s\n",
				GetErrorName(GSErr));
		}
	}
}

END_NAMESPACE_UE_AC
