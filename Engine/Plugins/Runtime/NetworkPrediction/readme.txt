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
-Aux Buffer is not implemented, but referenced in a few places.
-"Events" into and out of the system are not in place. That is, something happens in your movement sim that needs to call outside code/generate an event. A system is needed to track this stuff rather than just calling directly into the handler.
-Interpolation/smoothing layer (visual, non simulation affecting smoothing to help with corrections and simulated proxies)
-No optimizations (bandwidth, cpu) have been done.
-[Movement specific]: must lock down SetActorLocation/Rotation API so that movement state can be changed out from underneath the system.
 
Short term road map:
-The focus right now is on the generalized prediction system, rather than actual movement code.
-We are exploring "forward predicting non autonomous proxies" with the parametric mover. Unclear how deep we will go with this.
-Then Aux buffer and "events"
-Then mixing in ability system + other scripting options
 
-Once everything feels solid we will go deeper on movement and build out a more extendable movement system.
-E.g, We don't intend all users of the engine to write their own network simulation model movement system.
-We hope to provide a generalized movement system that is easy to extend in itself without knowing all the details of the NetworkPrediction plugin.
-We intend to support RootMotion (Animation) / RootMotionSources (non anim) in the new movement system.
 
 
// ----------------------------------------------------------------------------------------------------------
// Glossary
// ----------------------------------------------------------------------------------------------------------
 
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
 
 
 


