// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
All common code shared between the editor side debugger and debugger clients running in game.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotifyHook.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDebuggerCommon.generated.h"

#define WITH_NIAGARA_DEBUGGER !UE_BUILD_SHIPPING

//////////////////////////////////////////////////////////////////////////
// Niagara Outliner.

USTRUCT()
struct FNiagaraOutlinerTimingData
{
	GENERATED_BODY()

	/** Game thread time, including concurrent tasks*/
	UPROPERTY(VisibleAnywhere, Category="Time")
	float GameThread = 0.0f;

	/** Render thread time. */
	UPROPERTY(VisibleAnywhere, Category="Time")
	float RenderThread = 0.0f;
};

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
struct NIAGARA_API FNiagaraOutlinerSystemInstanceData
{
	GENERATED_BODY()

	FNiagaraOutlinerSystemInstanceData();

	/** Name of the component object for this instance, if there is one. */
	UPROPERTY(VisibleAnywhere, Category = "System")
	FString ComponentName;

	UPROPERTY(VisibleAnywhere, Category = "System")
	TArray<FNiagaraOutlinerEmitterInstanceData> Emitters;
	
	UPROPERTY(VisibleAnywhere, Category = "State")
	ENiagaraExecutionState ActualExecutionState = ENiagaraExecutionState::Num;

	UPROPERTY(VisibleAnywhere, Category = "State")
	ENiagaraExecutionState RequestedExecutionState = ENiagaraExecutionState::Num;
	
	UPROPERTY(VisibleAnywhere, Category = "State")
	FNiagaraScalabilityState ScalabilityState;
	
	UPROPERTY(VisibleAnywhere, Category = "State")
	uint32 bPendingKill : 1;
	
	UPROPERTY(VisibleAnywhere, Category = "State")
	ENCPoolMethod PoolMethod;

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData AverageTime;

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData MaxTime;

	//TODO:
	//Tick info, solo, tick group etc.
	//Mem usage?
};

/** Wrapper for array of system instance outliner data so that it can be placed in a map. */
USTRUCT()
struct FNiagaraOutlinerSystemData
{
	GENERATED_BODY()

	//TODO: Cache off any shared representation of the system and emitters here for the instances to reference. 

	/** Map of System Instance data indexed by the UNiagaraSystem name. */
	UPROPERTY(VisibleAnywhere, Category = "System")
	TArray<FNiagaraOutlinerSystemInstanceData> SystemInstances;

	
	
	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData AveragePerFrameTime;

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData MaxPerFrameTime;
	
	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData AveragePerInstanceTime;

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData MaxPerInstanceTime;
};

/** All information about a specific world for the Niagara Outliner. */
USTRUCT()
struct FNiagaraOutlinerWorldData
{
	GENERATED_BODY()

	/** Map of System Instance data indexed by the UNiagaraSystem name. */
	UPROPERTY(VisibleAnywhere, Category = "World")
	TMap<FString, FNiagaraOutlinerSystemData> Systems;

	UPROPERTY(VisibleAnywhere, Category = "State")
	bool bHasBegunPlay = false;

	UPROPERTY(VisibleAnywhere, Category = "State")
	uint8 WorldType = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "State")
	uint8 NetMode = INDEX_NONE;
	

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData AveragePerFrameTime;

	UPROPERTY(VisibleAnywhere, Category = "Performance")
	FNiagaraOutlinerTimingData MaxPerFrameTime;

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
enum class ENiagaraDebugHudHAlign : uint8
{
	Left,
	Center,
	Right
};

UENUM()
enum class ENiagaraDebugHudVAlign : uint8
{
	Top,
	Center,
	Bottom
};

UENUM()
enum class ENiagaraDebugHudFont
{
	Small = 0,
	Normal,
};

UENUM()
enum class ENiagaraDebugHudVerbosity
{
	None,
	Basic,
	Verbose,
};

