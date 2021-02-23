// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
All common code shared between the editor side debugger and debugger clients running in game.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDebuggerCommon.generated.h"

#define WITH_NIAGARA_DEBUGGER !UE_BUILD_SHIPPING

//////////////////////////////////////////////////////////////////////////
// Niagara Outliner.

USTRUCT()
struct FNiagaraOutlinerEmitterInstanceData
{
	GENERATED_BODY()

	//Name of this emitter.
	UPROPERTY(VisibleAnywhere, Category = "Emitter")
	FString EmitterName; //TODO: Move to shared asset representation.

	UPROPERTY(VisibleAnywhere, Category = "Emitter")
	ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim; //TODO: Move to shared asset representation.

	UPROPERTY(VisibleAnywhere, Category = "Emitter")
	ENiagaraExecutionState ExecState = ENiagaraExecutionState::Num;
	
	UPROPERTY(VisibleAnywhere, Category = "Emitter")
	int32 NumParticles = 0;

	//Mem Usage?
	//Scalability info?
};

/** Outliner information on a specific system instance. */
USTRUCT()
struct FNiagaraOutlinerSystemInstanceData
{
	GENERATED_BODY()
	
	/** Name of the component object for this instance, if there is one. */
	UPROPERTY(VisibleAnywhere, Category = "System")
	FString ComponentName;
	
	UPROPERTY(VisibleAnywhere, Category = "System")
	ENiagaraExecutionState ActualExecutionState = ENiagaraExecutionState::Num;

	UPROPERTY(VisibleAnywhere, Category = "System")
	ENiagaraExecutionState RequestedExecutionState = ENiagaraExecutionState::Num;
	
	UPROPERTY(VisibleAnywhere, Category = "System")
	TArray<FNiagaraOutlinerEmitterInstanceData> Emitters;
	
	UPROPERTY(VisibleAnywhere, Category = "System")
	FNiagaraScalabilityState ScalabilityState;
	
	UPROPERTY(VisibleAnywhere, Category = "System")
	uint32 bPendingKill : 1;

	//TODO:
	//Tick info, solo, tick group etc.
	//Scalability Info
	//Perf data?
	//Mem usage?
};

/** Wrapper for array of system instance outliner data so that it can be placed in a map. */
USTRUCT()
struct FNiagaraOutlinerSystemData
{
	GENERATED_BODY()

	//TODO: Cache off any shared representation of the system and emitters here for the instances to reference. 

	/** Map of System Instance data indexed by the UNiagaraSystem name. */
	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	TArray<FNiagaraOutlinerSystemInstanceData> SystemInstances;
};

/** All information about a specific world for the Niagara Outliner. */
USTRUCT()
struct FNiagaraOutlinerWorldData
{
	GENERATED_BODY()

	/** Map of System Instance data indexed by the UNiagaraSystem name. */
	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	TMap<FString, FNiagaraOutlinerSystemData> Systems;

	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	bool bHasBegunPlay = false;
	
	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	uint8 WorldType = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	uint8 NetMode = INDEX_NONE;

	//Perf info?
	//Mem Usage?
};

USTRUCT()
struct FNiagaraOutlinerData
{
	GENERATED_BODY()

	/** Map all world data indexed by the world name. */
	UPROPERTY(VisibleAnywhere, Category = "Outliner")
	TMap<FString, FNiagaraOutlinerWorldData> WorldData;
};

// Outliner END
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Messages passed between the editor side debugger and the client. 

/** 
Messaged broadcast from debugger to request a connection to a particular session. 
If any matching client is found and it accepts, it will return a FNiagaraDebuggerAcceptConnection message to the sender. 
*/
USTRUCT()
struct NIAGARA_API FNiagaraDebuggerRequestConnection
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid InstanceId;

	FNiagaraDebuggerRequestConnection() { }

	FNiagaraDebuggerRequestConnection(const FGuid& InSessionId, const FGuid& InInstanceId)
		: SessionId(InSessionId)
		, InstanceId(InInstanceId)
	{}
};

/** Response message from the a debugger client accepting a connection requested by a FNiagaraDebuggerRequestConnection message. */
USTRUCT()
struct NIAGARA_API FNiagaraDebuggerAcceptConnection
{
	GENERATED_BODY()
		
	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid InstanceId;

	FNiagaraDebuggerAcceptConnection() { }
	
	FNiagaraDebuggerAcceptConnection(const FGuid& InSessionId, const FGuid& InInstanceId)
		: SessionId(InSessionId)
		, InstanceId(InInstanceId)
	{}
};

