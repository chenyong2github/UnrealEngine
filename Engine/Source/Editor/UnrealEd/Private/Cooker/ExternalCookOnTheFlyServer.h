// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CookOnTheFly.h"
#include "TickableEditorObject.h"
#include "IMessageContext.h"
#include "MessageEndpoint.h"
#include "ExternalCookOnTheFlyServer.generated.h"

class IAssetRegistry;

class FExternalCookOnTheFlyServer
	: public FTickableEditorObject
{
public:
	FExternalCookOnTheFlyServer();
	~FExternalCookOnTheFlyServer();
	void HandleRegisterServiceMessage(const FZenCookOnTheFlyRegisterServiceMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	static FString GenerateServiceId();

private:
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override { return TStatId(); }

	void AssetUpdatedOnDisk(const FAssetData& AssetData);

	UE::Cook::ICookOnTheFlyModule& CookOnTheFlyModule;
	IAssetRegistry& AssetRegistry;
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;
	const FString ServiceId;
	TUniquePtr<UE::Cook::ICookOnTheFlyServerConnection> CookOnTheFlyServerConnection;
	FCriticalSection AllPackagesToRecookCritical;
	TSet<FName> AllPackagesToRecook;
};

USTRUCT()
struct FZenCookOnTheFlyRegisterServiceMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Message")
	FString ServiceId;

	UPROPERTY(EditAnywhere, Category = "Message")
	int32 Port = -1;
};