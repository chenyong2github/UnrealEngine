// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/GameInstance.h"
#include "LiveStreamAnimationFwd.h"
#include "ForwardingChannelsFwd.h"
#include "ForwardingChannelFactory.h"
#include "LiveStreamAnimationHandle.h"
#include "Containers/ArrayView.h"
#include "LiveLink/LiveStreamAnimationLiveLinkSourceOptions.h"
#include "LiveStreamAnimationSubsystem.generated.h"

UENUM(BlueprintType)
enum class ELiveStreamAnimationRole : uint8
{
	Proxy,		//! Subsystem neither creates nor consumes animation data,
				//! but is acting as a Proxy to pass through.

	Processor,	//! Subsystem is consuming animation packets and evaluating
				//! them locally. It also acts as a Proxy.

	Tracker		//! Subsystem is evaluating animation locally and generating
				//! animation packets that can be sent to other connections.
				//! This node will ignore any received packets.
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLiveStreamAnimationRoleChanged, const ELiveStreamAnimationRole);

namespace LiveStreamAnimation
{
	class FSkelMeshToLiveLinkSource;
}

/**
 * Subsystem used to help with replicating Animation Data (typically performance capture data)
 * through a network to multiple connections, at multiple layers.
 *
 * Other means are used to manage the connections themselves and this plugin is used to help
 * facilitate different animation formats and compression techniques.
 *
 **************************************************************************************
 *********************************** Typical Setup ************************************
 **************************************************************************************
 *
 * This subsystem would be added as a subsystem with your Game Instance.
 *
 * Every Net Connection that is participating in the replication of data
 * will need to open a ULiveStreamAnimation channel.
 *
 * Using the ForwardingChannels plugin, these channels will automatically register themselves
 * with the appropriate Forwarding Group so we can send and receive animation data.
 *
 **************************************************************************************
 *************************************** Role *****************************************
 **************************************************************************************
 *
 * Typically, any node in the network will be either be Tracking Data, Processing Data,
 * or Proxying.
 *
 * Nodes that are Tracking Data are actually evaluating animation data, serializing frames
 * into packets, and sending those packets off so others can evaluate them.
 *
 * Game code should tell the Live Stream Animation Subsystem what
 * type of animation data it wants to track and how. At that point, the Subsystem
 * will listen for new animation data and generate the appropriate packets.
 * These packets are then sent up to a connected server node.
 *
 * NOTE: These packets *could* also be sent to attached clients, but its assumed
 *			that a tracker is itself acting as a client with no connections.
 *
 * Nodes that are Processing Data will receiving animation data and evaluate them.
 * Depending on the animation data type, Game code may not need to tell the
 * Subsystem exactly what type of data its expecting.
 *
 * For example, Live Link data will automatically be pushed into the correct Live Link Subject
 * and Game code can just register for Live Link updates directly.
 *
 * Nodes that are acting as Proxies are simply receving animation packets, doing minimal
 * validation on them, and passing them along to connected clients. Proxies currently
 * do not send data upstream to servers.
 *
 **************************************************************************************
 ********************************* Join In Progress ***********************************
 **************************************************************************************
 *
 * Both Proxies and Trackers will maintain some amount of Registration state for
 * animation data so when new connections are established they can be properly initialized
 * to start receiving new data from the server immediately.
 *
 * While Trackers may have some cached animation frames, neither Proxies nor Trackers
 * will attempt to send that data to newly established connections.
 *
 * So, in Unreal parlance, Animation Frames can be thought of as Unreliable Multicasts,
 * where Registration Data is more akin to Reliable Property Replication.
 *
 **************************************************************************************
 ***************************** Stream Animation Handles *******************************
 **************************************************************************************
 *
 * Live Stream Animation handles are a very simple way to efficiently replicate
 * references to names in the Live Stream Animation plugin.
 *
 * These work similar to Gameplay Tags or fixed FName replication in that designers or anyone
 * can setup names that can be shared across all builds (see ULiveStreamAnimationSubsystem::HandleNames),
 * and then instead of replicating string data we can simple replicate an index that maps
 * to one of these preconfigured names.
 *
 * The list of available Handle Names is defined in ULiveStreamAnimationSettings, and **must**
 * be the same (order and size) on all instances that are sending or receiving animation data.
 *
 * The main reason why existing engine systems weren't used was just to ensure isolation
 * between this plugin and other game systems.
 * However, there's no reason why this couldn't be changed later.
 *
 */
UCLASS(DisplayName = "Live Stream Animation Subsystem", Transient, Config=Engine, Category = "Live Stream Animation")
class ULiveStreamAnimationSubsystem : public UGameInstanceSubsystem, public IForwardingChannelFactory
{
	GENERATED_BODY()

public:

