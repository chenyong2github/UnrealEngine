
// -------------------------------------------------------------------------------------------------------------------------------------------------
//	Network Prediction Plugin
// -------------------------------------------------------------------------------------------------------------------------------------------------

High level overview:
-This is a *WIP* plugin that aims to generalize and improve our player prediction systems.
-Long term we hope this replaces UCharacterMovementComponent and its networking system (ServerMove, ClientAck*, ClientAdjust*, etc).
-The GameplayAbility system will be updated to interop with this as well.
-NetworkPrediction plugin is the core generalized system that you need to enable to use anything.
	-For now at least, new movement classes build on top of this will live here but could one day be moved out into a dedicated movement system plugin.
-NetworkPredictionExtras plugin contains some examples that may be helpful in learning the system. It is not inteded to be used directly in shipping games.

High level goals:
-Make working with predicted gameplay systems such as character movement easier.
-Give predictived gameplay systems the ability to interop with each other (E.g, CharacterMovement and Gameplay Abilities).
-Provide better tooling to understand what is going on in these complex predicted gameplay systems.
-This all leads to: ability to develop richer and more interactive gameplay systems that work in multiplayer games.


Current State (7-25-19)
-Initial public check in. Core simulation model structure is in place. Lots of pieces missing but overall system is roughed in.
-Two main examples right now: Flying Movement and 'MockNetworkSimulation' (in NetworkPredictionExtras)
-Flying Movement: a port of FloatingPawnMovement into the new system. Essentially just a basic flying movement system.
-MockNetworkSimulation: a very basic minimal "simulation" that demonstrates how the system works. Not tied to movement or anything physical: just a running counter/sum.

Getting Started:
-Both NetworkPrediction and NetworkPredictionExtras are disabled by default. You will need to enable them for your project (manually edit .uproject or do so through editor plugin screen).
-Once NetworkPredictionExtras is loaded, /NetworkPredictionExtras/Content/TestMap.umap can be loaded (must enable 'Show Engine Content' AND 'Show Plugin Content' in content browser!).
-The "MockNetworkSimulation" can be tested anywhere just by typing "mns.Spawn".

Code to look at:
-MockNetworkSimulation.h: Good place to start to see a bare minimum example of a predicted network simulation. Both the simulation and actor component live in this header.
-NetworkPredictionExtrasFlyingPawn.h: Starting point to see how a pawn using flying movement is setup. In NetworkPredictionExtras.
-FlyingMovement.h: The actual implementation of the flying movement system. In NetworkPrediction.
-NetworkSimulationModelTemplates.h: this is the lowest level guts of the system. This is what every simulation ultimately uses to stay in sync.

Extremely high level technical overview:
-Concept of "NetworkedSimulationModel": defining a tightly constrained (gameplay) simulation that can be run predictively on clients and authoratatively on server. E.g., a "Movement System" is a NetworkedSimulationModel.
-The simulation has tightly defined input and output. At the lowest level we have generic buffers of structs that are synchronized:
	Input Buffer: the data that the controlling client generates
	Sync Buffer: the data that evolves frame to frame and is produced by ticking the simulation
	Aux Buffer: additional data that can change, can be predicted, etc but does not necessarily get updated frame to frame (e.g, somethign else usually updates it).
-Implicit correction: all prediction and correction logic is now client side. Server just tells clients what the authority state was and it is up to them to reconcile.
-Single code path: the core network simulation has one "Update" function that does not branch based on authority/prediction/simulated etc. "Write once" is the goal for gamplay code here.

UE4 Networking info (How this plugs into UE4 networking)
-See UNetworkPredictionComponent for the glue.
-"Replication Proxy" (FReplicationProxy): struct that points to the network simulation and has policies about how it replicates and to who. Custom NetSerialize.
-E.g, there is a replication proxy for owner, non owners, replays, and debug. All point to the same underlying data.
-Still using actor replication to determine when to replicate.



TODO: Major missing elements
-Aux Buffer is not implemented, but referenced in a few places.
-"Events" into and out of the system are not in place. That is, something happens in your movement sim that needs to call outside code/generate an event. A system is needed to track this stuff rather than just calling directly into the handler.
-Interpolation/smoothing layer (visual, non simulation affecting smoothing to help with corrections and simulated proxies)
-No optimizations (bandwith, cpu) have been done.
-[Movement specific]: must lock down SetActorLocation/Rotation API so that movement state can be changed out from underneath the system.

Short term road map:
-The focus right now is on the generalized prediction system, rather than actual movement code.
-Next will be interopping the Flying Movement example with Ability System (E.g., movement abilities, stamina/fuel attributes, etc).
-Once everything feels solid we will go deeper on movement and build out a more extendable movement system.
-E.g, We don't intend all users of the engine to write their own network simulation model movement system.
-We hope to provide a generalized movement system that is easy to extend in itself without knowing all the details of the NetwokrPrediction plugin.
-We intend to support RootMotion (Animation) / RootMotionSources (non anim) in the new movement system.