USTRUCT()
struct FNiagaraDebugHudTextOptions
{
	GENERATED_BODY()
		
	UPROPERTY(Config, EditAnywhere, Category = "Text Options")
	ENiagaraDebugHudFont Font = ENiagaraDebugHudFont::Small;

	UPROPERTY(EditAnywhere, Category = "Text Options")
	ENiagaraDebugHudHAlign	HorizontalAlignment = ENiagaraDebugHudHAlign::Left;

	UPROPERTY(EditAnywhere, Category = "Text Options")
	ENiagaraDebugHudVAlign	VerticalAlignment = ENiagaraDebugHudVAlign::Top;

	UPROPERTY(EditAnywhere, Category = "Text Options")
	FVector2D ScreenOffset = FVector2D::ZeroVector;
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

	/** Master control for all HUD features. */
	UPROPERTY(EditAnywhere, Category = "Debug General", meta = (DisplayName = "Debug HUD Enabled"))
	bool bEnabled = true;

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

	/** When enabled the overview display will be enabled. */
	UPROPERTY(EditAnywhere, Category = "Debug Overview", meta = (DisplayName = "Debug Overview Enabled"))
	bool bOverviewEnabled = false;

	/** Overview display font to use. */
	UPROPERTY(EditAnywhere, Category = "Debug Overview", meta = (DisplayName = "Debug Overview Font", EditCondition = "bOverviewEnabled"))
	ENiagaraDebugHudFont OverviewFont = ENiagaraDebugHudFont::Normal;

	/** Overview display location. */
	UPROPERTY(EditAnywhere, Category = "Debug Overview", meta = (DisplayName = "Debug Overview Text Location", EditCondition = "bOverviewEnabled"))
	FVector2D OverviewLocation = FIntPoint(30.0f, 150.0f);

