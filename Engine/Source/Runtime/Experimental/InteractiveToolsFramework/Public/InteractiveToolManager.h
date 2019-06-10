// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Change.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "InputRouter.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.generated.h"



/** A Tool can be activated on a particular input device, currently identified by a "side" */
UENUM()
enum class EToolSide
{
	/** Left-hand Tool, also used for Mouse */
	Left = 1,
	Mouse = 1,
	/** Right-hand Tool*/
	Right = 2,
};


/**
 * UInteractiveToolManager allows users of the tools framework to create and operate Tool instances.
 * For each Tool, a (string,ToolBuilder) pair is registered with the ToolManager.
 * Tools can then be activated via the string identifier.
 * 
 * Currently a single Tool can be active for each input device. So for mouse input a single
 * Tool is available and effectively a lightweight mode. The mouse uses the "Left" tool slot.
 * 
 * For VR controllers and touch input, a "Left" and "Right" tool can be active at the same time.  
 * @todo this is not fully supported yet
 * 
 * Tools are not directly created. Use SelectActiveToolType(side,string) to set the active ToolBuilder
 * on a given side, and then use ActivateTool() to create the new Tool instance.
 *
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolManager : public UObject
{
	GENERATED_BODY()

protected:
	friend class UInteractiveToolsContext;		// to call Initialize/Shutdown

	UInteractiveToolManager();

	/** Initialize the ToolManager with the necessary Context-level state. UInteractiveToolsContext calls this, you should not. */
	virtual void Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI, UInputRouter* InputRouter);

	/** Shutdown the ToolManager. Called by UInteractiveToolsContext. */
	virtual void Shutdown();

public:

	//
	// Tool registration and Current Tool state
	//

	/**
	 * Register a new ToolBuilder
	 * @param Identifier string used to identify this Builder
	 * @param Builder new ToolBuilder instance
	 */
	virtual void RegisterToolType(const FString& Identifier, UInteractiveToolBuilder* Builder);

	/**
	 * Set active ToolBuilder for a ToolSide via string identifier
	 * @param Side which "side" should we set this Builder on
	 * @param Identifier name of ToolBuilder that was passed to RegisterToolType()
	 */
	virtual bool SelectActiveToolType(EToolSide Side, const FString& Identifier);

	/**
	 * Check if a named Tool type can currently be activated on the given ToolSide
	 * @param Side which "side" you would like to active the tool on
	 * @param Identifier string name of the Tool type
	 * @return true if the Tool type could be activated
	 */
	virtual bool CanActivateTool(EToolSide eSide, const FString& Identifier);

	/**
	 * Try to activate a new Tool instance on the given Side
	 * @param Side which "side" you would like to active the tool on
	 * @return true if a new Tool instance was created and initialized
	 */	
	virtual bool ActivateTool(EToolSide Side);

	/**
	 * Check if there is an active Tool on the given Side
	 * @param Side which Side to check
	 * @return true if there is an active Tool on that side
	 */
	virtual bool HasActiveTool(EToolSide Side) const;


	/**
	 * @return true if there are any active tools
	 */
	virtual bool HasAnyActiveTool() const;


	/**
	 * Get pointer to active Tool on a given side
	 * @param Side which Side is being requested
	 * @return pointer to Tool instance active on that Side, or nullptr if no such Tool exists
	 */
	virtual UInteractiveTool* GetActiveTool(EToolSide Side);


	/**
	 * Check if an active Tool on the given Side can be Accepted in its current state
	 * @param Side which Side to check
	 * @return true if there is an active Tool and it returns true from HasAccept() and CanAccept()
	 */
	virtual bool CanAcceptActiveTool(EToolSide Side);

	/**
	 * Check if an active Tool on the given Side can be Canceled
	 * @param Side which Side to check
	 * @return true if there is an active Tool and it returns true from HasCancel()
	 */
	virtual bool CanCancelActiveTool(EToolSide Side);

	/**
	 * Shut down an active Tool on the given side
	 * @param Side which "side" you would like to shut down
	 * @param ShutdownType how should the tool be terminated (eg Accept/Cancel)
	 */
	virtual void DeactivateTool(EToolSide Side, EToolShutdownType ShutdownType);



	//
	// Functions that Tools can call to interact with Transactions API
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


	/**
	 * Forward an FChange object to the Context
	 */
	virtual bool RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange);



	//
	// State control  (@todo: have the Context call these? not safe for anyone to call)
	//

	/** Tick any active Tools. Called by UInteractiveToolsContext */
	virtual void Tick(float DeltaTime);

	/** Render any active Tools. Called by UInteractiveToolsContext. */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);


	//
	// access to APIs, etc
	//
	
	/** @return current IToolsContextQueriesAPI */
	virtual IToolsContextQueriesAPI* GetContextQueriesAPI() { return QueriesAPI; }



public:
	/** Currently-active Left Tool, or null if no Tool is active */
	UPROPERTY()
	UInteractiveTool* ActiveLeftTool;

	/** Currently-active Right Tool, or null if no Tool is active */
	UPROPERTY()
	UInteractiveTool* ActiveRightTool;




public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FToolManagerToolStartedSignature, UInteractiveToolManager*, UInteractiveTool*);
	FToolManagerToolStartedSignature OnToolStarted;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FToolManagerToolEndedSignature, UInteractiveToolManager*, UInteractiveTool*);
	FToolManagerToolEndedSignature OnToolEnded;


protected:
	/** Pointer to current Context-Queries implementation */
	IToolsContextQueriesAPI* QueriesAPI;
	/** Pointer to current Transactions implementation */
	IToolsContextTransactionsAPI* TransactionsAPI;

	/** Pointer to current InputRouter (Context owns this) */
	UInputRouter* InputRouter;

	/** Current set of named ToolBuilders */
	UPROPERTY()
	TMap<FString, UInteractiveToolBuilder*> ToolBuilders;

	/** Currently-active Left ToolBuilder */
	UInteractiveToolBuilder* ActiveLeftBuilder;
	/** Currently-active Right ToolBuilder */
	UInteractiveToolBuilder* ActiveRightBuilder;

};