// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AsyncLoading.h: Unreal async loading definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Serialization/AsyncPackageLoader.h"

class FAsyncLoadingThread2Impl;

class FAsyncLoadingThread2
	: public IAsyncPackageLoader
{
public:
	FAsyncLoadingThread2(IEDLBootNotificationManager& InEDLBootNotificationManager);
	virtual ~FAsyncLoadingThread2();

	void InitializeLoading() override;
	void ShutdownLoading() override;
	void StartThread() override;
	bool IsMultithreaded() override;
	bool IsInAsyncLoadThread() override;
	void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject) override;
	void FireCompletedCompiledInImport(FGCObject* AsyncPackage, FPackageIndex Import) override;
	int32 LoadPackage(const FString& InPackageName, const FGuid* InGuid, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority) override;
	EAsyncPackageState::Type ProcessLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit) override;
	EAsyncPackageState::Type ProcessLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, float TimeLimit) override;
	void CancelLoading() override;
	void SuspendLoading() override;
	void ResumeLoading() override;
	void FlushLoading(int32 PackageId) override;
	int32 GetNumAsyncPackages() override;
	float GetAsyncLoadPercentage(const FName& PackageName) override;
	bool IsAsyncLoadingSuspended() override;
	bool IsAsyncLoadingPackages() override;

private:
	FAsyncLoadingThread2Impl* Impl;
};

