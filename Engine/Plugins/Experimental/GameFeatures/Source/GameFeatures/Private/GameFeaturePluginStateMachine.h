// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"
#include "Containers/Union.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeatureTypes.h"
#include "GameFeaturePluginStateMachine.generated.h"

class UGameFeatureData;
class UGameFrameworkComponentManager;
class UGameFeaturePluginStateMachine;
struct FComponentRequestHandle;

/*
*************** GameFeaturePlugin state machine graph ***************
Descriptions for each state are below in EGameFeaturePluginState.
Destination states have a *. These are the only states that external sources can ask to transition to via SetDestinationState().
Error states have !. These states become destinations if an error occurs during a transition.
Transition states are expected to transition the machine to another state after doing some work.

                         +--------------+
                         |              |
                         |Uninitialized |
                         |              |
                         +------+-------+
     +------------+             |
     |     *      |             |
     |  Terminal  <-------------~------------------------------
     |            |             |                             |
     +--^------^--+             |                             |
        |      |                |                             |
        |      |         +------v-------+                     |
        |      |         |      *       |                     |
        |      ----------+UnknownStatus |                     |
        |                |              |                     |
        |                +------+-------+                     |
        |                  |                                  |
        |      +-----------v---+     +--------------------+   |
        |      |               |     |         !          |   |
        |      |CheckingStatus <-----> ErrorCheckingStatus+-->|
        |      |               |     |                    |   |
        |      +------+------^-+     +--------------------+   |
        |             |      |                                |
        |             |      |       +--------------------+   |
        ----------    |      |       |         !          |   |
                 |    |      --------> ErrorUnavailable   +----
                 |    |              |                    |
                 |    |              +--------------------+
                 |    |
               +-+----v-------+                            
               |      *       |
     ----------+ StatusKnown  |
     |         |              |
     |         +-^-----+---+--+
     |           |         |
     |     ------~---------~-------------------------
     |     |     |         |                        |
     |  +--v-----+---+   +-v----------+       +-----v--------------+
     |  |            |   |            |       |         !          |
     |  |Uninstalling|   |Downloading <-------> ErrorInstalling    |
     |  |            |   |            |       |                    |
     |  +--------^---+   +-+----------+       +--------------------+
     |           |         |
     |         +-+---------v-+
     |         |      *      |
     ---------->  Installed  |
               |             |
               +-^---------+-+
                 |         |
		   ------~---------~--------------------------------
           |     |         |                               |
        +--v-----+--+    +-v---------+               +-----v--------------+
        |           |    |           |				 |         !          |
        |Unmounting |    | Mounting  <---------------> ErrorMounting      |
        |           |    |           |				 |                    |
        +--^-----^--+    +--+--------+				 +--------------------+
           |     |          |
           ------~----------~-------------------------------
                 |          |                              |
                 |       +--v------------------- +   +-----v-----------------------+
                 |       |                       |	 |         !                   |
                 |       |WaitingForDependencies <---> ErrorWaitingForDependencies |
                 |       |                       |	 |                             |
                 |       +--+------------------- +	 +-----------------------------+
                 |          |
           ------~----------~-------------------------------
           |     |          |                              |
        +--v-----+----+  +--v-------- +              +-----v--------------+
        |             |  |            |				 |         !          |
        |Unregistering|  |Registering <--------------> ErrorRegistering   |
        |             |  |            |				 |                    |
        +--------^----+  ++---------- +				 +--------------------+
                 |        |
               +-+--------v-+
               |      *     |
               | Registered |
               |            |
               +-^--------+-+
                 |        |
        +--------+--+  +--v--------+
        |           |  |           |
        | Unloading |  |  Loading  |
        |           |  |           |
        +--------^--+  +--+--------+
                 |        |
               +-+--------v-+
               |      *     |
               |   Loaded   |
               |            |
               +-^--------+-+
                 |        |
        +--------+---+  +-v---------+
        |            |  |           |
        |Deactivating|  |Activating |
        |            |  |           |
        +--------^---+  +-+---------+
                 |        |
               +-+--------v-+
               |      *     |
               |   Active   |
               |            |
               +------------+
*/

// JMarcus TODO: move this stuff into UE::GameFeatures namespace

#define GAME_FEATURE_PLUGIN_PROTOCOL_LIST(XPROTO)	\
	XPROTO(File,			TEXT("file:"))			\
	XPROTO(InstallBundle,	TEXT("installbundle:"))	\
	XPROTO(Unknown,			TEXT(""))				

#define GAME_FEATURE_PLUGIN_PROTOCOL_ENUM(inEnum, inString) inEnum,
enum class EGameFeaturePluginProtocol : uint8
{
	GAME_FEATURE_PLUGIN_PROTOCOL_LIST(GAME_FEATURE_PLUGIN_PROTOCOL_ENUM)
	Count,
};
#undef GAME_FEATURE_PLUGIN_PROTOCOL_ENUM

