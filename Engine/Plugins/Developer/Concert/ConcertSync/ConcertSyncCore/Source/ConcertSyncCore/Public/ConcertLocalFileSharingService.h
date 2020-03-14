// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertFileSharingService.h"

/**
 * Share files using a local directory. It works only for one client and one server running on the same machine.
 * @note This is mainly designed for the recovery system.
 */
class CONCERTSYNCCORE_API FConcertLocalFileSharingService : public IConcertFileSharingService
{
public:
	/**
	 * Construct the local file sharing service.
	 * @param Role Appended to the shared directory (like DisasterRecovery) as a hint for the temp folder purpose. Client and server must use the same value.
	 * @note The service uses the Engine intermediate directory to share the files. The files are automatically deleted after consumption. In case of crash
	 *       the temporary shared files are deleted on next reboot if no other client/server on the machine are not running.
	 */
	FConcertLocalFileSharingService(const FString& Role);
	virtual ~FConcertLocalFileSharingService();

	virtual bool Publish(const FString& Pathname, FString& OutFileId) override;
	virtual bool Publish(FArchive& SrcAr, int64 Size, FString& OutFileUri) override;
	virtual TSharedPtr<FArchive> CreateReader(const FString& InFileUri) override;

private:
	void LoadActiveServices(TArray<uint32>& OutPids);
	void SaveActiveServices(const TArray<uint32>& InPids);
	void RemoveDeadProcessesAndFiles(TArray<uint32>& InOutPids);

private:
	FString SharedRootPathname;
	FString SystemMutexName;
	FString ActiveServicesRepositoryPathname;
};
