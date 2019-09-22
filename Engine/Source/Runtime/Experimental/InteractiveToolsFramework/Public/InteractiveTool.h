// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehaviorSet.h"
#include "InteractiveToolActionSet.h"
#include "ToolContextInterfaces.h"
#include "InteractiveTool.generated.h"

class UInteractiveToolManager;


/** Passed to UInteractiveTool::Shutdown to indicate how Tool should shut itself down*/
enum class EToolShutdownType
{
	/** Tool cleans up and exits. Pass this to tools that do not have Accept/Cancel options. */
	Completed = 0,
	/** Tool commits current preview to scene */
	Accept = 1,
	/** Tool discards current preview without modifying scene */
	Cancel = 2
};


/** This delegate is used by UInteractiveToolPropertySet */
DECLARE_MULTICAST_DELEGATE_TwoParams(FInteractiveToolPropertySetModifiedSignature, UObject*, UProperty*);


/**
 * A UInteractiveTool contains a set of UObjects that contain "properties" of the Tool, ie
 * the configuration flags, parameters, etc that control the Tool. Currently any UObject
 * can be added as a property set, however there is no automatic mechanism for those child 
 * UObjects to notify the Tool when a property changes.
 * 
 * If you make your property set UObjects subclasses of UInteractiveToolPropertySet, then
 * when the Tool Properties are changed *in the Editor*, the parent Tool will be automatically notified.
 * You can override UInteractiveTool::OnPropertyModified() to act on these notifications
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolPropertySet : public UObject
{
	GENERATED_BODY()

protected:
	FInteractiveToolPropertySetModifiedSignature OnModified;

public:

	/** @return the multicast delegate that is called when properties are modified */
	FInteractiveToolPropertySetModifiedSignature& GetOnModified()
	{
		return OnModified;
	}

	/** 
	  * Posts a message to the OnModified delegate with the modified UProperty 
	  * @warning this function is currently only called in Editor (not at runtime)
	  */
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		OnModified.Broadcast(this, PropertyChangedEvent.Property);
	}



	//
	// Setting saving/serialization
	//

public:
	/**
	 * Save values of current Tool Properties. Implementing these functions is *optional*
	 * and how it is implemented is up to the PropertySet implementation.
	 * It is not necessary to save/restore all possible Properties (in many cases this would not make sense).
	 * GetPropertyCache() can be used to return an instance of subclasses that is an easy
	 * place to save/restore these properties
	 */
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) {}

	/**
	 * Restore saved property values
	 */
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) {}

protected:
	/**
	 * GetPropertyCache returns a class-internal object that subclasses can use to save/restore properties.
	 * If the subclass is UMyPropertySet, this function should only ever be called as GetPropertyCache<UMyPropertySet>().
	 */
	template<typename ObjType>
	ObjType* GetPropertyCache()
	{
		ObjType* CDO = GetMutableDefault<ObjType>();
		if (CDO->CachedProperties == nullptr)
		{
			CDO->CachedProperties = NewObject<ObjType>();
		}
		return CastChecked<ObjType>(CDO->CachedProperties);
	}

private:
	// CachedProperties should only ever be set to an instance of the subclass, ideally via GetPropertyCache().
	UPROPERTY()
	UObject* CachedProperties = nullptr;

};


