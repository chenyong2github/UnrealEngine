// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeaturePluginStateMachine.generated.h"

class UGameFeatureData;
class UGameFrameworkComponentManager;
class UGameFeaturePluginStateMachine;
struct FComponentRequestHandle;

/*
*************** GameFeaturePlugin state machine graph ***************
Descriptions for each state are below in EGameFeaturePluginState.
Destination states have a *. These are the only states that external sources can ask to transition to via SetDestinationState().
Non-destination states are expected to transition the machine to another state after doing some work.

	   +--------------+
	   |              |
	   |Uninitialized |
	   |              |
	   +------+-------+
			  |
	   +------v-------+
	   |      *       |
	   |UnknownStatus |
	   |              |
	   +------+-------+
			  |
	   +------v-------+
	   |              |
	   |CheckingStatus|
	   |              |
	   +------+-------+
			  |
	   +------v-------+
	   |      *       |
	   | StatusKnown  |
	   |              |
	   +-^-----+---+--+
		 |     |   |
+--------+---+ | +-v---------+
|            | | |           |
|Uninstalling| | |Downloading|
|            | | |           |
+--------^---+ | +-+---------+
		 |     |   |
	   +-+-----v---v-+
	   |      *      |
	   |  Installed  |
	   |             |
	   +-^---------+-+
		 |         |
+--------+--+    +-v---------+
|           |    |           |
|Unmounting |    | Mounting  |
|           |    |           |
+--------^--+    +--+--------+
		 |          |
		 |       +--v-------------------+
		 |       |                      |
		 |       |WaitingForDependencies|
		 |       |                      |
		 |       +--+-------------------+
		 |          |
+--------+----+  +--v--------+
|             |  |           |
|Unregistering|  |Registering|
|             |  |           |
+--------^----+  ++----------+
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

/** The states a game feature plugin can be in before fully active */
namespace EGameFeaturePluginState
{
	enum Type
	{
		Uninitialized,				// Unset. Not yet been set up.
		UnknownStatus,				// Initialized, but the only thing known is the URL to query status.
		CheckingStatus,				// Transition state UnknownStatus -> StatusKnown. The status is in the process of being queried.
		StatusKnown,				// The plugin's information is known, but no action has taken place yet.
		Uninstalling,				// Transition state Installed -> StatusKnown. In the process of removing from local storage.
		Downloading,				// Transition state StatusKnown -> Installed. In the process of adding to local storage.
		Installed,					// The plugin is in local storage (i.e. it is on the hard drive)
		WaitingForDependencies,		// Transition state Installed -> Registered. In the process of loading code/content for all dependencies into memory.
		Unmounting,					// Transition state Registered -> Installed. The content file(s) (i.e. pak file) for the plugin is unmounting.
		Mounting,					// Transition state Installed -> Registered. The content files(s) (i.e. pak file) for the plugin is getting mounted.
		Unregistering,				// Transition state Registered -> Installed. Cleaning up data gathered in Registering.
		Registering,				// Transition state Installed -> Registered. Discovering assets in the plugin, but not loading them, except a few for discovery reasons.
		Registered,					// The assets in the plugin are known, but have not yet been loaded, except a few for discovery reasons.
		Unloading,					// Transition state Loaded -> Registered. In the process of removing code/contnet from memory. 
		Loading,					// Transition state Registered -> Loaded. In the process of loading code/content into memory.
		Loaded,						// The plugin is loaded into memory, but not registered with game systems and active.
		Deactivating,				// Transition state Active -> Loaded. Currently unregistering with game systems.
		Activating,					// Transition state Loaded -> Active. Currently registering plugin code/content with game systems.
		Active,						// Plugin is fully loaded and active. It is affecting the game.

		MAX
	};

	FString ToString(Type InType);
}

enum class EGameFeaturePluginProtocol : uint8
{
	File,
	Web,
	Count
};

/** Notification that a state transition is complete */
DECLARE_DELEGATE_TwoParams(FGameFeatureStateTransitionComplete, UGameFeaturePluginStateMachine* /*Machine*/, const UE::GameFeatures::FResult& /*Result*/);

/** A request for other state machine dependencies */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FGameFeaturePluginRequestStateMachineDependencies, const FString& /*DependencyPluginURL*/, TArray<UGameFeaturePluginStateMachine*>& /*OutDependencyMachines*/);

