// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Containers/Map.h"

struct CORE_API FAndroidCrashContext : public FGenericCrashContext
{
	/** Signal number */
	int32 Signal;
	
	/** Additional signal info */
	siginfo* Info;
	
	/** Thread context */
	void* Context;

	FAndroidCrashContext(ECrashContextType InType, const TCHAR* InErrorMessage);

	~FAndroidCrashContext()
	{
	}

	/**
	 * Inits the crash context from data provided by a signal handler.
	 *
	 * @param InSignal number (SIGSEGV, etc)
	 * @param InInfo additional info (e.g. address we tried to read, etc)
	 * @param InContext thread context
	 */
	void InitFromSignal(int32 InSignal, siginfo* InInfo, void* InContext)
	{
		Signal = InSignal;
		Info = InInfo;
		Context = InContext;
	}

	virtual void GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack) override;
	virtual void AddPlatformSpecificProperties() const override;

	void CaptureCrashInfo();
	void StoreCrashInfo() const;

	static const int32 CrashReportMaxPathSize = 512;

	static void Initialize();
	static const FString GetCrashDirectoryName();
	static void GetCrashDirectoryName(char(&DirectoryNameOUT)[CrashReportMaxPathSize]);

	void AddAndroidCrashProperty(const FString& Key, const FString& Value);

	// generate an absolute path to a crash report folder.
	static void GenerateReportDirectoryName(char(&DirectoryNameOUT)[CrashReportMaxPathSize]);

private:
	TMap<FString, FString> AdditionalProperties;

};

typedef FAndroidCrashContext FPlatformCrashContext;