/** Empty message informing a debugger client that the debugger is closing the connection. */
USTRUCT()
struct NIAGARA_API FNiagaraDebuggerConnectionClosed
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, Category = "Message")
	FGuid InstanceId;

	FNiagaraDebuggerConnectionClosed() { }
	
	FNiagaraDebuggerConnectionClosed(const FGuid& InSessionId, const FGuid& InInstanceId)
		: SessionId(InSessionId)
		, InstanceId(InInstanceId)
	{}
};

/** Command that will execute a console command on the debugger client. */
USTRUCT() 
struct NIAGARA_API FNiagaraDebuggerExecuteConsoleCommand
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Message")
	FString Command;

	UPROPERTY()
	bool bRequiresWorld = false;

	FNiagaraDebuggerExecuteConsoleCommand() { }
	
	FNiagaraDebuggerExecuteConsoleCommand(FString InCommand, bool bInRequiresWorld)
		: Command(InCommand)
		, bRequiresWorld(bInRequiresWorld)
	{}
};

/** Message containing updated outliner information sent from the client to the debugger. */
USTRUCT()
struct NIAGARA_API FNiagaraDebuggerOutlinerUpdate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Message")
	FNiagaraOutlinerData OutlinerData;

	FNiagaraDebuggerOutlinerUpdate() { }
};

// End of messages.
//////////////////////////////////////////////////////////////////////////

UENUM()
enum class ENiagaraDebugPlaybackMode : uint8
{
	Play = 0,
	Loop,
	Paused,
	Step,
};

UENUM()
enum class ENiagaraDebugHudSystemVerbosity
{
	/** Display no text with the system. */
	None = 0,
	/** Display minimal information with the system, i.e. component / system name. */
	Minimal = 1,
	/** Display basic information with the system, i.e. Minimal + counts. */
	Basic = 2,
	/** Display basic information with the system, i.e. Basic + per emitter information. */
	Verbose = 3,
};

UENUM()
enum class ENiagaraDebugHudFont
{
	Small = 0,
	Normal,
};

USTRUCT()
struct NIAGARA_API FNiagaraDebugHUDVariable
{
	GENERATED_BODY()
		
	UPROPERTY(EditAnywhere, Category = "Attribute")
	bool bEnabled = true;

	/** Name of attributes to match, uses wildcard matching. */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	FString Name;

	static FString BuildVariableString(const TArray<FNiagaraDebugHUDVariable>& Variables);
	static void InitFromString(const FString& VariablesString, TArray<FNiagaraDebugHUDVariable>& OutVariables);
};

/** Settings for Niagara debug HUD. Contained in it's own struct so that we can pass it whole in a message to the debugger client. */
USTRUCT()
struct NIAGARA_API FNiagaraDebugHUDSettingsData
{
	GENERATED_BODY()

	FNiagaraDebugHUDSettingsData();

	/**
	Changes the verbosity of the HUD display.
	The default will only disable information if you enable a feature that impacts playback.
	*/
	UPROPERTY(EditAnywhere, Category = "Debug General")
	ENiagaraDebugHudSystemVerbosity HudVerbosity = ENiagaraDebugHudSystemVerbosity::Minimal;

	/**
	When enabled GPU particles will be debuggable by copying the data to CPU
	accessible memory, this has both a performance & memory cost.  The data
	is also latent so expect a frame or two of lag.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	bool bEnableGpuReadback = false;

	/** Modifies the display location of the HUD overview. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	FIntPoint HUDLocation = FIntPoint(30.0f, 150.0f);

	/** Selects which font to use for the HUD display. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	ENiagaraDebugHudFont HUDFont = ENiagaraDebugHudFont::Normal;

	/**
	When enabled all Niagara systems that pass the filter will have the simulation data buffers validation.
	i.e. we will look for NaN or other invalidate data  inside it
	Note: This will have an impact on performance.
	*/
	UPROPERTY(EditAnywhere, Category = "Debug General")
	bool bValidateSystemSimulationDataBuffers = false;

	/**
	When enabled all Niagara systems that pass the filter will have the particle data buffers validation.
	i.e. we will look for NaN or other invalidate data  inside it
	Note: This will have an impact on performance.
	*/
	UPROPERTY(EditAnywhere, Category = "Debug General")
	bool bValidateParticleDataBuffers = false;