/** A request to update the state machine and process states */
DECLARE_DELEGATE(FGameFeaturePluginRequestUpdateStateMachine);

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

	/** Once installed, this is the filename on disk of the .uplugin file. */
	FString PluginInstalledFilename;
	
	/** Once installed, this is the name of the plugin. */
	FString PluginName;

	/** Delegate for when a state transition request has completed. */
	FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete;

	/** Delegate for when this machine needs to request its dependency machines. */
	FGameFeaturePluginRequestStateMachineDependencies OnRequestStateMachineDependencies;

	/** Delegate to request the state machine be updated. */
	FGameFeaturePluginRequestUpdateStateMachine OnRequestUpdateStateMachine;

	/** The desired state during a transition. */
	EGameFeaturePluginState::Type DestinationState;

	/** Once status is known, this describes whether the plugin is available to download/load */
	bool bIsAvailable;

	/** The data asset describing this game feature */
	UPROPERTY(Transient)
	UGameFeatureData* GameFeatureData;

	FGameFeaturePluginStateMachineProperties() : DestinationState(EGameFeaturePluginState::Uninitialized), bIsAvailable(false), GameFeatureData(nullptr) {}
	FGameFeaturePluginStateMachineProperties(
		const FString& InPluginURL,
		EGameFeaturePluginState::Type DesiredDestination,
		const FGameFeaturePluginRequestStateMachineDependencies& RequestStateMachineDependenciesDelegate,
		const TDelegate<void()>& RequestUpdateStateMachineDelegate);

	EGameFeaturePluginProtocol GetPluginProtocol() const;
	static FString FileProtocolPrefix();
	static FString WebProtocolPrefix();
};

/** Input and output information for a state's UpdateState */
struct FGameFeaturePluginStateStatus
{
	/** The state to transition to after UpdateState is complete. */
	EGameFeaturePluginState::Type TransitionToState;

	/** Holds the current error for any state transition. */
	UE::GameFeatures::FResult TransitionResult;

	FGameFeaturePluginStateStatus() : TransitionToState(EGameFeaturePluginState::Uninitialized), TransitionResult(MakeValue()) {}
};

/** Base class for all game feature plugin states */
struct FGameFeaturePluginState
{
	FGameFeaturePluginState(FGameFeaturePluginStateMachineProperties& InStateProperties) : StateProperties(InStateProperties) {}
	virtual ~FGameFeaturePluginState() {}

	/** Called when this state becomes the active state */
	virtual void BeginState() {}

	/** Process the state's logic to decide if there should be a state transition. */
	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) {}

	/** Called when this state is no longer the active state */
	virtual void EndState() {}

	/** Returns true if this state is allowed to be a destination (i.e. is not a transition state like 'Downloading') */
	virtual bool CanBeDestinationState() const { return false; }

	/** The common properties that can be accessed by the states of the state machine */
	FGameFeaturePluginStateMachineProperties& StateProperties;
};

/** Information about a given plugin state, used to expose information to external code */
struct FGameFeaturePluginStateInfo
{
	/** The state this info represents */
	EGameFeaturePluginState::Type State;

	/** The progress of this state. Relevant only for transition states. */
	float Progress;

	FGameFeaturePluginStateInfo() : State(EGameFeaturePluginState::Uninitialized), Progress(0.f) {}
	explicit FGameFeaturePluginStateInfo(EGameFeaturePluginState::Type InState) : State(InState), Progress(0.f) {}
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
	void SetDestinationState(EGameFeaturePluginState::Type InDestinationState, FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete);

	/** Returns the name of the game feature. Before StatusKnown, this returns the URL. */
	FString GetGameFeatureName() const;

	/** Returns the uplugin filename of the game feature. Before StatusKnown, this returns false. */
	bool GetPluginFilename(FString& OutPluginFilename) const;

	/** Returns the enum state for this machine */
	EGameFeaturePluginState::Type GetCurrentState() const;

	/** Returns the state this machine is trying to move to */
	EGameFeaturePluginState::Type GetDestinationState() const;

	/** Returns information about the current state */
	const FGameFeaturePluginStateInfo& GetCurrentStateInfo() const;

	/** Returns true if the state is at least StatusKnown so we can query info about the game feature plugin */
	bool IsStatusKnown() const;

	/** Returns true if the plugin is available to download/load. Only call if IsStatusKnown is true */
	bool IsAvailable() const;

	/** If the plugin is activated already, we will retrieve its game feature data */
	UGameFeatureData* GetGameFeatureDataForActivePlugin();

	/** Delegate for the machine's state changed. */
	DECLARE_EVENT_OneParam(UGameFeaturePluginStateMachine, FGameFeaturePluginStateChanged, UGameFeaturePluginStateMachine* /*Machine*/);
	FGameFeaturePluginStateChanged& OnStateChanged() { return OnStateChangedEvent; }

private:
	/** Returns true if the specified state is not a transition state like 'Downloading' */
	bool IsValidDestinationState(EGameFeaturePluginState::Type InDestinationState) const;

	/** Processes the current state and looks for state transitions */
	void UpdateStateMachine();

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
