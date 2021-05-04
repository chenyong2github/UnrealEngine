// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "SubobjectData.h"
#include "ModuleDescriptor.h"
#include "SubobjectDataFactory.h"
#include "SubobjectDataSubsystem.generated.h"

class UActorComponent;
class FScopedTransaction;

/** Options when adding a new subobject */
USTRUCT(BlueprintType)
struct FAddNewSubobjectParams
{
	GENERATED_USTRUCT_BODY()

	FAddNewSubobjectParams()
		: ParentHandle(FSubobjectDataHandle::InvalidHandle)
		, NewClass(nullptr)
		, AssetOverride(nullptr)
		, BlueprintContext(nullptr)
		, bSkipMarkBlueprintModified(false)
		, bConformTransformToParent(true)
	{
	}
	
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	FSubobjectDataHandle ParentHandle;

	/** The class of the new subobject that will be added */
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	UClass* NewClass;

	/** Specific asset to use instead of the selected asset in the content browser */
	UObject* AssetOverride;

	/**
	 * Pointer to the blueprint context that this subobject is in. If this is null, it is assumed that
	 * this subobject is being added to an instance.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	UBlueprint* BlueprintContext;
	
	/** Optionally skip marking this blueprint as modified (e.g. if we're handling that externally */
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	uint8 bSkipMarkBlueprintModified : 1;

	/** Whether the newly created component should keep its transform, or conform it to its parent */
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	uint8 bConformTransformToParent : 1;
};

/** Options for reparenting subobjects */
USTRUCT(BlueprintType)
struct FReparentSubobjectParams
{
	GENERATED_USTRUCT_BODY()

	FReparentSubobjectParams() = default;

	/** The handle of the subobject to reparent to. */
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	FSubobjectDataHandle NewParentHandle;

	/**
	* Pointer to the blueprint context that this subobject is in. If this is null, it is assumed that
	* this subobject is being added to an instance.
	*/
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	UBlueprint* BlueprintContext = nullptr;

	/** 
	* The preview actor context to be used if in a blueprint context. 
	* This must have a value if BlueprintContext is needed. 
	*/
	UPROPERTY(BlueprintReadWrite, Category = "SubobjectDataSubsystem|AddNewSubobject")
	AActor* ActorPreviewContext = nullptr;
};

