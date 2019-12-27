// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OVR_Avatar.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "OvrAvatarManager.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAvatars, Log, All);

class UTexture2D;
class UTexture;

UCLASS()
class OCULUSAVATAR_API UOvrAvatarManager : public UObject, public FTickableGameObject
{
public:
	GENERATED_BODY()

	static UOvrAvatarManager& Get();
	static void Destroy();

	// from FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override { return TStatId(); }

	void InitializeSDK();
	void ShutdownSDK();

	void LoadTexture(const uint64_t id, const ovrAvatarTextureAssetData* data);
	UTexture* FindTexture(uint64_t id);
	void CacheNormalMapID(uint64_t id);
	void CacheRoughnessMapID(uint64_t id);

	//These both call from the main game thread so should be thread safe.
	ovrAvatarPacket* RequestAvatarPacket(const FString& key);
	void QueueAvatarPacket(ovrAvatarPacket* packet);

	//declaring this version of the function as a Server side call.  So when a client calls this, it executes on the server only.  
	//UFUNCTION(Server, Reliable)
	//void QueueAvatarPacketServer(uint8_t* inBuffer, uint32_t inBufferSize, const FString& key, uint32 packetSequenceNumber);

	void RegisterRemoteAvatar(const FString& key);
	void UnregisterRemoteAvatar(const FString& key);

	float GetSDKPacketDuration(ovrAvatarPacket* packet);
	void FreeSDKPacket(ovrAvatarPacket* packet);

	bool IsOVRPluginValid() const;

	void SetSDKLoggingLevel(ovrAvatarLogLevel level) { ovrAvatar_SetLoggingLevel(level); }

	/** Event used to avoid accessing the avatar library after shutdown */
	DECLARE_EVENT(UOvrAvatarManager, FAvatarShutdownEvent)
	FAvatarShutdownEvent& OnShutdown() { return ShutdownEvent; }

private:
	static void SDKLogger(const char * str);

	UOvrAvatarManager() {};
	~UOvrAvatarManager();

	void HandleAvatarSpecification(const ovrAvatarMessage_AvatarSpecification* message);
	void HandleAssetLoaded(const ovrAvatarMessage_AssetLoaded* message);

	UTexture2D* LoadTexture(const uint64_t id, const ovrAvatarTextureAssetData* data, bool isLinearColor);

	bool bIsInitialized = false;

	UPROPERTY()
	TMap<uint64, UTexture*> Textures;

	TSet<uint64> NormalMapIDs;
	TSet<uint64> RoughnessMapIDs;
	
	static UOvrAvatarManager* sAvatarManager;
	FString AVATAR_APP_ID;

	struct SerializedPacketBuffer
	{
		uint32_t Size;
		uint8_t* Buffer;
	};

	struct AvatarPacketQueue
	{
		TQueue<SerializedPacketBuffer> PacketQueue;
		uint32_t PacketQueueSize = 0;
	};

	TMap<FString, AvatarPacketQueue*> AvatarPacketQueues;

	void* OVRPluginHandle = nullptr;
	void* OVRAvatarHandle = nullptr;
	FAvatarShutdownEvent ShutdownEvent;

	ovrAvatarLogLevel LogLevel = ovrAvatarLogLevel::ovrAvatarLogLevel_Silent;

	static FSoftObjectPath AssetList[];
	static TArray<UObject*> AssetObjects;
};
