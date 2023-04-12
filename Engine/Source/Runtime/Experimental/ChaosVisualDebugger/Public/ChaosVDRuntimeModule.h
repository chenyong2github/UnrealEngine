// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

struct FChaosVDRecording;

class CHAOSVDRUNTIME_API FChaosVDRuntimeModule : public IModuleInterface
{
public:

	static FChaosVDRuntimeModule& Get();
	
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Starts a CVD recording by starting a Trace session. It will stop any existing trace session
	 * @param Args : Arguments array provided by the commandline. Used to determine if we want to record to file or a local trace server
	 */
	void StartRecording(const TArray<FString>& Args);
	/**/
	void StopRecording();

	/** Returns true if we are currently recording a Physics simulation */
	bool IsRecording() const { return bIsRecording; }

	/** Returns a unique ID used to be used to identify CVD (Chaos Visual Debugger) data */
	int32 GenerateUniqueID();

private:

	/** Stops the current Trace session */
	void StopTrace();
	/** Finds a valid file name for a new file - Used to generate the file name for the Trace recording */
	void GenerateRecordingFileName(FString& OutFileName);

	/** Used to handle stop requests to the active trace session that were not done by us
	 * That is a possible scenario because Trace is shared by other In-Editor tools
	 */
	void HandleTraceStopRequest(FTraceAuxiliary::EConnectionType TraceType, const FString& TraceDestination);

	bool bIsRecording = false;
	bool bRequestedStop = false;

	FThreadSafeCounter LastGeneratedID;
};