/**
* The Subobject Data Subsystem will produce the reflected subobject data
* based on a given root object. A root object can be anything, an actor 
* instance clicked on via the level editor, a UBlueprint* by opening an asset,
* or something piped in from python or other scripting languages. 
*/
UCLASS()
class SUBOBJECTDATAINTERFACE_API USubobjectDataSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:

	/** Implement this for initialization of instances of the system */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Implement this for deinitialization of instances of the system */
	virtual void Deinitialize() override;
	
	/**
	* Static wrapper for getting this engine subsystem. Will return nullptr
	* if the SubobjectDataInterface module has not been loaded yet.
	*/
	static USubobjectDataSubsystem* Get();
	
	/**
	* Gather all subobjects that the given UObject context has. Populates an array of 
	* handles that will have the given context and all it's subobjects.
	* 
	* @param Context		Object to gather subobjects for
	* @param OutArray		Array to populate (will be emptied first)
	*/
	void GatherSubobjectData(UObject* Context, TArray<FSubobjectDataHandle>& OutArray);

	/**
	* Gather all subobjects that the given Blueprint context has. Populates an array of
	* handles that will have the given context and all it's subobjects.
	*
	* @param Context		Object to gather subobjects for
	* @param OutArray		Array to populate (will be emptied first)
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem", meta = (DisplayName = "Gather Subobject Data For Blueprint"))
	void K2_GatherSubobjectDataForBlueprint(UBlueprint* Context, TArray<FSubobjectDataHandle>& OutArray);

	/**
	* Gather all subobjects that the given actor instance has. Populates an array of
	* handles that will have the given context and all it's subobjects.
	*
	* @param Context		Object to gather subobjects for
	* @param OutArray		Array to populate (will be emptied first)
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem", meta = (DisplayName = "Gather Subobject Data For Instance"))
	void K2_GatherSubobjectDataForInstance(AActor* Context, TArray<FSubobjectDataHandle>& OutArray);

	/**
	* Attempt to find the subobject data for a given handle. OutData will only 
	* be valid if the function returns true.
	*
	* @param Handle		Handle of the subobject data you want to aquire
	* @param OutData	Reference to the subobject data to populate
	*
	* @return bool		true if the data was found
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem", meta = (DisplayName = "FindSubobjectDataFromHandle"))
	bool K2_FindSubobjectDataFromHandle(const FSubobjectDataHandle& Handle, FSubobjectData& OutData) const;

	/**
	* Attempt to find an existing handle for the given object. 
	*
	* @param Context		The context that the object to find is within
	* @param ObjectToFind	The object that you want to find the handle for within the context
	*
	* @return FSubobjectDataHandle	The subobject handle for the object, Invalid handle if not found.
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	FSubobjectDataHandle FindHandleForObject(const FSubobjectDataHandle& Context, const UObject* ObjectToFind, UBlueprint* BPContext = nullptr) const;
	
	////////////////////////////////////////////
	// Modifying subobjects

	/**
	 * Creates a new C++ component from the specified class type
	 * The user will be prompted to pick a new subclass name and code will be recompiled
	 *
	 * @return The new class that was created
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	static UClass* CreateNewCPPComponent(TSubclassOf<UActorComponent> ComponentClass, const FString& NewClassPath, const FString& NewClassName);

	/**
	 * Creates a new Blueprint component from the specified class type
	 * The user will be prompted to pick a new subclass name and a blueprint asset will be created
	 *
	 * @return The new class that was created
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	static UClass* CreateNewBPComponent(TSubclassOf<UActorComponent> ComponentClassconst, const FString& NewClassPath, const FString& NewClassName);
	
	/**
	* Add a new subobject as a child to the given parent object 
	*
	* @param Params			Options to consider when adding this subobject
	*
	* @return FSubobjectDataHandle		Handle to the newly created subobject, Invalid handle if creation failed
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	FSubobjectDataHandle AddNewSubobject(const FAddNewSubobjectParams& Params, FText& FailReason);

	/**
	* Attempts to delete the given array of subobjects from their context 
	*
	* @param ContextHandle			The owning context of the subobjects that should be removed
	* @param SubobjectsToDelete		Array of subobject handles that should be deleted
	*
	* @return 	The number of subobjects successfully deleted
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	int32 DeleteSubobjects(const FSubobjectDataHandle& ContextHandle, const TArray<FSubobjectDataHandle>& SubobjectsToDelete, UBlueprint* BPContext = nullptr);

	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
    int32 DeleteSubobject(const FSubobjectDataHandle& ContextHandle, const FSubobjectDataHandle& SubobjectToDelete, UBlueprint* BPContext = nullptr);
	
	/**
	* Attempts to delete the given array of subobjects from their context 
	*
	* @param ContextHandle			The owning context of the subobjects that should be removed
	* @param SubobjectsToDelete		Array of subobject handles that should be deleted
	* @param OutComponentToSelect	Populates this handle with a valid selection in the component heirarchy if desiered
	* @return 	The number of subobjects successfully deleted
	*/
	int32 DeleteSubobjects(const FSubobjectDataHandle& ContextHandle, const TArray<FSubobjectDataHandle>& SubobjectsToDelete, FSubobjectDataHandle& OutComponentToSelect, UBlueprint* BPContext = nullptr);
	
	/**
	* Attempts to rename the given subobject to the new name.
	*
	* @param Handle			Handle to the subobject to rename
	* @param InNewName		The new name that is desired for the give subobject
	*
	* @return True if the rename was successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool RenameSubobject(const FSubobjectDataHandle& Handle, const FText& InNewName);

	/**
	* Attempts to reparent the given subobject to the new parent
	*
	* @param NewParentHandle		Handle of the new parent
	* @param ToReparentHandle		The handle of the subobject that will get moved
	*
	* @return True if the reparent was successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
    bool ReparentSubobject(const FReparentSubobjectParams& Params, const FSubobjectDataHandle& ToReparentHandle);

	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool MakeNewSceneRoot(const FSubobjectDataHandle& Context, const FSubobjectDataHandle& NewSceneRoot, UBlueprint* BPContext);
	
	/**
	* Attempts to reparent all subobjects in the HandlesToMove array to the new parent handle.
	* 
	* @param NewParentHandle		Handle of the new parent
	*/
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool ReparentSubobjects(const FReparentSubobjectParams& Params, const TArray<FSubobjectDataHandle>& HandlesToMove);

	/**
	 * Remove the child subobject from the owner
	 *
	 * @return True if the child was successfully removed. 
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool DetachSubobject(const FSubobjectDataHandle& OwnerHandle, const FSubobjectDataHandle& ChildToRemove);

	/**
	 * Addd the given subobject to a new owner. This will remove the subobject from its previous 
	 * owner if necessary.
	 *
	 * @param OwnerHandle		The new owner to attach to
	 * @param ChildToAddHandle	Handle to the subobject that will become a child of the owner
	 * 
	 * @return true if the child was added successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
    bool AttachSubobject(const FSubobjectDataHandle& OwnerHandle, const FSubobjectDataHandle& ChildToAddHandle);
	
	/**
	 * Returns true if the given new text is a valid option to rename the
	 * subobject with the given handle. Populates the OutErrorMessage if
	 * it is not valid.
	 *
	 * @param Handle			Handle to the subobject that is being checked
	 * @param InNewText			The new name that is desired
	 * @param OutErrorMessage	The reasoning for an invalid name
	 *
	 * @return True if the rename is valid
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool IsValidRename(const FSubobjectDataHandle& Handle, const FText& InNewText, FText& OutErrorMessage) const;

	/**
	 * Returns true if the given array of handles represents subobjects that
	 * can be copied.
	 */
	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool CanCopySubobjects(const TArray<FSubobjectDataHandle>& Handles) const;

	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	void CopySubobjects(const TArray<FSubobjectDataHandle>& Handles, UBlueprint* BpContext);

	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	bool CanPasteSubobjects(const FSubobjectDataHandle& RootHandle, UBlueprint* BPContext = nullptr) const;

	void PasteSubobjects(const FSubobjectDataHandle& PasteToContext, const TArray<FSubobjectDataHandle>& NewParentHandles, UBlueprint* BpContext, TArray<FSubobjectDataHandle>& OutPastedHandles);
	
	/**
	 * Duplicate the given array of subobjects on the context. 
	 *
	 * @param Context			The owning context that the subobjects to dup come from
	 * @param SubobjectsToDup	Array of handles of existing subobejcts you would like to have duplicated
	 * @param BpContext			Pointer to the current blueprint context if necessary. Use nullptr if dealing with instances
	 */
	void DuplicateSubobjects(const FSubobjectDataHandle& Context, const TArray<FSubobjectDataHandle>& SubobjectsToDup, UBlueprint* BpContext);
	
	FScopedTransaction* BeginTransaction(const TArray<FSubobjectDataHandle>& Handles, const FText& Description, UBlueprint* Blueprint);

	/**
	* Find the scene root for a given subobject handle.
	* If this is an actor, then it will return the handle pointing to its DefaultSceneRoot.
	* If this is a component, it will walk the parent chain until it finds the DefaultSceneRoot.
	*/
	FSubobjectDataHandle FindSceneRootForSubobject(const FSubobjectDataHandle& InHandle) const;

	UFUNCTION(BlueprintCallable, Category = "SubobjectDataSubsystem")
	static void RenameSubobjectMemberVariable(UBlueprint* BPContext, const FSubobjectDataHandle& InHandle, const FName NewName);
	
