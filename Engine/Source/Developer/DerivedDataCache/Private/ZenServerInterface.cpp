// Copyright Epic Games, Inc. All Rights Reserved.
#include "ZenServerInterface.h"

#if UE_WITH_ZEN

#include "DerivedDataBackendInterface.h"
#include "ZenBackendUtils.h"

#include "Memory/CompositeBuffer.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/MemoryReader.h"

namespace UE::Zen {

static bool bHasLaunched = false;

FZenServiceInstance::FZenServiceInstance()
{
	if (bHasLaunched)
	{
		return;
	}

	FString Parms;
	bool bLaunchDetached = false;
	bool bLaunchHidden = false;
	bool bLaunchReallyHidden = false;
	uint32* OutProcessID = nullptr;
	int32 PriorityModifier = 0;
	const TCHAR* OptionalWorkingDirectory = nullptr;
	void* PipeWriteChild = nullptr;
	void* PipeReadChild = nullptr;

	FString MainFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("zenserver.exe")));

	Parms.Appendf(TEXT("--owner-pid %d"), FPlatformProcess::GetCurrentProcessId());

	FProcHandle Proc = FPlatformProcess::CreateProc(
		*MainFilePath,
		*Parms,
		bLaunchDetached,
		bLaunchHidden,
		bLaunchReallyHidden,
		OutProcessID,
		PriorityModifier,
		OptionalWorkingDirectory,
		PipeWriteChild,
		PipeReadChild);

	if (!Proc.IsValid())
	{
		Proc = FPlatformProcess::CreateElevatedProcess(*MainFilePath, *Parms);
	}

	bHasLaunched = true;
}

FZenServiceInstance::~FZenServiceInstance()
{
}

bool 
FZenServiceInstance::IsServiceRunning()
{
	return false;
}

bool 
FZenServiceInstance::IsServiceReady()
{
	return false;
}

}
#endif // UE_WITH_ZEN