	LIVESTREAMANIMATION_API ULiveStreamAnimationSubsystem();

	//~ Begin USubsystem Interface
	LIVESTREAMANIMATION_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	LIVESTREAMANIMATION_API virtual void Deinitialize() override;
	//~ End USubsystem Interface

	//~ Begin IForwardingChannelFactory Interface
	virtual void CreateForwardingChannel(class UNetConnection* InNetConnection) override;
	virtual void SetAcceptClientPackets(bool bInShouldAcceptClientPackets) override;
	//~ End IForwardingChannelFactory Interface

	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation")
	LIVESTREAMANIMATION_API void SetRole(const ELiveStreamAnimationRole NewRole);

	UFUNCTION(BlueprintPure, Category = "Live Stream Animation")
	LIVESTREAMANIMATION_API ELiveStreamAnimationRole GetRole() const
	{
		return Role;
	}

	/**
	 * Start tracking a Live Link subject that is active on this machine, serializing its data
	 * to animation packets, and forward those to others connections.
	 * Requires Animation Tracking to be enabled.
	 *
	 * The Registered Name passed in *must* be available / configured in the AllowedRegisteredNames
	 * list, and that list is expected to be the same on all instances.
	 *
	 * @param LiveLinkSubject		The Live Link Subject that we are pulling animation data from locally.
	 *
	 * @param RegisteredName		The registered Live Link Subject name that will be used for clients
	 *								evaluating animation data remotely.
	 *								This name must be present in the HandleNames list.
	 *
	 * @param Options				Options describing the type of data we will track and send.
	 *
	 * @param TranslationProfile	The Translation Profile that we should use for this subject.
	 *								This name must be present in the HandleNames list, otherwise the translation will not
	 *								be applied.
	 *								@see ULiveStreamAnimationLiveLinkFrameTranslator.
	 *
	 * @return Whether or not we successfully registered the subject for tracking.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	LIVESTREAMANIMATION_API bool StartTrackingLiveLinkSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandleWrapper RegisteredName,
		const FLiveStreamAnimationLiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandleWrapper TranslationProfile);

	LIVESTREAMANIMATION_API bool StartTrackingLiveLinkSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandle RegisteredName,
		const FLiveStreamAnimationLiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandle TranslationProfile);

	/**
	 * Stop tracking a Live Link subject.
	 *
	 * @param RegisteredName		The registered remote name for the Live Link Subject.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	LIVESTREAMANIMATION_API void StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandleWrapper RegisteredName);
	LIVESTREAMANIMATION_API void StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandle RegisteredName);

	static FName GetChannelName();

	void ReceivedPacket(const TSharedRef<const LiveStreamAnimation::FLiveStreamAnimationPacket>& Packet, ForwardingChannels::FForwardingChannel& Channel);

	bool SendPacketToServer(const TSharedRef<const LiveStreamAnimation::FLiveStreamAnimationPacket>& Packet);

	static bool IsSubsystemEnabledInConfig()
	{
		return GetDefault<ULiveStreamAnimationSubsystem>()->bEnabled;
	}

	bool IsEnabledAndInitialized() const
	{
		return bEnabled && bInitialized;
	}

	FOnLiveStreamAnimationRoleChanged& GetOnRoleChanged()
	{
		return OnRoleChanged;
	}

	TWeakPtr<const LiveStreamAnimation::FSkelMeshToLiveLinkSource> GetOrCreateSkelMeshToLiveLinkSource();

private:

	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation", DisplayName = "SetAcceptClientPackets", Meta=(AllowPrivateAccess="True"))
	void SetAcceptClientPackets_Private(bool bInShouldAcceptClientPackets)
	{
		bShouldAcceptClientPackets = bInShouldAcceptClientPackets;
	}

	FOnLiveStreamAnimationRoleChanged OnRoleChanged;

	UPROPERTY(Config, Transient)
	bool bEnabled = true;

	template<typename T>
	T* GetSubsystem()
	{
		UGameInstance* GameInstance = GetGameInstance();
		return GameInstance ? GameInstance->GetSubsystem<T>() : nullptr;
	}

	bool bInitialized = false;
	bool bShouldAcceptClientPackets = false;

	TSharedPtr<ForwardingChannels::FForwardingGroup> ForwardingGroup;
	TSharedPtr<LiveStreamAnimation::FLiveLinkStreamingHelper> LiveLinkStreamingHelper;
	
	ELiveStreamAnimationRole Role;
};