/**
 * UInteractiveTool is the base class for all Tools in the InteractiveToolsFramework.
 * A Tool is is a "lightweight mode" that may "own" one or more Actors/Components/etc in
 * the current scene, may capture certain input devices or event streams, and so on.
 * The base implementation essentially does nothing but provide sane default behaviors.
 *
 * The BaseTools/ subfolder contains implementations of various kinds of standard
 * "tool behavior", like a tool that responds to a mouse click, etc, that can be
 * extended to implement custom behaviors.
 *
 * In the framework, you do not create instances of UInteractiveTool yourself. 
 * You provide a UInteractiveToolBuilder implementation that can properly construct
 * an instance of your Tool, this is where for example default parameters would be set.
 * The ToolBuilder is registered with the ToolManager, and then UInteractiveToolManager::ActivateTool()
 * is used to kick things off.
 *
 * @todo callback/delegate for if/when .InputBehaviors changes
 * @todo callback/delegate for when tool properties change
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveTool : public UObject, public IInputBehaviorSource
{
	GENERATED_BODY()

public:
	UInteractiveTool();

	/**
	 * Called by ToolManager to initialize the Tool *after* ToolBuilder::BuildTool() has been called
	 */
	virtual void Setup();

	/**
	 * Called by ToolManager to shut down the Tool
	 * @param ShutdownType indicates how the tool should shutdown (ie Accept or Cancel current preview, etc)
	 */
	virtual void Shutdown(EToolShutdownType ShutdownType);

	/**
	 * Allow the Tool to do any custom drawing (ie via PDI/RHI)
	 * @param RenderAPI Abstraction that provides access to Rendering in the current ToolsContext
	 */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);

	/**
	 * Allow the Tool to do any necessary processing on Tick 
	 * @param DeltaTime the time delta since last tick
	 */
	virtual void Tick(float DeltaTime);



	/**
	 * @return ToolManager that owns this Tool
	 */
	virtual UInteractiveToolManager* GetToolManager() const;


	/** 
	 * @return true if this Tool support being Cancelled, ie calling Shutdown(EToolShutdownType::Cancel)  
	 */
	virtual bool HasCancel() const;

	/** 
	 * @return true if this Tool support being Accepted, ie calling Shutdown(EToolShutdownType::Accept)  
	 */
	virtual bool HasAccept() const;

	/** 
	 * @return true if this Tool is currently in a state where it can be Accepted. This may be false if for example there was an error in the Tool. 
	 */
	virtual bool CanAccept() const;



	//
	// Input Behaviors support
	//

	/**
	 * Add an input behavior for this Tool
	 * @param Behavior behavior to add
	 */
	virtual void AddInputBehavior(UInputBehavior* Behavior);

	/**
	 * @return Current input behavior set.
	 */
	virtual const UInputBehaviorSet* GetInputBehaviors() const;


	//
	// Property support
	//

	/**
	 * @return list of property UObjects for this tool (ie to add to a DetailsViewPanel, for example)
	 */
	virtual const TArray<UObject*>& GetToolProperties() const;


	/**
	 * Automatically called by UInteractiveToolPropertySet.OnModified delegate to notify Tool of child property set changes
	 * @param PropertySet which UInteractiveToolPropertySet was modified
	 * @param Property which UProperty in the set was modified
	 */
	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property)
	{
	}


protected:

	/** The current set of InputBehaviors provided by this Tool */
	UPROPERTY()
	UInputBehaviorSet* InputBehaviors;

	/** The current set of Property UObjects provided by this Tool. May contain pointer to itself. */
	UPROPERTY()
	TArray<UObject*> ToolPropertyObjects;

	/**
	 * Add a Property object for this Tool
	 * @param Property object to add
	 */
	virtual void AddToolPropertySource(UObject* PropertyObject);

	/**
	 * Add a PropertySet object for this Tool
	 * @param PropertySet Property Set object to add
	 */
	virtual void AddToolPropertySource(UInteractiveToolPropertySet* PropertySet);




	//
	// Action support/system
	// 
	// Your Tool subclass can register a set of "Actions" it can execute
	// by overloading RegisterActions(). Then external systems can use GetActionSet() to
	// find out what Actions your Tool supports, and ExecuteAction() to run those actions.
	//
	
public:
	/**
	 * Get the internal Action Set for this Tool. The action set is created and registered on-demand.
	 * @return pointer to initialized Action set
	 */
	virtual FInteractiveToolActionSet* GetActionSet();

	/**
	 * Request that the Action identified by ActionID be executed.
	 * Default implementation forwards these requests to internal ToolActionSet.
	 */
	virtual void ExecuteAction(int32 ActionID);


protected:
	/**
	 * Override this function to register the set of Actions this Tool supports, using FInteractiveToolActionSet::RegisterAction.
	 * Note that 
	 */
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet);


private:
	/** 
	 * Set of actions this Tool can execute. This variable is allocated on-demand. 
	 * Use GetActionSet() instead of accessing this pointer directly!
	 */
	FInteractiveToolActionSet* ToolActionSet = nullptr;

};