	/**
	Wildcard filter which is compared against the Components Actor name to narrow down the detailed information.
	For example, "*Water*" would match all actors that contain the string "water".
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bActorFilterEnabled"))
	FString ActorFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (InlineEditConditionToggle))
	bool bComponentFilterEnabled = false;

	/**
	Wildcard filter for the components to show more detailed information about.
	For example, "*MyComp*" would match all components that contain MyComp.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bComponentFilterEnabled"))
	FString ComponentFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (InlineEditConditionToggle))
	bool bSystemFilterEnabled = false;

	/**
	Wildcard filter for the systems to show more detailed information about.
	For example,. "NS_*" would match all systems starting with NS_.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bSystemFilterEnabled"))
	FString SystemFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (InlineEditConditionToggle))
	bool bEmitterFilterEnabled = false;

	/**
	Wildcard filter used to match emitters when generating particle attribute view.
	For example,. "Fluid*" would match all emtiters starting with Fluid and only particle attributes for those would be visible.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (EditCondition = "bEmitterFilterEnabled"))
	FString EmitterFilter;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter", meta = (InlineEditConditionToggle))
	bool bActorFilterEnabled = false;

	/** When enabled system debug information will be displayed in world. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	ENiagaraDebugHudVerbosity SystemDebugVerbosity = ENiagaraDebugHudVerbosity::Basic;

	/** When enabled we show information about emitter / particle counts. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (EditCondition = "SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None"))
	ENiagaraDebugHudVerbosity SystemEmitterVerbosity = ENiagaraDebugHudVerbosity::Basic;

	/** When enabled will show the system bounds for all filtered systems. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowBounds = false;

	/** When disabled in world rendering will show systems deactivated by scalability. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (EditCondition = "SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None"))
	bool bSystemShowActiveOnlyInWorld = true;

	/** Should we display the system attributes. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (EditCondition = "SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None", DisplayName="Show System Attributes"))
	bool bShowSystemVariables = true;

	/**
	List of attributes to show about the system, each entry uses wildcard matching.
	For example, "System.*" would match all system attributes.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta = (EditCondition = "SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None && bShowSystemVariables", DisplayName="System Attributes"))
	TArray<FNiagaraDebugHUDVariable> SystemVariables;

	/** Sets display text options for system information. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System", meta=(EditCondition="SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None"))
	FNiagaraDebugHudTextOptions SystemTextOptions;

	/** When enabled will show particle attributes from the list. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (DisplayName="Show Particle Attributes"))
	bool bShowParticleVariables = true;

	/**
	When enabled GPU particle data will be copied from the GPU to the CPU.
	Warning: This has an impact on performance & memory since we copy the whole buffer.
	The displayed data is latent since we are seeing what happened a few frames ago.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bShowParticleVariables"))
	bool bEnableGpuParticleReadback = false;

	/**
	List of attributes to show per particle, each entry uses wildcard matching.
	For example, "*Position" would match all attributes that end in Position.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bShowParticleVariables", DisplayName="Particle Attributes"))
	TArray<FNiagaraDebugHUDVariable> ParticlesVariables;

	/** Sets display text options for particle information. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bShowParticleVariables"))
	FNiagaraDebugHudTextOptions ParticleTextOptions;

	/**
	When enabled particle attributes will display with the system information
	rather than in world at the particle location.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bShowParticleVariables", DisplayName="Show Particles Attributes With System"))
	bool bShowParticlesVariablesWithSystem = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bShowParticleVariables"))
	bool bUseMaxParticlesToDisplay = true;

	/**
	When enabled, the maximum number of particles to show information about.
	When disabled all particles will show attributes, this can result in poor performance & potential OOM on some platforms.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles", meta = (EditCondition = "bUseMaxParticlesToDisplay && bShowParticleVariables", UIMin="1", ClampMin="1"))
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

	UPROPERTY(Config, EditAnywhere, Category = "Performance")
	bool bShowGlobalBudgetInfo = false;
};

/** Message passed from debugger to client when it needs updated simple client info. */
USTRUCT()
struct NIAGARA_API FNiagaraRequestSimpleClientInfoMessage
{
	GENERATED_BODY()
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class NIAGARA_API UNiagaraDebugHUDSettings : public UObject, public FNotifyHook
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChangedDelegate;

	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta=(ShowOnlyInnerProperties))
	FNiagaraDebugHUDSettingsData Data;

	void NotifyPropertyChanged();
	virtual void NotifyPreChange(FProperty*) {}
	virtual void NotifyPostChange(const FPropertyChangedEvent&, FProperty*) { NotifyPropertyChanged(); }
	virtual void NotifyPreChange(class FEditPropertyChain*) {}
	virtual void NotifyPostChange(const FPropertyChangedEvent&, class FEditPropertyChain*) { NotifyPropertyChanged(); }
};


USTRUCT()
struct NIAGARA_API FNiagaraOutlinerCaptureSettings
{
	GENERATED_BODY()

	/** Press to trigger a single capture of Niagara data from the connected debugger client. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bTriggerCapture = false;

	/** How many frames to delay capture. If gathering performance data, this is how many frames will be collected. */
	UPROPERTY(EditAnywhere, Config, Category="Settings")
	uint32 CaptureDelayFrames = 60;
	
	UPROPERTY(EditAnywhere, Config, Category = "Settings")
	bool bGatherPerfData = true;
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


enum class ENiagaraDebugMessageType : uint8
{
	Info,
	Warning,
	Error
};
struct FNiagaraDebugMessage
{
	ENiagaraDebugMessageType Type;
	FString Message;
	float Lifetime;
	FNiagaraDebugMessage()
		: Type(ENiagaraDebugMessageType::Error)
		, Lifetime(0.0f)
	{}
	FNiagaraDebugMessage(ENiagaraDebugMessageType InType, const FString& InMessage, float InLifetime)
		: Type(InType)
		, Message(InMessage)
		, Lifetime(InLifetime)
	{}
};