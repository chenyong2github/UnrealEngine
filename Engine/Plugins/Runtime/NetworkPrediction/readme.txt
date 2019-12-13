// -------------------------------------------------------------------------------------------------------------------------------------------------
//	Network Prediction Plugin
// -------------------------------------------------------------------------------------------------------------------------------------------------
 
High level overview:
-This is a *WIP* plugin that aims to generalize and improve our player prediction systems.
-Long term we hope this replaces UCharacterMovementComponent and its networking system (ServerMove, ClientAck*, ClientAdjust*, etc).
-The GameplayAbility system will be updated to interop with this as well.
-NetworkPrediction plugin is the core generalized system that you need to enable to use anything.
	-For now at least, new movement classes built on top of this will live here but could one day be moved out into a dedicated movement system plugin.
-NetworkPredictionExtras plugin contains some examples that may be helpful in learning the system. It is not inteded to be used directly in shipping games.
 
High level goals:
-Make working with predicted gameplay systems such as character movement easier.
-Give predictived gameplay systems the ability to interop with each other (E.g, CharacterMovement and Gameplay Abilities).
-Provide better tooling to understand what is going on in these complex predicted gameplay systems.
-This all leads to: ability to develop richer and more interactive gameplay systems that work in multiplayer games.
 
What this is NOT:
-We are not moving to a global fixed ticking system. Though these systems have huge advantages, we don't think it is a good universal solution for the engine.
-That said, we recognize that it is the right solution for many things and we want to enable support for that. Our goal is that the user simulation code is independent of ticking being fixed or not.
-To be clear: widely different, variable, fluctuating tick rates between clients and server is basically the root problems we are fighting here.
-Related, we are also not all in on a "predict everything: seamlessly and automatically" solution. We still feel player prediction is best kept to a minimum (meaning: predict the minimum amount of stuff you can get away with).
-But, we are trying to make it way easier to write predictive gameplay systems. Easier to write, to debug and reason about.
-We do hope the new movement system will be automatic in this sense: if you write a new movement mode within the movement simulation class, things should "just work". (But we are not trying to make your entire game predicted)
 
 
Getting Started:
-Both NetworkPrediction and NetworkPredictionExtras are disabled by default. You will need to enable them for your project (manually edit .uproject or do so through editor plugin screen).
-Once NetworkPredictionExtras is loaded, /NetworkPredictionExtras/Content/TestMap.umap can be loaded (must enable 'Show Engine Content' AND 'Show Plugin Content' in content browser!).
-The "MockNetworkSimulation" can be tested anywhere just by typing "mns.Spawn".
 
Code to look at:
-MockNetworkSimulation.h: Good place to start to see a bare minimum example of a predicted network simulation. Both the simulation and actor component live in this header.
-NetworkPredictionExtrasFlyingPawn.h: Starting point to see how a pawn using flying movement is setup. In NetworkPredictionExtras.
-FlyingMovement.h: The actual implementation of the flying movement system. In NetworkPrediction.
-NetworkSimulationModel.h: this is the lowest level guts of the system. This is what every simulation ultimately uses to stay in sync.
-FMockAbilitySimulation::Update an example "ability system" working on top of a movement simulation
 
Extremely high level technical overview:
-Concept of "NetworkedSimulationModel": defining a tightly constrained (gameplay) simulation that can be run predictively on clients and authoritatively on server. E.g., a "Movement System" is a NetworkedSimulationModel.
-The simulation has tightly defined input and output. At the lowest level we have generic buffers of structs that are synchronized:
	Input Buffer: the data that the controlling client generates
	Sync Buffer: the data that evolves frame to frame and is produced by ticking the simulation
	Aux Buffer: additional data that can change, can be predicted, etc but does not necessarily get updated frame to frame (e.g, something else usually updates it).
-Implicit correction: all prediction and correction logic is now client side. Server just tells clients what the authority state was and it is up to them to reconcile.
-Single code path: the core network simulation has one "Update" function that does not branch based on authority/prediction/simulated etc. "Write once" is the goal for gameplay code here.
 
UE4 Networking info (How this plugs into UE4 networking)
-See UNetworkPredictionComponent for the glue.
-"Replication Proxy" (FReplicationProxy): struct that points to the network simulation and has policies about how it replicates and to who. Custom NetSerialize.
-E.g, there is a replication proxy for owner, non owners, replays, and debug. All point to the same underlying data.
-Still using actor replication to determine when to replicate.