ENUM_RANGE_BY_COUNT(EGameFeaturePluginProtocol, EGameFeaturePluginProtocol::Count);

const TCHAR* GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol Protocol);

struct FInstallBundlePluginProtocolMetaData
{
	TArray<FName> InstallBundles;
};

/** Notification that a state transition is complete */
DECLARE_DELEGATE_TwoParams(FGameFeatureStateTransitionComplete, UGameFeaturePluginStateMachine* /*Machine*/, const UE::GameFeatures::FResult& /*Result*/);

/** A request for other state machine dependencies */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FGameFeaturePluginRequestStateMachineDependencies, const FString& /*DependencyPluginURL*/, TArray<UGameFeaturePluginStateMachine*>& /*OutDependencyMachines*/);

/** A request to update the state machine and process states */
DECLARE_DELEGATE(FGameFeaturePluginRequestUpdateStateMachine);

/** A request to update progress for the current state */
DECLARE_DELEGATE_OneParam(FGameFeatureStateProgressUpdate, float Progress);

/** The common properties that can be accessed by the states of the state machine */
USTRUCT()
struct FGameFeaturePluginStateMachineProperties
{
	GENERATED_BODY()

	/**
	 * The URL to find the plugin. This takes the form protocol:identifier.
	 * Every protocol will have its own style of identifier.
	 * For example, if the file is simply on disk, you can use file:../../../YourGameModule/Plugins/MyPlugin/MyPlugin.uplugin
	 */
	FString PluginURL;

	/** Filename on disk of the .uplugin file. */
	FString PluginInstalledFilename;
	
	/** Name of the plugin. */
	FString PluginName;

	/** Meta data parsed from the URL for a specific protocol. */
	TUnion<FInstallBundlePluginProtocolMetaData> ProtocolMetadata;

	/** Tracks whether or not this state machine added the pluging to the plugin manager. */
	bool bAddedPluginToManager = false;

	/** The desired state during a transition. */
	EGameFeaturePluginState DestinationState = EGameFeaturePluginState::Uninitialized;

	/** The data asset describing this game feature */
	UPROPERTY(Transient)
	UGameFeatureData* GameFeatureData = nullptr;

	/** Delegate for when a state transition request has completed. */
	FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete;

	/** Delegate for when this machine needs to request its dependency machines. */
	FGameFeaturePluginRequestStateMachineDependencies OnRequestStateMachineDependencies;

	/** Delegate to request the state machine be updated. */
	FGameFeaturePluginRequestUpdateStateMachine OnRequestUpdateStateMachine;

	/** Delegate for when a feature state needs to update progress. */
	FGameFeatureStateProgressUpdate OnFeatureStateProgressUpdate;

	FGameFeaturePluginStateMachineProperties() = default;
	FGameFeaturePluginStateMachineProperties(
		const FString& InPluginURL,
		EGameFeaturePluginState DesiredDestination,
		const FGameFeaturePluginRequestStateMachineDependencies& RequestStateMachineDependenciesDelegate,
		const FGameFeaturePluginRequestUpdateStateMachine& RequestUpdateStateMachineDelegate,
		const FGameFeatureStateProgressUpdate& FeatureStateProgressUpdateDelegate);

	EGameFeaturePluginProtocol GetPluginProtocol() const;

	bool ParseURL();

private:
	mutable EGameFeaturePluginProtocol CachedPluginProtocol = EGameFeaturePluginProtocol::Unknown;
};

/** Input and output information for a state's UpdateState */
struct FGameFeaturePluginStateStatus
{
private:
	/** The state to transition to after UpdateState is complete. */
	EGameFeaturePluginState TransitionToState = EGameFeaturePluginState::Uninitialized;

	/** Holds the current error for any state transition. */
	UE::GameFeatures::FResult TransitionResult{ MakeValue() };

	friend class UGameFeaturePluginStateMachine;

public:
	void SetTransition(EGameFeaturePluginState InTransitionToState)
	{
		TransitionToState = InTransitionToState;
		TransitionResult = MakeValue();
	}

	void SetTransitionError(EGameFeaturePluginState TransitionToErrorState, FString Error)
	{
		TransitionToState = TransitionToErrorState;
		TransitionResult = MakeError(MoveTemp(Error));
	}
};

enum class EGameFeaturePluginStateType : uint8
{
	Transition,
	Destination,
	Error
};

/** Base class for all game feature plugin states */
struct FGameFeaturePluginState
{
	FGameFeaturePluginState(FGameFeaturePluginStateMachineProperties& InStateProperties) : StateProperties(InStateProperties) {}
	virtual ~FGameFeaturePluginState();

	/** Called when this state becomes the active state */
	virtual void BeginState() {}

