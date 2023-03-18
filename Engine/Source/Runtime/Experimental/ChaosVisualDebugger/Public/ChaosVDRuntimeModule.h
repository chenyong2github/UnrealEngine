// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"

struct FChaosVDRecording;

class CHAOSVDRUNTIME_API FChaosVDRuntimeModule : public IModuleInterface
{
public:

	static FChaosVDRuntimeModule& Get();
	
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FChaosVDRecording>& GetRecording() { return CurrentRecording; }

	void StartRecording();
	void StopRecording();

	bool IsRecording() const { return bIsRecording; }

private:

	bool bIsRecording = false;
	TSharedPtr<FChaosVDRecording> CurrentRecording;

};
