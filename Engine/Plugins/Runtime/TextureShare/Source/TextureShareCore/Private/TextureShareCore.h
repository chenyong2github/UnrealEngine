// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareCore.h"

class ITextureShareItem;

class FTextureShareCoreModule 
	: public ITextureShareCore
{
public:
	FTextureShareCoreModule();
	virtual ~FTextureShareCoreModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	/** global share lib api */
	virtual FTextureShareSyncPolicySettings GetSyncPolicySettings(ETextureShareProcess Process) const override;
	virtual void SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings) override;
	virtual void ReleaseLib() override;

	/** shared resource object api */
	virtual bool CreateTextureShareItem(const FString& ShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, TSharedPtr<ITextureShareItem>& OutShareObject, float SyncWaitTime) override;
	virtual bool ReleaseTextureShareItem(const FString& ShareName) override;
	virtual bool GetTextureShareItem(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareObject) const override;

	virtual bool BeginSyncFrame() override;
	virtual bool EndSyncFrame() override;

protected:
	void InitializeProcessMemory();
	void ReleaseProcessMemory();

	bool ImplCheckTextureShareItem(const FString& ShareName) const;

private:
	mutable FCriticalSection DataGuard;
	TMap<FString, TSharedPtr<ITextureShareItem>> TextureShares;

private:
	bool bIsValidSharedResource = false;
};