	/** Process the state's logic to decide if there should be a state transition. */
	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) {}

	/** Called when this state is no longer the active state */
	virtual void EndState() {}

	/** Returns the type of state this is */
	virtual EGameFeaturePluginStateType GetStateType() const { return EGameFeaturePluginStateType::Transition; }

	/** The common properties that can be accessed by the states of the state machine */
	FGameFeaturePluginStateMachineProperties& StateProperties;

	void UpdateStateMachineDeferred(float Delay = 0.0f) const;
	void GarbageCollectAndUpdateStateMachineDeferred() const;
	void UpdateStateMachineImmediate() const;

	void UpdateProgress(float Progress) const;

private:
	void CleanupDeferredUpdateCallbacks() const;

	mutable FTSTicker::FDelegateHandle TickHandle;
};

/** Information about a given plugin state, used to expose information to external code */
struct FGameFeaturePluginStateInfo
{
	/** The state this info represents */
	EGameFeaturePluginState State = EGameFeaturePluginState::Uninitialized;

	/** The progress of this state. Relevant only for transition states. */
	float Progress = 0.0f;

	FGameFeaturePluginStateInfo() = default;
	explicit FGameFeaturePluginStateInfo(EGameFeaturePluginState InState) : State(InState) {}
};

/** A state machine to manage transitioning a game feature plugin from just a URL into a fully loaded and active plugin, including registering its contents with other game systems */
UCLASS()
class UGameFeaturePluginStateMachine : public UObject
{
	GENERATED_BODY()

public:
	UGameFeaturePluginStateMachine(const FObjectInitializer& ObjectInitializer);

	/** Initializes the state machine and assigns the URL for the plugin it manages. This sets the machine to the 'UnknownStatus' state. */
	void InitStateMachine(const FString& InPluginURL, const FGameFeaturePluginRequestStateMachineDependencies& OnRequestStateMachineDependencies);

	/** Asynchronously transitions the state machine to the destination state and reports when it is done. DestinationState must not be a transition state like 'Downloading' */
	void SetDestinationState(EGameFeaturePluginState InDestinationState, FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete);

	/** Returns the name of the game feature. Before StatusKnown, this returns the URL. */
	FString GetGameFeatureName() const;

	/** Returns the URL */
	FString GetPluginURL() const;

	/** Returns the plugin name if known (plugin must have been registered to know the name). */
	FString GetPluginName() const;

	/** Returns the uplugin filename of the game feature. Before StatusKnown, this returns false. */
	bool GetPluginFilename(FString& OutPluginFilename) const;

	/** Returns the enum state for this machine */
	EGameFeaturePluginState GetCurrentState() const;

	/** Returns the state this machine is trying to move to */
	EGameFeaturePluginState GetDestinationState() const;

	/** Returns information about the current state */
	const FGameFeaturePluginStateInfo& GetCurrentStateInfo() const;

	/** Returns true if the state is at least StatusKnown so we can query info about the game feature plugin */
	bool IsStatusKnown() const;

	/** Returns true if the plugin is available to download/load. Only call if IsStatusKnown is true */
	bool IsAvailable() const;

	/** If the plugin is activated already, we will retrieve its game feature data */
	UGameFeatureData* GetGameFeatureDataForActivePlugin();

	/** If the plugin is registered already, we will retrieve its game feature data */
	UGameFeatureData* GetGameFeatureDataForRegisteredPlugin();

	/** Delegate for the machine's state changed. */
	DECLARE_EVENT_OneParam(UGameFeaturePluginStateMachine, FGameFeaturePluginStateChanged, UGameFeaturePluginStateMachine* /*Machine*/);
	FGameFeaturePluginStateChanged& OnStateChanged() { return OnStateChangedEvent; }

private:
	/** Returns true if the specified state is not a transition state */
	bool IsValidTransitionState(EGameFeaturePluginState InState) const;

	/** Returns true if the specified state is a destination state */
	bool IsValidDestinationState(EGameFeaturePluginState InDestinationState) const;

	/** Returns true if the specified state is a error state */
	bool IsValidErrorState(EGameFeaturePluginState InDestinationState) const;

	/** Processes the current state and looks for state transitions */
	void UpdateStateMachine();

	/** Update Progress for current state */
	void UpdateCurrentStateProgress(float Progress);

	/** Delegate for the machine's state changed. */
	FGameFeaturePluginStateChanged OnStateChangedEvent;

	/** Information about the current state */
	FGameFeaturePluginStateInfo CurrentStateInfo;

	/** The common properties that can be accessed by the states of the state machine */
	UPROPERTY(transient)
	FGameFeaturePluginStateMachineProperties StateProperties;

	/** All state machine state objects */
	TUniquePtr<FGameFeaturePluginState> AllStates[EGameFeaturePluginState::MAX];

	/** True when we are currently executing UpdateStateMachine, to avoid reentry */
	bool bInUpdateStateMachine;
};
