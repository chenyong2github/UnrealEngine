// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Change.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "InputRouter.h"
#include "ToolContextInterfaces.h"
#include "InteractiveGizmoManager.generated.h"



USTRUCT()
struct FActiveGizmo
{
	GENERATED_BODY();

	UInteractiveGizmo* Gizmo;
	FString BuilderIdentifier;
	FString InstanceIdentifier;
};


/**
 * UInteractiveGizmoManager allows users of the Tools framework to create and operate Gizmo instances.
 * For each Gizmo, a (string,GizmoBuilder) pair is registered with the GizmoManager.
 * Gizmos can then be activated via the string identifier.
 * 
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveGizmoManager : public UObject
{
	GENERATED_BODY()

protected:
	friend class UInteractiveToolsContext;		// to call Initialize/Shutdown

	UInteractiveGizmoManager();

	/** Initialize the GizmoManager with the necessary Context-level state. UInteractiveToolsContext calls this, you should not. */
	virtual void Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI, UInputRouter* InputRouter);

	/** Shutdown the GizmoManager. Called by UInteractiveToolsContext. */
	virtual void Shutdown();

public:

	//
	// GizmoBuilder Registration and Gizmo Creation/Shutdown
	//

	/**
	 * Register a new GizmoBuilder
	 * @param BuilderIdentifier string used to identify this Builder
	 * @param Builder new GizmoBuilder instance
	 */
	virtual void RegisterGizmoType(const FString& BuilderIdentifier, UInteractiveGizmoBuilder* Builder);

	/**
	 * Remove a GizmoBuilder from the set of known GizmoBuilders
	 * @param BuilderIdentifier identification string that was passed to RegisterGizmoType()
	 * @return true if Builder was found and deregistered
	 */
	virtual bool DeregisterGizmoType(const FString& BuilderIdentifier);



	/**
	 * Try to activate a new Gizmo instance on the given Side
	 * @param BuilderIdentifier string used to identify Builder that should be called
	 * @param InstanceIdentifier client-defined string that can be used to locate this instance
	 * @return new Gizmo instance that has been created and initialized
	 */	
	virtual UInteractiveGizmo* CreateGizmo(const FString& BuilderIdentifier, const FString& InstanceIdentifier );


	/**
	 * Shutdown and remove a Gizmo
	 * @param Gizmo the Gizmo to shutdown and remove
	 * @return true if the Gizmo was found and removed
	 */
	virtual bool DestroyGizmo(UInteractiveGizmo* Gizmo);

	/**
	 * Destroy all Gizmos that were created by the identified GizmoBuilder
	 * @param BuilderIdentifier the Builder string registered with RegisterGizmoType
	 */
	virtual void DestroyAllGizmosOfType(const FString& BuilderIdentifier);

	/**
	 * Find all the existing Gizmo instances that were created by the identified GizmoBuilder
	 * @param BuilderIdentifier the Builder string registered with RegisterGizmoType
	 * @return list of found Gizmos
	 */
	virtual TArray<UInteractiveGizmo*> FindAllGizmosOfType(const FString& BuilderIdentifier);

	/**
	 * Find the Gizmo that was created with the given instance identifier
	 * @param Identifier the InstanceIdentifier that was passed to CreateGizmo()
	 * @return the found Gizmo, or null
	 */
	virtual UInteractiveGizmo* FindGizmoByInstanceIdentifier(const FString& Identifier);




	//
	// Functions that Gizmos can call to interact with Transactions API
	//
	
	/** Post a message via the Transactions API */
	virtual void PostMessage(const TCHAR* Message, EToolMessageLevel Level);

	/** Post a message via the Transactions API */
	virtual void PostMessage(const FString& Message, EToolMessageLevel Level);

	/** Request an Invalidation via the Transactions API (ie to cause a repaint, etc) */
	virtual void PostInvalidation();

	/**
	 * Request that the Context open a Transaction, whatever that means to the current Context
	 * @param Description text description of this transaction (this is the string that appears on undo/redo in the UE Editor)
	 */
	virtual void BeginUndoTransaction(const FText& Description);

	/** Request that the Context close and commit the open Transaction */
	virtual void EndUndoTransaction();

	/**
	 * Forward an FChange object to the Context
	 * @param TargetObject the object that the FChange applies to
	 * @param Change the change object that the Context should insert into the transaction history
	 * @param Description text description of this change (this is the string that appears on undo/redo in the UE Editor)
	 */
	virtual void EmitObjectChange(UObject* TargetObject, TUniquePtr<FChange> Change, const FText& Description );


	//
	// State control  (@todo: have the Context call these? not safe for anyone to call)
	//

	/** Tick any active Gizmos. Called by UInteractiveToolsContext */
	virtual void Tick(float DeltaTime);

	/** Render any active Gizmos. Called by UInteractiveToolsContext. */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);



	//
	// access to APIs, etc
	//

	/** @return current IToolsContextQueriesAPI */
	virtual IToolsContextQueriesAPI* GetContextQueriesAPI() { return QueriesAPI; }


public:
	/** set of Currently-active Gizmos */
	UPROPERTY()
	TArray<FActiveGizmo> ActiveGizmos;


protected:
	/** Current Context-Queries implementation */
	IToolsContextQueriesAPI* QueriesAPI;
	/** Current Transactions implementation */
	IToolsContextTransactionsAPI* TransactionsAPI;

	/** Current InputRouter (Context owns this) */
	UInputRouter* InputRouter;

	/** Current set of named GizmoBuilders */
	UPROPERTY()
	TMap<FString, UInteractiveGizmoBuilder*> GizmoBuilders;
};