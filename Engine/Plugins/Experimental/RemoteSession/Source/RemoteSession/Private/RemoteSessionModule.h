// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSession.h"
#include "RemoteSessionRole.h"

class FRemoteSessionHost;
class FRemoteSessionClient;
class IRemoteSessionChannelFactoryWorker;

class FRemoteSessionModule : public IRemoteSessionModule, public FTickableGameObject
{
public:

	struct FChannelRedirects
	{
		FString OldName;		// Old channel name
		FString NewName;		// New channel name
		int32 VersionNumber;	// 
		FChannelRedirects() = default;
		FChannelRedirects(FString InOldName, FString InNewName) : OldName(InOldName), NewName(InNewName) {}
	};

private:

	TSharedPtr<FRemoteSessionHost>							Host;
	TSharedPtr<FRemoteSessionClient>						Client;

	TArray<TWeakPtr<IRemoteSessionChannelFactoryWorker>>	FactoryWorkers;
	TArray<TSharedPtr<IRemoteSessionChannelFactoryWorker>>	BuiltInFactory;

	int32													DefaultPort;

	bool													bAutoHostWithPIE;
	bool													bAutoHostWithGame;

	TMap<FString, ERemoteSessionChannelMode>				IniSupportedChannels;
	TArray<FRemoteSessionChannelInfo>						ProgramaticallySupportedChannels;
	TArray<FChannelRedirects>								ChannelRedirects;

	FDelegateHandle PostPieDelegate;
	FDelegateHandle EndPieDelegate;
	FDelegateHandle GameStartDelegate;

public:

	//~ Begin IRemoteSessionModule interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual void AddChannelFactory(TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker) override;
	virtual void RemoveChannelFactory(TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker) override;
	virtual TSharedPtr<IRemoteSessionChannelFactoryWorker> FindChannelFactoryWorker(const TCHAR* Type) override;
	virtual void SetSupportedChannels(TMap<FString, ERemoteSessionChannelMode>& InSupportedChannels) override;
	virtual void AddSupportedChannel(FString InType, ERemoteSessionChannelMode InMode) override;
	virtual void AddSupportedChannel(FString InType, ERemoteSessionChannelMode InMode, FOnRemoteSessionChannelCreated InOnCreated) override;

	virtual TSharedPtr<IRemoteSessionRole>	CreateClient(const TCHAR* RemoteAddress) override;
	virtual void StopClient(TSharedPtr<IRemoteSessionRole> InClient) override;

	virtual void InitHost(const int16 Port = 0) override;
	virtual bool IsHostRunning() const override { return Host.IsValid(); }
	virtual bool IsHostConnected() const override;
	virtual void StopHost() override { Host = nullptr; }
	virtual TSharedPtr<IRemoteSessionRole> GetHost() const override;
	virtual TSharedPtr<IRemoteSessionUnmanagedRole> CreateHost(TArray<FRemoteSessionChannelInfo> SupportedChannels, int32 Port) const override;
	//~ End IRemoteSessionModule interface

	//~ Begin FTickableGameObject interface
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FRemoteSession, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	//~ End FTickableGameObject interface

	const TArray<FChannelRedirects>& GetChannelRedirects() const { return ChannelRedirects; }
	void SetAutoStartWithPIE(bool bEnable);

private:
	void ReadIniSettings();

	TSharedPtr<FRemoteSessionHost> CreateHostInternal(TArray<FRemoteSessionChannelInfo> SupportedChannels, int32 Port) const;

	void OnGameStarted();
	void OnPIEStarted(bool bSimulating);
	void OnPIEEnded(bool bSimulating);
};