Simulated Proxy behavior / "modes"
-Interpolate: Sync buffer is replicated but there is no ticking of the net sim. Literally do not have to Tick the simulation.
	-Actual interpolation will happen in some class outside of TNetworkSimulationModel (doesn't exist yet). It will look at sync buffer and interpolate over it.
-(Sim) Extrapolation: by default if the netsim is ticked, we synthesize command and extrapolate the simulation. With basic reconciliation to absorb fluctuations in latency.
-Forward Predict (not implemented yet): must be explicitly enabled by outside call, ties simulated proxy sim to an autonomous proxy sim. Sims will be reconciled together in step.
 
 
TODO: Major missing elements
-"Events" into and out of the system are not in place. That is, something happens in your movement sim that needs to call outside code/generate an event. A system is needed to track this stuff rather than just calling directly into the handler.
-No optimizations (bandwidth, cpu) have been done.
-[Movement specific]: must lock down SetActorLocation/Rotation API so that movement state can be changed out from underneath the system.
 
High level focus:
-The focus right now is on the generalized prediction system, rather than actual movement code.
-We are exploring "forward predicting non autonomous proxies" with the parametric mover. Unclear how deep we will go with this.
-Then Aux buffer and "events"
-Then mixing in ability system + other scripting options
 
-Once everything feels solid we will go deeper on movement and build out a more extendable movement system.
-E.g, We don't intend all users of the engine to write their own network simulation model movement system.
-We hope to provide a generalized movement system that is easy to extend in itself without knowing all the details of the NetworkPrediction plugin.
-We intend to support RootMotion (Animation) / RootMotionSources (non anim) in the new movement system.

Road Map:
-New Movement System / Ability System integration
 
 
// ----------------------------------------------------------------------------------------------------------
// Glossary
// ----------------------------------------------------------------------------------------------------------

"Simulation": should always refer to use code. The simulation is what we are networking. It doesn't matter what the simulation is actually doing, we are building a system to network it like a black box.
"System": should always refer to the TNetworkedSimulationModel code: the generic code that is doing the networking. It is distinct from the simulation.


Reconcile: When a predicting client receives authoritative data from the server that conflicts with what they previously predicted: sorting this out is called "reconciling". Usually means you will resimulate.
Synthesize: In general means "making shit up". For example, "synthesizing user commands" would mean "creating user commands that were not generated by a player, but instead guessing what it might be".
 
 
Client Prediction: using new, locally created information (input cmd) to evolve the game state ahead of the authoritative server.
(Client) Extrapolation: Taking the latest received state from the authoritative server and "guessing" how it continues to evolve. The key difference is you are not using new information that the server does not have yet.
 
Forward Predicting: Using Client Prediction to predict ahead N frames, that is proportional to the ping time with the server.
(Sim) Extrapolation: Using the network sim model to extrapolate frames, by (usually) synthesizing input cmds. Extrapolation is "one frame at a time" rather than "N frames based on ping".
Interpolation: Interpolating between known states. E.g, you are never guessing at future state.
	Note: We may technically allow interpolation past "100%", which would technically make it extrapolation (you are guessing at state you don't have yet). 
	This would still be distinct from the above "Sim" Extrapolation where the network sim is used to generate future data instead of simple "interpolation" algorithms.
Smoothing: Taking the output of the simulation and applying an additional layer of "ease in/ease out" updates. Smoothing would use things like spring equations and "lag constants" rather than being tied to the actual simulation itself.
 
 
// ----------------------------------------------------------------------------------------------------------
// Release notes
// ----------------------------------------------------------------------------------------------------------

Update (12-13-19)
-Only small updates (if any) for the rest of the year. System is going through code reviews as we plan for writing a new general movement system on top of Network Prediction.
-Things are still liable to change but for the most part every major feature is in.

Update (11-21-19)
-Rewindable NetSimCue support added. This just about wraps up the initial implemention of NetSimCues and the "Game API / Event System".
-Still some cleanup and debugging/logging stuff to do but will be transitioning into porting the legacy CMC movement system over soon.

Update (11-15-19)
-NetSimCues initial check in
-These are the async/deferred events that are outputted during the network simulation tick
-Different classification in types wrt prediction and replication
-See FMockAbilityBlinkCue in code, "Misc Notes" at bottom of readme file
-"Rewindable NetSimCue" api still todo

Update (10-28-19)
Renaming/cleanup on ::Update function:
-"TSimulation::Update" is now "TSimulation::SimulationTick". Makes searching through source a bit easier, less generic name
-Collapsed parameters of SimulationTick into 3 containers: FNetSimTimeStep, TNetSimInput, TNetSimOutput
-Time passed into simulation is now FNetworkSimTime (instead of forcing float RealTimeSeconds conversion at callsite)
-TSimulationTickState broken up into FSimulationTickState and TSimulationTicker. 
-::SimulationTick now also gets a reference to FSimulationTickState. This is setting us up to do timers/countdowns based on sim time within these functions.

Update (10-25-19)
-Another large refactor on the core classes. Things are feeling good!
-"Simulation Driver" and a few other concepts have changed:
	-"Simulation" is now implemented by a simulation class. What was previously a static method + a "driver" pointer, is now an instantiated C++ class that user code creates and passes into TNetworkedSimulationModel.
	-The Simulation (previously driver) is now longer implemented by the owning actor/component. This simplifies a lot of things and make extending simulation much less awkward.
	-TNetworkSimDriverInterfaceBase --> TNetworkedSimulationModelDriver. This is now distinct from "what the simulation needs to call internally" (previously defined by the driver now the simulation).
	-TNetworkedSimulationModelDriver ("System Driver") is the set of functions the system internally needs to call.
	-Visual logging has been refactored to be a single function of the system driver that takes all 3 states instead of single methods on the individual system types (Input,Sync,Aux). this simplifies a lot of stuff and allows you to visual sync+aux together.
-"MockAbilitySystem" has been implemented.
	-Simple, "hardcoded" abilities that function on top of the flying movement simulation. See notes in MockAbilitySimulation.h
	-This code gives a good glimpse into what GameplayAbilities will look like (again, more hardcoded in this example but same techniques will be applied).


Update (10-16-19)
-Cleanup how we advance the simulation and unified which keyframes are accessed across the three main buffers
-LastProcessedInputKeyframe -> PendingKeyframe. Code is simplified dealing with the "next" keyframe rather than making assumptions based on "last" keyframe.
-Input/Sync/Aux states @ PendingKeyframe are what gets used as inputs to T::Update. This is more consistent that how it worked previously, where it was essentially (Input+1/Sync) -> (Sync+1).
-This gets rid of awkward hacks like empty input cmd @ keyframe = 0.
-Also got rid of InitSync/InitAuxState interface functions. The initial states can now be provided when instantiating the network sim model.

Update (10-15-19)
-Aux buffer hooked up. Still some decisions to make here, expect a few more refactors.
-TReplicationBuffer replaced with simpler API.

Update (10-1-19)
-Forward Predict / Dependent simulation initial check in. This has some limitations and is not final
	-Dependent Sim can't trigger reconcile yet
	-Assumptions made about replication frequency. Dependents must replicate as same frequency as parent sim right now.
	-General lack of precision here when running in variable tick simulation. (Sims tick at different rates on server, so hard to correlate client side).


Update (9-26-19)
-More refactors on general system architecture. UNetworkSimulationGlobalManager is solidifying and most boiler plate is out of the actor components (Not an exciting update but this was overdue!).
-PostSimTick added and Interpolator refactored to be part of this step, dependent on network role (Eg., simulated proxy only when enabled).
-Continuing to investigate dependent actor simulations. A few early prototypes did not work out but am going to try a slightly different approach.

Update (8-14-19)
-Some more cleanup. Cvar fixes and other consolidation. Now time for vacation (2 weeks). 
TODO still:
-Improve debugger (has rotted a bit due to refactors)
-Network fault handling, disable/enable local prediction causes some bad transitions
-Initialization of system, specifically InitializeForNetworkRole can be much better now.
	-Consider using templates to inline allocate all memory. Maybe make auto proxy buffers allocated on demant.

Update (8-13-19)
-Refactor really feeling good, things are falling into place. Still a few things to do and bugs to chase down.
-Added simulated proxy notes above to detail current plan

Update
-Refactor on ticking state is taking shape and feeling much better. Some thing may have broke but will get smoothed out.
-New goal is to support fixed ticking seamlessly. Meaning, when you instantiate or define your network sim you can opt into fix ticking and this won't affect how your simulation code works.
-(Nice thing is that storing frame time on the user cmds is now an internal detail of the system and transparent to users. So you will not need seperate sets of input cmds for a fix tixed version of your sim)


Update (8-9-19)
-Big refactor underway within the NetworkSimModel. Some files are renamed or moved as well. Still a bit to do but this was a good commit point.
-Mainly to facilitate clean implementation of interpolation/extrapolation/forward predict option for simulated proxy simulations: this was exposing weaknesses in the templated implementations.
-Working towards improving "how and when simulation is allowed to advanced" (see TNetworkSimulationTickInfo)
-Incomplete, but thinking about global management of active simulations: making sure reconcile and Tick are called in the right places. See notes in NetworkSimulationGlobalManager.h

 
Update (8-2-19)
-We want to take a look non player controlled simulations, such as doors, elevators, "pushers" before going deeper on ability system or movement.
-Basic ParametricMoverment system checked in. This is also in an incomplete state. ("Pushing" the flying movement component does not work and won't be the focus for now until we are looking for closely at movement specifically).
-The short term goal here is to be able to forward predict these even if they are simulated proxies. When/why/how is very tbd, but we want to see what it looks like.
 
 
Current State (7-25-19)
-Initial public check in. Core simulation model structure is in place. Lots of pieces missing but overall system is roughed in.
-Two main examples right now: Flying Movement and 'MockNetworkSimulation' (in NetworkPredictionExtras)
-Flying Movement: a port of FloatingPawnMovement into the new system. Essentially just a basic flying movement system.
-MockNetworkSimulation: a very basic minimal "simulation" that demonstrates how the system works. Not tied to movement or anything physical: just a running counter/sum.
 

// ----------------------------------------------------------------------------------------------------------
// Misc Notes
// ----------------------------------------------------------------------------------------------------------

Notes on "State Transitions"
-Detecting changes to sync/aux state in FinalizeFrame
-Can emit events from these transitions
-Events can't have additional data (thats not in the sync/aux state)
-Can't really know for sure if transition "just actually happnened in the simulation" vs "I'm just finding out about it now"
-Biggest advantage: "Reliable", doesn't require SimulationTick to run. (Interpolated proxies can still use them)

Notes on "Events" --> NetSimCues

Basic idea/notes:
-"Non simulation affecting" events that are emitted during the ::SimulationTick function.
-Cues are *handled* (by user code) during actor tick. Not during ::SimulationTick. (they are queued in a buffer)
-Must support arbitrary data payloads. E.g, "Impact Velocity" for a "Landed" cue.
-Fundamentally unreliable: join in progress, relevancy, temporary network disconnect = missed events.
	-Seriously! These events should never be used to "track state" at some higher level. Even if simulation code guaruntees 1:1 On/Off events, you may miss them!
	-Note: see "State transitions" above for how "reliable" events would be done

Implementation progression:

0. Niave approach is to let simulation code call directly into user code at anytime to do this stuff
	Major issue: rollback/resimulate will double play events	
	Interpolating proxies would not invoke because they do not run the simulation

[+Simple Invocation Suppressiong mechanism/context]
1. Simplest approach would be "Weak Cues": "Cues don't replicate. Are only played on 'latest' simulation ticks (supressed during any rewind/resimulate scope)"
	This causes/leaves two main issues/needs:
		A. Interpolating simulated proxies (who don't run TickSimulation) will never invoke these cues. 
			(Note: could just say "yup, thats how it works" and leave replication up to the user at a higher level. Not great though!)
		B. Predicting Client can mispredict: missed events or can play wrong events. Won't catch this / don't care.

[+Cue type traits/policies, build on invocation supression above. Cue data doesn't matter yet, just local context and cue type]
[+Time information passed to cue handler]
2. Next simplest approach would be to add two more cues types (in addition to "Weak Cues"):
	A. Replicated, non predicted. Always comes from server, never predicted. Everyone gets them.
		-Downside: we won't predict them since we can't guard against double playing yet.
		-Downside: cues can fire off "in the past" and now we must include time information to Cue handler (let them figure out how to deal with time make up)
	B. Replicated (to sim proxy only), predicted (by auto proxy only).
		-Allow auto proxy to treat them as Weak Cues: always play on simulate (not resimulate), always ignore server replication (so, can mispredict)
		-Sim proxy will have them replicated explicitly	

[+Cue identification/uniqueness function]
3. Next complicated approach would be to have identifying/uniqueness function to determine cues that were predicted or not. "Strong Cues"
	Keep buffer of last N msecs of cues that were invoked locally.
	When receiving replicated cues, look for matches that are "close enough" (hard to define!). If we already predicted, then ignore from server.
	+This allows us to combine #2's cue types into a single type: Replicated + predicted (universal)		
	+Can also now invoke while resimulating by checking if a matching cue was already invoked during the original predicted simulate.
	
[+Cue rollback/resimulate callbacks]
4. Final complicated approach: full rollback/resimulate aware cue system
	Cues are invoked with callbacks that can be subcribed to: Rewind, Resimulate, Confirmed. This is the only way to support "undoing" cues.
	+Allows cues that "didn't actually happen" (mispredict) to be undone.
	+Allows "all cues" to be undown on rollback and then repredicted during resimulate. (How important is this actually given everything else???)
		-Allows us to "skip" the uniqueness test at the cost of redundantly rolling back -> redoing cues that were not mispredicted

	
	
	