private:

	/**
	 * Find all child subobject data related to the given root context.
	 * Populates the OutVisited set with all data. 
	 */
	void FindAllSubobjectData(FSubobjectData* RootContext, TSet<FSubobjectData*>& OutVisited) const;
	
	/**
	 * Gets a ref to subobject data that has been initalized with the given context.
	 * It will have a unique ID, but may be reused from the FreeData array. 
	 */
	FSubobjectDataHandle CreateSubobjectData(UObject* Context, const FSubobjectDataHandle& ParentHandle = FSubobjectDataHandle::InvalidHandle, bool bIsInheritedSCS = false);

	/**
	 * Get a handle to the subobject with the given context with a specific parent.
	 * If the context is already a known child of the parent, then it will return an existing subobject handle. 
	 */
	FSubobjectDataHandle FactoryCreateSubobjectDataWithParent(UObject* Context, const FSubobjectDataHandle& ParentHandle, bool bIsInheritedSCS = false);

	/**
	 * Create a subobject data and handle for an inherited blueprint context.
	 * This will populate the given array of handles recursively with all possible children
	 * (which are USCS_Node* in this case)
	 */
	FSubobjectDataHandle FactoryCreateInheritedBpSubobject(UObject* Context, const FSubobjectDataHandle& ParentHandle, bool bIsInherited, TArray<FSubobjectDataHandle>& OutArray);
	
	/** Find the parent that this new subobject should attach to */
	FSubobjectDataHandle FindParentForNewSubobject(const UObject* NewSubobject, const FSubobjectDataHandle& SelectedParent);
	
	FSubobjectFactoryManager* FactoryManager = nullptr;
	
public:
	FSubobjectFactoryManager* GetSubobjectFactoryManager() const { return FactoryManager; }
	
};