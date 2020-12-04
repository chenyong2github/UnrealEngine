// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "DataLayerAction.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "DataLayerEditorSubsystem.generated.h"

class AActor;
class FDataLayersBroadcast;
class FLevelEditorViewportClient;
class UEditorEngine;
class UDataLayer;
class ULevel;
class UWorld;
template<typename TItemType> class IFilter;

UCLASS()
class DATALAYEREDITOR_API UDataLayerEditorSubsystem final : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static UDataLayerEditorSubsystem* Get();

	typedef IFilter<const TWeakObjectPtr<AActor>&> FActorFilter;

	/**
	 *	Prepares for use
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 *	Internal cleanup
	 */
	virtual void Deinitialize() override;

	/**
	 *	Destructor
	 */
	virtual ~UDataLayerEditorSubsystem() {}

	/* Broadcasts whenever one or more DataLayers are modified
	 *
	 * Actions
	 * Add    : The specified ChangedDataLayer is a newly created UDataLayer
	 * Modify : The specified ChangedDataLayer was just modified, if ChangedDataLayer is invalid then multiple DataLayers were modified.
	 *          ChangedProperty specifies what field on the UDataLayer was changed, if NAME_None then multiple fields were changed
	 * Delete : A DataLayer was deleted
	 * Rename : The specified ChangedDataLayer was just renamed
	 * Reset  : A large amount of changes have occurred to a number of DataLayers.
	 */
	DECLARE_EVENT_ThreeParams(UDataLayerEditorSubsystem, FOnDataLayerChanged, const EDataLayerAction /*Action*/, const TWeakObjectPtr<const UDataLayer>& /*ChangedDataLayer*/, const FName& /*ChangedProperty*/);
	virtual FOnDataLayerChanged& OnDataLayerChanged() { return DataLayerChanged; }

	/** Broadcasts whenever one or more Actors changed DataLayers*/
	DECLARE_EVENT_OneParam(UDataLayerEditorSubsystem, FOnActorDataLayersChanged, const TWeakObjectPtr<AActor>& /*ChangedActor*/);
	virtual FOnActorDataLayersChanged& OnActorDataLayersChanged() { return ActorDataLayersChanged; }
	
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Operations on an individual actor.

	/**
	 *	Checks to see if the specified actor is in an appropriate state to interact with DataLayers
	 *
	 *	@param	Actor	The actor to validate
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool IsActorValidForDataLayer(AActor* Actor);

	/**
	 * Adds the actor to the DataLayer.
	 *
	 * @param	Actor		The actor to add to the DataLayer
	 * @param	DataLayer	The DataLayer to add the actor to
	 * @return				true if the actor was added.  false is returned if the actor already belongs to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool AddActorToDataLayer(AActor* Actor, UDataLayer* DataLayer);

	/**
	 * Adds the provided actor to the DataLayers.
	 *
	 * @param	Actor		The actor to add to the provided DataLayers
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if the actor was added to at least one of the provided DataLayers.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool AddActorToDataLayers(AActor* Actor, const TArray<UDataLayer*>& DataLayers);

	/**
	 * Removes an actor from the specified DataLayer.
	 *
	 * @param	Actor			The actor to remove from the provided DataLayer
	 * @param	DataLayerToRemove	The DataLayer to remove the actor from
	 * @return					true if the actor was removed from the DataLayer.  false is returned if the actor already belonged to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveActorFromDataLayer(AActor* Actor, UDataLayer* DataLayerToRemove);

	/**
	 * Removes the provided actor from the DataLayers.
	 *
	 * @param	Actor		The actor to remove from the provided DataLayers
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if the actor was removed from at least one of the provided DataLayers.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveActorFromDataLayers(AActor* Actor, const TArray<UDataLayer*>& DataLayers);


	/////////////////////////////////////////////////
	// Operations on multiple actors.

	/**
	 * Add the actors to the DataLayer
	 *
	 * @param	Actors		The actors to add to the DataLayer
	 * @param	DataLayer	The DataLayer to add to
	 * @return				true if at least one actor was added to the DataLayer.  false is returned if all the actors already belonged to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool AddActorsToDataLayer(const TArray<AActor*>& Actors, UDataLayer* DataLayer);

	/**
	 * Add the actors to the DataLayers
	 *
	 * @param	Actors		The actors to add to the DataLayers
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if at least one actor was added to at least one DataLayer.  false is returned if all the actors already belonged to all specified DataLayers.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	bool AddActorsToDataLayers(const TArray<AActor*>& Actors, const TArray<UDataLayer*>& DataLayers);

	/**
	 * Removes the actors from the specified DataLayer.
	 *
	 * @param	Actors			The actors to remove from the provided DataLayer
	 * @param	DataLayerToRemove	The DataLayer to remove the actors from
	 * @return					true if at least one actor was removed from the DataLayer.  false is returned if all the actors already belonged to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveActorsFromDataLayer(const TArray<AActor*>& Actors, UDataLayer* DataLayer);

	/**
	 * Remove the actors to the DataLayers
	 *
	 * @param	Actors		The actors to remove to the DataLayers
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if at least one actor was removed from at least one DataLayer.  false is returned if none of the actors belonged to any of the specified DataLayers.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveActorsFromDataLayers(const TArray<AActor*>& Actors, const TArray<UDataLayer*>& DataLayers);

	/////////////////////////////////////////////////
	// Operations on selected actors.

	/**
	 * Adds selected actors to the DataLayer.
	 *
	 * @param	DataLayer	A DataLayer.
	 * @return				true if at least one actor was added.  false is returned if all selected actors already belong to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool AddSelectedActorsToDataLayer(UDataLayer* DataLayer);

	/**
	 * Adds selected actors to the DataLayers.
	 *
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if at least one actor was added.  false is returned if all selected actors already belong to the DataLayers.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool AddSelectedActorsToDataLayers(const TArray<UDataLayer*>& DataLayers);

	/**
	 * Removes the selected actors from the DataLayer.
	 *
	 * @param	DataLayer	A DataLayer.
	 * @return				true if at least one actor was added.  false is returned if all selected actors already belong to the DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveSelectedActorsFromDataLayer(UDataLayer* DataLayer);

	/**
	 * Removes selected actors from the DataLayers.
	 *
	 * @param	DataLayers	A valid list of DataLayers.
	 * @return				true if at least one actor was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RemoveSelectedActorsFromDataLayers(const TArray<UDataLayer*>& DataLayers);


	/////////////////////////////////////////////////
	// Operations on actors in DataLayers
	
	/**
	 * Selects/de-selects actors belonging to the DataLayer.
	 *
	 * @param	DataLayer						A valid DataLayer.
	 * @param	bSelect							If true actors are selected; if false, actors are deselected.
	 * @param	bNotify							If true the Editor is notified of the selection change; if false, the Editor will not be notified.
	 * @param	bSelectEvenIfHidden	[optional]	If true even hidden actors will be selected; if false, hidden actors won't be selected.
	 * @return									true if at least one actor was selected/deselected.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SelectActorsInDataLayer(UDataLayer* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden = false);

	/**
	 * Selects/de-selects actors belonging to the DataLayer.
	 *
	 * @param	DataLayer						A valid DataLayer.
	 * @param	bSelect							If true actors are selected; if false, actors are deselected.
	 * @param	bNotify							If true the Editor is notified of the selection change; if false, the Editor will not be notified.
	 * @param	bSelectEvenIfHidden	[optional]	If true even hidden actors will be selected; if false, hidden actors won't be selected.
	 * @param	Filter	[optional]				Actor that don't pass the specified filter restrictions won't be selected.
	 * @return									true if at least one actor was selected/deselected.
	 */
	virtual bool SelectActorsInDataLayer(UDataLayer* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter);

	/**
	 * Selects/de-selects actors belonging to the DataLayers.
	 *
	 * @param	DataLayers						A valid list of DataLayers.
	 * @param	bSelect							If true actors are selected; if false, actors are deselected.
	 * @param	bNotify							If true the Editor is notified of the selection change; if false, the Editor will not be notified
	 * @param	bSelectEvenIfHidden	[optional]	If true even hidden actors will be selected; if false, hidden actors won't be selected.
	 * @return									true if at least one actor was selected/deselected.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SelectActorsInDataLayers(const TArray<UDataLayer*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden = false);

	/**
	 * Selects/de-selects actors belonging to the DataLayers.
	 *
	 * @param	DataLayers						A valid list of DataLayers.
	 * @param	bSelect							If true actors are selected; if false, actors are deselected.
	 * @param	bNotify							If true the Editor is notified of the selection change; if false, the Editor will not be notified
	 * @param	bSelectEvenIfHidden	[optional]	If true even hidden actors will be selected; if false, hidden actors won't be selected.
	 * @param	Filter	[optional]				Actor that don't pass the specified filter restrictions won't be selected.
	 * @return									true if at least one actor was selected/deselected.
	 */
	virtual bool SelectActorsInDataLayers(const TArray<UDataLayer*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter);


	/////////////////////////////////////////////////
	// Operations on actor viewport visibility regarding DataLayers

	/**
	 * Updates the visibility for all actors for all views.
	 *
	 * @param DataLayerThatChanged  If one DataLayer was changed (toggled in view pop-up, etc), then we only need to modify actors that use that DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void UpdateAllViewVisibility(UDataLayer* DataLayerThatChanged);

	/**
	 * Updates the per-view visibility for all actors for the given view
	 *
	 * @param ViewportClient				The viewport client to update visibility on
	 * @param DataLayerThatChanged [optional]	If one DataLayer was changed (toggled in view pop-up, etc), then we only need to modify actors that use that DataLayer
	 */
	virtual void UpdatePerViewVisibility(FLevelEditorViewportClient* ViewportClient, UDataLayer* DataLayerThatChanged = nullptr);

	/**
	 * Updates per-view visibility for the given actor in the given view
	 *
	 * @param ViewportClient				The viewport client to update visibility on
	 * @param Actor								Actor to update
	 * @param bReregisterIfDirty [optional]		If true, the actor will reregister itself to give the rendering thread updated information
	 */
	virtual void UpdateActorViewVisibility(FLevelEditorViewportClient* ViewportClient, AActor* Actor, const bool bReregisterIfDirty = true);

	/**
	 * Updates per-view visibility for the given actor for all views
	 *
	 * @param Actor		Actor to update
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void UpdateActorAllViewsVisibility(AActor* Actor);

	/**
	 * Removes the corresponding visibility bit from all actors (slides the later bits down 1)
	 *
	 * @param ViewportClient	The viewport client to update visibility on
	 */
	virtual void RemoveViewFromActorViewVisibility(FLevelEditorViewportClient* ViewportClient);

	/**
	 * Updates the provided actors visibility in the viewports
	 *
	 * @param	Actor						Actor to update
	 * @param	bOutSelectionChanged [OUT]	Whether the Editors selection changed
	 * @param	bOutActorModified [OUT]		Whether the actor was modified
	 * @param	bNotifySelectionChange		If true the Editor is notified of the selection change; if false, the Editor will not be notified
	 * @param	bRedrawViewports			If true the viewports will be redrawn; if false, they will not
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool UpdateActorVisibility(AActor* Actor, bool& bOutSelectionChanged, bool& bOutActorModified, const bool bNotifySelectionChange, const bool bRedrawViewports);

	/**
	 * Updates the visibility of all actors in the viewports
	 *
	 * @param	bNotifySelectionChange		If true the Editor is notified of the selection change; if false, the Editor will not be notified
	 * @param	bRedrawViewports			If true the viewports will be redrawn; if false, they will not
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool UpdateAllActorsVisibility(const bool bNotifySelectionChange, const bool bRedrawViewports);


	/////////////////////////////////////////////////
	// Operations on DataLayers

	/**
	 *	Appends all the actors associated with the specified DataLayer.
	 *
	 *	@param	DataLayer			The DataLayer to find actors for.
	 *	@param	InOutActors			The list to append the found actors to.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void AppendActorsFromDataLayer(UDataLayer* DataLayer, TArray<AActor*>& InOutActors) const;

	/**
	 *	Appends all the actors associated with the specified DataLayer.
	 *
	 *	@param	DataLayer			The DataLayer to find actors for.
	 *	@param	InOutActors			The list to append the found actors to.
	 *  @param	Filter	[optional]	Actor that don't pass the specified filter restrictions won't be selected.
	 */
	virtual void AppendActorsFromDataLayer(UDataLayer* DataLayer, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const;
	
	/**
	 *	Appends all the actors associated with ANY of the specified DataLayers.
	 *
	 *	@param	DataLayers			The DataLayers to find actors for.
	 *	@param	InOutActors			The list to append the found actors to.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void AppendActorsFromDataLayers(const TArray<UDataLayer*>& DataLayers, TArray<AActor*>& InOutActors) const;

	/**
	 *	Appends all the actors associated with ANY of the specified DataLayers.
	 *
	 *	@param	DataLayers			The DataLayers to find actors for.
	 *	@param	InOutActors			The list to append the found actors to.
	 *  @param	Filter	[optional]	Actor that don't pass the specified filter restrictions won't be selected.
	 */
	virtual void AppendActorsFromDataLayers(const TArray<UDataLayer*>& DataLayers, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const;

	/**
	 *	Gets all the actors associated with the specified DataLayer. Analog to AppendActorsFromDataLayer but it returns rather than appends the actors.
	 *
	 *	@param	DataLayer			The DataLayer to find actors for.
	 *	@return						The list to assign the found actors to.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual TArray<AActor*> GetActorsFromDataLayer(UDataLayer* DataLayer) const;

	/**
	 *	Gets all the actors associated with the specified DataLayer. Analog to AppendActorsFromDataLayer but it returns rather than appends the actors.
	 *
	 *	@param	DataLayer			The DataLayer to find actors for.
	 *  @param	Filter	[optional]	Actor that don't pass the specified filter restrictions won't be selected.
	 *	@return						The list to assign the found actors to.
	 */
	virtual TArray<AActor*> GetActorsFromDataLayer(UDataLayer* DataLayer, const TSharedPtr<FActorFilter>& Filter) const;

	/**
	 *	Gets all the actors associated with ANY of the specified DataLayers. Analog to AppendActorsFromDataLayers but it returns rather than appends the actors.
	 *
	 *	@param	DataLayers			The DataLayers to find actors for.
	 *	@return						The list to assign the found actors to.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual TArray<AActor*> GetActorsFromDataLayers(const TArray<UDataLayer*>& DataLayers) const;

	/**
	 *	Gets all the actors associated with ANY of the specified DataLayers. Analog to AppendActorsFromDataLayers but it returns rather than appends the actors.
	 *
	 *	@param	DataLayers			The DataLayers to find actors for.
	 *  @param	Filter	[optional]	Actor that don't pass the specified filter restrictions won't be selected.
	 *	@return						The list to assign the found actors to.
	 */
	virtual TArray<AActor*> GetActorsFromDataLayers(const TArray<UDataLayer*>& DataLayers, const TSharedPtr<FActorFilter>& Filter) const;

	/**
	 * Changes the DataLayer's visibility to the provided state
	 *
	 * @param	DataLayer	The DataLayer to affect.
	 * @param	bIsVisible	If true the DataLayer will be visible; if false, the DataLayer will not be visible.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void SetDataLayerVisibility(UDataLayer* DataLayer, const bool bIsVisible);

	/**
	 * Changes visibility of the DataLayers to the provided state
	 *
	 * @param	DataLayers	The DataLayers to affect
	 * @param	bIsVisible	If true the DataLayers will be visible; if false, the DataLayers will not be visible
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void SetDataLayersVisibility(const TArray<UDataLayer*>& DataLayers, const bool bIsVisible);

	/**
	 * Toggles the DataLayer's visibility
	 *
	 * @param DataLayer	The DataLayer to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void ToggleDataLayerVisibility(UDataLayer* DataLayer);

	/**
	 * Toggles the visibility of all of the DataLayers
	 *
	 * @param	DataLayers	The DataLayers to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void ToggleDataLayersVisibility(const TArray<UDataLayer*>& DataLayers);

	/**
	 * Changes the DataLayer's IsDynamicallyLoaded flag to the provided state
	 *
	 * @param	DataLayer				The DataLayer to affect.
	 * @param	bIsDynamicallyLoaded	If true the DataLayer will affect runtime streaming and actors will only be loaded if the DataLayer is active.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SetDataLayerIsDynamicallyLoaded(UDataLayer* DataLayer, const bool bIsDynamicallyLoaded);

	/**
	 * Changes the IsDynamicallyLoaded flag of the DataLayers to the provided state
	 *
	 * @param	DataLayers				The DataLayers to affect
	 * @param	bIsDynamicallyLoaded	If true the DataLayer will affect runtime streaming and actors will only be loaded if the DataLayer is active.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SetDataLayersIsDynamicallyLoaded(const TArray<UDataLayer*>& DataLayers, const bool bIsDynamicallyLoaded);

	/**
	 * Toggles the DataLayer's IsDynamicallyLoaded flag
	 *
	 * @param DataLayer	The DataLayer to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool ToggleDataLayerIsDynamicallyLoaded(UDataLayer* DataLayer);

	/**
	 * Toggles the IsDynamicallyLoaded flag of all of the DataLayers
	 *
	 * @param	DataLayers	The DataLayers to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool ToggleDataLayersIsDynamicallyLoaded(const TArray<UDataLayer*>& DataLayers);

	/**
	 * Changes the DataLayer's IsDynamicallyLoadedInEditor flag to the provided state
	 *
	 * @param	DataLayer						The DataLayer to affect.
	 * @param	bIsDynamicallyLoadedInEditor	The new value of the flag IsDynamicallyLoadedInEditor.
	 *											If the DataLayer is DynamicallyLoaded, the Editor loading will consider this DataLayer to load or not an Actor.
	 *											An Actor will not be loaded in the Editor if all its DataLayers are DynamicallyLoaded and not DynamicallyLoadedInEditor.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SetDataLayerIsDynamicallyLoadedInEditor(UDataLayer* DataLayer, const bool bIsDynamicallyLoadedInEditor);

	/**
	 * Changes the IsDynamicallyLoadedInEditor flag of the DataLayers to the provided state
	 *
	 * @param	DataLayers						The DataLayers to affect
	 * @param	bIsDynamicallyLoadedInEditor	The new value of the flag IsDynamicallyLoadedInEditor.
	 *											If the DataLayer is DynamicallyLoaded, the Editor loading will consider this DataLayer to load or not an Actor.
	 *											An Actor will not be loaded in the Editor if all its DataLayers are DynamicallyLoaded and not DynamicallyLoadedInEditor.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool SetDataLayersIsDynamicallyLoadedInEditor(const TArray<UDataLayer*>& DataLayers, const bool bIsDynamicallyLoadedInEditor);

	/**
	 * Toggles the DataLayer's IsDynamicallyLoadedInEditor flag
	 *
	 * @param DataLayer	The DataLayer to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool ToggleDataLayerIsDynamicallyLoadedInEditor(UDataLayer* DataLayer);

	/**
	 * Toggles the IsDynamicallyLoadedInEditor flag of all of the DataLayers
	 *
	 * @param	DataLayers	The DataLayers to affect
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool ToggleDataLayersIsDynamicallyLoadedInEditor(const TArray<UDataLayer*>& DataLayers);

	/**
	 * Set the visibility of all DataLayers to true
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void MakeAllDataLayersVisible();

	/**
	 * Gets the UDataLayer Object of the DataLayer label
	 *
	 * @param	DataLayerLabel	The label of the DataLayer whose UDataLayer Object is returned
	 * @return					The UDataLayer Object of the provided DataLayer label
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual UDataLayer* GetDataLayerFromLabel(const FName& DataLayerLabel) const;

	/**
	 * Gets the UDataLayer Object of the ActorDataLayer
	 *
	 * @param	ActorDataLayer	The ActorDataLayer whose UDataLayer Object is returned
	 * @return					The UDataLayer Object of the provided ActorDataLayer
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual UDataLayer* GetDataLayer(const FActorDataLayer& ActorDataLayer) const;

	/**
	 * Gets the UDataLayer Object of the DataLayer name
	 *
	 * @param	DataLayerName	The name of the DataLayer whose UDataLayer Object is returned
	 * @return					The UDataLayer Object of the provided DataLayer name
	 */
	virtual UDataLayer* GetDataLayerFromName(const FName& DataLayerName) const;
	
	/**
	 * Attempts to get the UDataLayer Object of the provided DataLayer label.
	 *
	 * @param	DataLayerLabel		The label of the DataLayer whose UDataLayer Object to retrieve
	 * @param	OutDataLayer[OUT] 	Set to the UDataLayer Object of the DataLayer label. Set to Invalid if no UDataLayer Object exists.
	 * @return					If true a valid UDataLayer Object was found and set to OutDataLayer; if false, a valid UDataLayer object was not found and invalid set to OutDataLayer
	 */
	virtual bool TryGetDataLayerFromLabel(const FName& DataLayerLabel, UDataLayer*& OutDataLayer);

	/**
	 * Gets all known DataLayers and appends them to the provided array
	 *
	 * @param OutDataLayers[OUT] Output array to store all known DataLayers
	 */
	virtual void AddAllDataLayersTo(TArray<TWeakObjectPtr<UDataLayer>>& OutDataLayers) const;

	/**
	 * Creates a UDataLayer Object
	 *
	 * @return	The newly created UDataLayer Object
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* CreateDataLayer();

	/**
	 * Deletes all of the provided DataLayers
	 *
	 * @param DataLayersToDelete	A valid list of DataLayer.
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void DeleteDataLayers(const TArray<UDataLayer*>& DataLayersToDelete);

	/**
	 * Deletes the provided DataLayer
	 *
	 * @param DataLayerToDelete		A valid DataLayer
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual void DeleteDataLayer(UDataLayer* DataLayerToDelete);

	/**
	 * Renames the provided DataLayer to the provided new label
	 *
	 * @param	DataLayer			The DataLayer to be renamed
	 * @param	NewDataLayerLabel	The new label for the DataLayer to be renamed
	 */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	virtual bool RenameDataLayer(UDataLayer* DataLayer, const FName& NewDataLayerLabel);

private:

	/**
	 *	Synchronizes an newly created Actor's DataLayers with the DataLayer system
	 *
	 *	@param	Actor	The actor to initialize
	 */
	void InitializeNewActorDataLayers(AActor* Actor);

	/**
	 * Find and return the selected actors.
	 *
	 * @return				The selected AActor's as a TArray.
	 */
	TArray<AActor*> GetSelectedActors() const;

	/**
	 * Gets the WorldDataLayers for this world
	 *
	 * @param	bCreateIfNotFound	If true, created the AWorldDataLayers object if not found.
	 * @return	The AWorldDataLayers Object
	 */
	class AWorldDataLayers* GetWorldDataLayers(bool bCreateIfNotFound = false);

	/**
	 * Gets the WorldDataLayers for this world
	 *
	 * @return	The AWorldDataLayers Object
	 */
	const class AWorldDataLayers* GetWorldDataLayers() const;

	/**
	 * Get the current UWorld object.
	 *
	 * @return						The UWorld* object
	 */
	UWorld* GetWorld() const; // Fallback to GWorld

	/** Delegate handler for FEditorDelegates::MapChange. It internally calls DataLayerChanged.Broadcast. */
	void EditorMapChange();

	/** Delegate handler for FEditorDelegates::RefreshDataLayerBrowser. It internally calls UpdateAllActorsVisibility to refresh the actors of each DataLayer. */
	void EditorRefreshDataLayerBrowser();

	/** Delegate handler for FEditorDelegates::PostUndoRedo. It internally calls DataLayerChanged.Broadcast and UpdateAllActorsVisibility to refresh the actors of each DataLayer. */
	void PostUndoRedo();

	bool SetDataLayerIsDynamicallyLoadedInternal(UDataLayer* DataLayer, const bool bIsDynamicallyLoaded);
	bool SetDataLayerIsDynamicallyLoadedInEditorInternal(UDataLayer* DataLayer, const bool bIsDynamicallyLoadedInEditor);
	bool RefreshWorldPartitionEditorCells();
	void UpdateDataLayerEditorPerProjectUserSettings();

	/** Fires whenever one or more DataLayer changes */
	FOnDataLayerChanged DataLayerChanged;

	/**	Fires whenever one or more actor DataLayer changes */
	FOnActorDataLayersChanged ActorDataLayersChanged;

	/** Auxiliary class that sets the callback functions for multiple delegates */
	TSharedPtr<class FDataLayersBroadcast> DataLayersBroadcast;

	friend class FDataLayersBroadcast;
};