	/**
	Wildcard filter which is compared against the Components Actor name to narrow down the detailed information.
	For example, "*Water*" would match all actors that contain the string "water".
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bActorFilterEnabled"))
	FString ActorFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bComponentFilterEnabled = false;

	/**
	Wildcard filter for the components to show more detailed information about.
	For example, "*MyComp*" would match all components that contain MyComp.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bComponentFilterEnabled"))
	FString ComponentFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bSystemFilterEnabled = false;

	/**
	Wildcard filter for the systems to show more detailed information about.
	For example,. "NS_*" would match all systems starting with NS_.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bSystemFilterEnabled"))
	FString SystemFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bEmitterFilterEnabled = false;

	/**
	Wildcard filter used to match emitters when generating particle attribute view.
	For example,. "Fluid*" would match all emtiters starting with Fluid and only particle attributes for those would be visible.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bEmitterFilterEnabled"))
	FString EmitterFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bActorFilterEnabled = false;

	/** Modifies the in world system display information level. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	ENiagaraDebugHudSystemVerbosity SystemVerbosity = ENiagaraDebugHudSystemVerbosity::Basic;

	/** When enabled will show the system bounds for all filtered systems. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowBounds = false;

	/** When disabled in world rendering will show systems deactivated by scalability. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowActiveOnlyInWorld = true;

	/** Should we display the system attributes. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (DisplayName="Show System Attributes"))
	bool bShowSystemVariables = true;

	/**
	List of attributes to show about the system, each entry uses wildcard matching.
	For example, "System.*" would match all system attributes.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (DisplayName="System Attributes"))
	TArray<FNiagaraDebugHUDVariable> SystemVariables;

	/** Selects which font to use for system information display. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	ENiagaraDebugHudFont SystemFont = ENiagaraDebugHudFont::Small;

	/** When enabled will show particle attributes from the list. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (DisplayName="Show Particle Attributes"))
	bool bShowParticleVariables = true;

	/**
	List of attributes to show per particle, each entry uses wildcard matching.
	For example, "*Position" would match all attributes that end in Position.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (DisplayName="Particle Attributes"))
	TArray<FNiagaraDebugHUDVariable> ParticlesVariables;

	/** Selects which font to use for particle information display. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	ENiagaraDebugHudFont ParticleFont = ENiagaraDebugHudFont::Small;

	/**
	When enabled particle attributess will display with the system information
	rather than in world at the particle location.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (DisplayName="Show Particles Attributes With System"))
	bool bShowParticlesVariablesWithSystem = false;

	/**
	Maximum number of particles to show information about.
	Set to 0 to show all attributes, but be warned that displaying information about 1000's of particles
	will result in poor editor performance & potentially OOM on some platforms.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	int32 MaxParticlesToDisplay = 32;

	UPROPERTY()
	ENiagaraDebugPlaybackMode PlaybackMode = ENiagaraDebugPlaybackMode::Play;

	UPROPERTY()
	bool bPlaybackRateEnabled = false;

	UPROPERTY(Config)
	float PlaybackRate = 0.25f;

	UPROPERTY(Config)
	bool bLoopTimeEnabled = false;

	UPROPERTY(Config)
	float LoopTime = 1.0f;
};

/** Message passed from debugger to client when it needs updated simple client info. */
USTRUCT()
struct NIAGARA_API FNiagaraRequestSimpleClientInfoMessage
{
	GENERATED_BODY()
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class NIAGARA_API UNiagaraDebugHUDSettings : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChangedDelegate;

	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta=(ShowOnlyInnerProperties))
	FNiagaraDebugHUDSettingsData Data;

#if WITH_EDITOR
	void PostEditChangeProperty();
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};


USTRUCT()
struct NIAGARA_API FNiagaraOutlinerSettings
{
	GENERATED_BODY()

	/** Press to trigger a single capture of Niagara data from the connected debugger client. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bTriggerCapture = false;

	/** Delay between pressing the capture button and the capture being taken. Allows time to influence the scene before a capture is taken. */
	UPROPERTY(EditAnywhere, Config, Category = "Settings")
	float CaptureDelay = 1.0f;

	//TODO:
	//Properties to control the capture? Optionally grab perf info etc.
};

/** Simple information on the connected client for use in continuous or immediate response UI elements. */
USTRUCT()
struct NIAGARA_API FNiagaraSimpleClientInfo
{
	GENERATED_BODY()

	/** List of all system names in the scene. */
	UPROPERTY(EditAnywhere, Category = "Info")
	TArray<FString> Systems;

	/** List of all actors with Niagara components. */
	UPROPERTY(EditAnywhere, Category = "Info")
	TArray<FString> Actors;
	
	/** List of all Niagara components. */
	UPROPERTY(EditAnywhere, Category = "Info")
	TArray<FString> Components;
	
	/** List of all Niagara emitters. */
	UPROPERTY(EditAnywhere, Category = "Info")
	TArray<FString> Emitters;
};