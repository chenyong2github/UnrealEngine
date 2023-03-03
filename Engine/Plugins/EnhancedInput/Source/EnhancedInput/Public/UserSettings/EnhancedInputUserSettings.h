// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/SaveGame.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"					// For FKey
#include "GameFramework/InputSettings.h"	// For FHardwareDeviceIdentifier
#include "EnhancedActionKeyMapping.h"
#include "GameplayTagContainer.h"

#include "EnhancedInputUserSettings.generated.h"

class UInputMappingContext;
class UEnhancedPlayerInput;
class ULocalPlayer;

/**
 * The "Slot" that a player mappable key is in.
 * Used by UI to allow for multiple keys to be bound by the player for a single action
 * 
 * | <Action Name>  | Slot 1 | Slot 2 | Slot 3 | Slot.... N |
 */
UENUM(BlueprintType)
enum class EPlayerMappableKeySlot : uint8 
{
	// The first key slot
	First = 0,

	// The second mappable key slot. This is the default max in the project settings
	Second,
	
	Third,
	Fourth,
	Fifth,
	Sixth,
	Seventh,
	
	// A key that isn't in any slot
	Unspecified,
	Max
};

/** Arguments that can be used when mapping a player key */
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FMapPlayerKeyArgs
{
	GENERATED_BODY()

	FMapPlayerKeyArgs();	
	
	/**
	 * The name of the action for this key. This is either the default mapping name from an Input Action asset, or one
	 * that is overriden in the Input Mapping Context.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	FName ActionName;

	/** What slot this key mapping is for */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	EPlayerMappableKeySlot Slot;

	/** The new Key that this action should be mapped to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	FKey NewKey;
	
	/** An OPTIONAL specifier about what kind of hardware this mapping is for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings", meta = (EditCondition = "bFilterByHardwareDeviceId == true", AllowPrivateAccess = true, GetOptions = "Engine.InputPlatformSettings.GetAllHardwareDeviceNames"))
	FName HardwareDeviceId;

	/** The Key Mapping Profile identifier that this mapping should be set on. If this is empty, then the currently equipped profile will be used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	FGameplayTag ProfileId;

	/** If there is not a player mapping already with the same Slot and Hardware Device ID, then create a new mapping for this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	uint8 bCreateMatchingSlotIfNeeded : 1;
};

/** Represents a single key mapping that is set by the player */
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FPlayerKeyMapping
{
	GENERATED_BODY()
	friend class UEnhancedInputUserSettings;
	
public:
	
	FPlayerKeyMapping();
	
	FPlayerKeyMapping(const FEnhancedActionKeyMapping& OriginalMapping, EPlayerMappableKeySlot InSlot = EPlayerMappableKeySlot::Unspecified);

	/** A static invalid player key mapping to be used for easy comparisons in blueprint */
	static FPlayerKeyMapping InvalidMapping;

	/** Returns true if this mapping has been customized by the player, and false if it has not been. */
	bool IsCustomized() const;

	/** Returns true if this player mapping is valid */
	bool IsValid() const;

	/**
	 * Returns the key that the player has mapped. If the player has not mapped one yet, then this returns the
	 * default key mapping from the input mapping context.
	 */
	const FKey& GetCurrentKey() const;

	/** Returns the default key that this mapping is to */
	const FKey& GetDefaultKey() const;

	/** Print out some debug information about this player key mappings */
	FString ToString() const;

	/**
	 * The unique FName associated with this action. This is defined by this mappings owning Input Action
	 * or the individual Enhanced Action Key Mapping if it is overriden
	 */
	const FName GetActionName() const;

	/** The localized display name to use for this mapping */
	const FText& GetDisplayName() const;

	/** Returns what player mappable slot this mapping is in */
	EPlayerMappableKeySlot GetSlot() const;

	/** Returns the optional hardware device ID that this mapping is specific to */
	const FHardwareDeviceIdentifier& GetHardwareDeviceId() const; 

	/** Resets the current mapping to the default one */
	void ResetToDefault();

	/** Sets the value of the current key to the one given */
	void SetCurrentKey(const FKey& NewKey);

	/**
	 * Updates the metadata properties on this player mapped key based on the given
	 * enhanced action mapping. This will populate the fields on this struct that are not editable
	 * by the player such as the localized display name and default key.  
	 */
	void UpdateOriginalKey(const FEnhancedActionKeyMapping& OriginalMapping);
	
	ENHANCEDINPUT_API friend uint32 GetTypeHash(const FPlayerKeyMapping& InMapping);
	bool operator==(const FPlayerKeyMapping& Other) const;
	bool operator!=(const FPlayerKeyMapping& Other) const;

	/** Returns true if this mapping has been modified since it was registered from an IMC */
	const bool IsDirty() const;

protected:
	
	/** The name of the action for this key */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	FName ActionName;
	
	/** Localized display name of this action */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	FText DisplayName;

	/** What slot this key is mapped to */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	EPlayerMappableKeySlot Slot;
	
	/** True if this key mapping is dirty (i.e. has been changed by the player) */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	uint8 bIsDirty : 1;
	
	/** The default key that this mapping was set to in its input mapping context */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings", meta = (AllowPrivateAccess = "True"))
	FKey DefaultKey;
	
	/** The key that the player has mapped this action to */
	UPROPERTY(VisibleAnywhere,  BlueprintReadOnly, Category="Enhanced Input|User Settings", meta = (AllowPrivateAccess = "True"))
	FKey CurrentKey;

	/** An optional Hardware Device specifier for this mapping */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	FHardwareDeviceIdentifier HardwareDeviceId;
};

/**
 * Stores all mappings bound to a single action.
 *
 * Since a single action can have multiple bindings to it and this system should be Blueprint friendly,
 * this needs to be a struct (blueprint don't support nested containers).
 */
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FKeyMappingRow
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enhanced Input|User Settings")
	TSet<FPlayerKeyMapping> Mappings;

	/** Returns true if this row has any mappings in it */
	bool HasAnyMappings() const;
};


/** Represents one "Profile" that a user can have for their player mappable keys */
UCLASS(BlueprintType)
class ENHANCEDINPUT_API UEnhancedPlayerMappableKeyProfile : public UObject
{
	GENERATED_BODY()

	friend class UEnhancedInputUserSettings;

public:

	//~ Begin UObject Interface
    /**
     * Because the key mapping profile is serialized as a subobject of the UEnhancedInputUserSettings and requires
     * some custom serialization logic, you should not override the Serialize method on your custom key profile.
     * If you need to add custom serialization logic then you can create a struct UPROPERTY and override the struct's
     * serialization logic, which will prevent you from running into possible issues with subobjects.
     */
    virtual void Serialize(FArchive& Ar) override final;
    //~ End UObject Interface

	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void SetDisplayName(const FText& NewDisplayName);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	const FGameplayTag& GetProfileIdentifer() const;

	/** Get the localized display name for this profile */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	const FText& GetProfileDisplayName() const;

	/**
	 * Get all known key mappings for this profile.
	 *
	 * This returns a map of "Action Name" -> Mappings to that action
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	const TMap<FName, FKeyMappingRow>& GetPlayerMappedActions() const;

	/** Resets every player key mapping to this action back to it's default value */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	void ResetActionMappingsToDefault(const FName InActionName);
	
	/** Get all the key mappings associated with the given action name on this profile */
	FKeyMappingRow* FindKeyMappingRowMutable(const FName InActionName);

	/** Get all the key mappings associated with the given action name on this profile */
	const FKeyMappingRow* FindKeyMappingRow(const FName InActionName) const;

	/** A helper function to print out all the current profile settings to the log. */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void DumpProfileToLog() const;

	/** Returns a string that can be used to debug the current key mappings.  */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual FString ToString() const;
	
	/**
	 * Returns all FKey's bound to the given Action Name on this profile.
	 *
	 * Returns the number of keys mapped to this action
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta = (ReturnDisplayName = "Number of keys"))
	virtual int32 GetKeysMappedToAction(const FName ActionName, /*OUT*/ TArray<FKey>& OutKeys) const;

	/**
	 * Populates the OutMappedActionNames with every action on this profile that has a mapping to the given key.
	 *
	 * Returns the number of actions mapped to this key
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta = (ReturnDisplayName = "Number of actions"))
	virtual int32 GetActionsMappedToKey(const FKey& InKey, /*OUT*/ TArray<FName>& OutMappedActionNames) const;

	/** Returns a pointer to the player key mapping that fits with the given arguments. Returns null if none exist. */
	virtual FPlayerKeyMapping* FindKeyMapping(const FMapPlayerKeyArgs& InArgs) const;
	
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta=(DisplayName="Find Key Mapping", AutoCreateRefTerm="OutKeyMapping"))
	void K2_FindKeyMapping(FPlayerKeyMapping& OutKeyMapping, const FMapPlayerKeyArgs& InArgs) const;

	/**
	 * Resets all the key mappings in this profile to their default value from their Input Mapping Context.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void ResetToDefault();
	
protected:

	/**
	 * Equips the current key profile. This will always be called after the previous key profile's UnEquip function.
	*  Make any changes to the given Enhanced Player input object that may be necessary for
	 * your custom profile.
	 * 
	 * This function will only ever be called by UEnhancedInputUserSettings::SetKeyProfile.
	 */
	virtual void EquipProfile();

	/**
	 * UnEquips the current profile. Make any changes to the given Enhanced Player input object that may be necessary for
	 * your custom profile.
	 * 
	 * This function will only ever be called by UEnhancedInputUserSettings::SetKeyProfile
	 */
	virtual void UnEquipProfile();
	
	/** The ID of this profile. This can be used by each Key Mapping to filter down which profile is required for it be equipped. */
	UPROPERTY(BlueprintReadOnly, SaveGame, EditAnywhere, Category="Enhanced Input|User Settings")
	FGameplayTag ProfileIdentifier;
	
	/** The localized display name of this profile */
	UPROPERTY(BlueprintReadWrite, SaveGame, EditAnywhere, Category="Enhanced Input|User Settings")
	FText DisplayName;
	
	/**
	 * A map of "Action Name" to all key mappings associated with it.
	 * Note: Dirty mappings will be serialized from UEnhancedInputUserSettings::Serialize
	 */
	UPROPERTY(BlueprintReadOnly, Transient, EditAnywhere, Category="Enhanced Input|User Settings")
	TMap<FName, FKeyMappingRow> PlayerMappedKeys;
};

/** Arguments that can be used when creating a new mapping profile */
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FPlayerMappableKeyProfileCreationArgs
{
	GENERATED_BODY()
	
	FPlayerMappableKeyProfileCreationArgs();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Enhanced Input|User Settings")
	TSubclassOf<UEnhancedPlayerMappableKeyProfile> ProfileType;
	
	/** The uniqiue identifier that this profile should have */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Enhanced Input|User Settings")
	FGameplayTag ProfileIdentifier;
	
	/** The display name of this profile */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Enhanced Input|User Settings")
	FText DisplayName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Enhanced Input|User Settings")
	uint8 bSetAsCurrentProfile : 1;
};

/**
 * The Enhanced Input User Settings class is a place where you can put all of your Input Related settings
 * that you want your user to be able to change. Things like their key mappings, aim sensativity, accessibility
 * settings, etc. This also provies a Registration point for Input Mappings Contexts (IMC) from possibly unloaded
 * plugins (i.e. Game Feature Plugins). You can register your IMC from a Game Feature Action plugin here, and then
 * have access to all the key mappings available. This is very useful for building settings screens because you can
 * now access all the mappings in your game, even if the entire plugin isn't loaded yet. 
 *
 * The user settings are stored on each UEnhancedPlayerInput object, so each instance of the settings can represent
 * a single User or Local Player.
 *
 * To customize this for your game, you can create a subclass of it and change the "UserSettingsClass" in the
 * Enhanced Input Project Settings.
 */
UCLASS(config=GameUserSettings, DisplayName="Enhanced Input User Settings (Experimental)", Category="Enhanced Input|User Settings")
class ENHANCEDINPUT_API UEnhancedInputUserSettings : public USaveGame
{
	GENERATED_BODY()

public:

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface
	
	/** Loads or creates new user settings for the owning local player of the given player input */
	static UEnhancedInputUserSettings* LoadOrCreateSettings(UEnhancedPlayerInput* PlayerInput);
	virtual void Initialize(UEnhancedPlayerInput* PlayerInput);

	/**
	 * Apply any custom input settings to your user. By default, this will just broadcast the OnSettingsApplied delegate
	 * which is a useful hook to maybe rebuild some UI or do other user facing updates.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void ApplySettings();

	/**
	 * Synchronously save the settings to a hardcoded save game slot. This will work for simple games,
	 * but if you need to integrate it into an advanced save system you should Serialize this object out with the rest of your save data.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void SaveSettings();

	/**
	 * Asynchronously save the settings to a hardcoded save game slot. This will work for simple games,
	 * but if you need to integrate it into an advanced save system you should Serialize this object out with the rest of your save data.
	 *
	 * OnAsyncSaveComplete will be called upon save completion.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual void AsyncSaveSettings();

protected:

	virtual void OnAsyncSaveComplete(const FString& SlotName, const int32 UserIndex, bool bSuccess);

public:
	
	UEnhancedPlayerInput* GetPlayerInput() const;
	ULocalPlayer* GetLocalPlayer() const;
	APlayerController* GetPlayerController() const;
	
	/** Fired when the user settings have changed, such as their key mappings. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnhancedInputUserSettingsChanged, UEnhancedInputUserSettings*, Settings);
	UPROPERTY(BlueprintAssignable, Category = "Enhanced Input|User Settings")
	FEnhancedInputUserSettingsChanged OnSettingsChanged;
	
	/** Called after the settings have been applied from the ApplySettings call. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEnhancedInputUserSettingsApplied);
	UPROPERTY(BlueprintAssignable, Category = "Enhanced Input|User Settings")
	FEnhancedInputUserSettingsApplied OnSettingsApplied;

	// Remappable keys API

	/**
	 * Sets the player mapped key for this action on the current key profile.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta = (AutoCreateRefTerm = "FailureReason"))
	virtual void MapPlayerKey(const FMapPlayerKeyArgs& InArgs, FGameplayTagContainer& FailureReason);

	/** Unmap what is currently mapped to the given action in the given slot */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta = (AutoCreateRefTerm = "FailureReason"))
	virtual void UnMapPlayerKey(const FMapPlayerKeyArgs& InArgs, FGameplayTagContainer& FailureReason);

	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	virtual const TSet<FPlayerKeyMapping>& FindMappingsForAction(const FName ActionName) const;

	/** Returns the current player key mapping for the given action in the given slot */
	virtual const FPlayerKeyMapping* FindCurrentMappingForSlot(const FName ActionName, const EPlayerMappableKeySlot InSlot) const;
	
	// Modifying key profile

	/** Fired when you equip a different key profile  */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMappableKeyProfileChanged, const UEnhancedPlayerMappableKeyProfile*, NewProfile);
	FMappableKeyProfileChanged OnKeyProfileChanged;
	
	/**
	 * Changes the currently active key profile to the one with the given name. Returns true if the operation was successful.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta=(ReturnDisplayName = "Was Successful"))
	virtual bool SetKeyProfile(const FGameplayTag& InProfileId);
	
	/** Gets the currently selected key profile */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	virtual const FGameplayTag& GetCurrentKeyProfileIdentifier() const;

	/** Get the current key profile that the user has set */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	UEnhancedPlayerMappableKeyProfile* GetCurrentKeyProfile() const;

	template<class T>
	T* GetCurrentKeyProfile() const
	{
		return Cast<T>(GetCurrentKeyProfile());
	}

	/** Returns all player saved key profiles */
	const TMap<FGameplayTag, TObjectPtr<UEnhancedPlayerMappableKeyProfile>>& GetAllSavedKeyProfiles() const;

	/**
	 * Creates a new profile with this name and type.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings", meta = (DeterminesOutputType = "InArgs.ProfileType"))
	virtual UEnhancedPlayerMappableKeyProfile* CreateNewKeyProfile(const FPlayerMappableKeyProfileCreationArgs& InArgs);

	/** Returns the key profile with the given name if one exists. Null if one doesn't exist */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	virtual UEnhancedPlayerMappableKeyProfile* GetKeyProfileWithIdentifier(const FGameplayTag& ProfileId) const;

	template<class T>
	T* GetKeyProfileWithIdentifier(const FGameplayTag& ProfileId) const
	{
		return Cast<T>(GetKeyProfileWithIdentifier(ProfileId));
	}

	// Registering input mapping contexts for access to them from your UI,
	// even if they are froma plugin

	/** Fired when a new input mapping context is registered. Useful if you need to update your UI */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMappingContextRegisteredWithSettings, const UInputMappingContext*, IMC);
	FMappingContextRegisteredWithSettings OnMappingContextRegistered;

	/**
	 * Registers this mapping context with the user settings. This will iterate all the key mappings
	 * in the context and create an initial Player Mappable Key for every mapping that is marked as mappable.
	 */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	bool RegisterInputMappingContext(UInputMappingContext* IMC);

	/** Registers multiple mapping contexts with the settings */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	bool RegisterInputMappingContexts(const TSet<UInputMappingContext*>& MappingContexts);

	/** Removes this mapping context from the registered mapping contexts */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	bool UnregisterInputMappingContext(const UInputMappingContext* IMC);

	/** Removes miltiple mapping contexts from the registered mapping contexts */
	UFUNCTION(BlueprintCallable, Category="Enhanced Input|User Settings")
	bool UnregisterInputMappingContexts(const TSet<UInputMappingContext*>& MappingContexts);

	/** Gets all the currently registered mapping contexts with the settings */
	const TSet<TObjectPtr<UInputMappingContext>>& GetRegisteredInputMappingContexts() const;

	/** Returns true if this mapping context is currently registered with the settings */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enhanced Input|User Settings")
	bool IsMappingContextRegistered(const UInputMappingContext* IMC) const;
	
protected:

	/** The current key profile that is equipped by the user. */
	UPROPERTY(SaveGame)
	FGameplayTag CurrentProfileIdentifier;
	
	/**
	 * All of the known Key Profiles for this user, including the currently active profile.
	 */
	UPROPERTY(SaveGame)
	TMap<FGameplayTag, TObjectPtr<UEnhancedPlayerMappableKeyProfile>> SavedKeyProfiles;
	
	/** The owning Player Input object of these settings */
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedPlayerInput> OwningPlayerInput;
	
	/**
	 * Set of currently registered input mapping contexts that may not be currently
	 * active on the user, but you want to track for creating a menu for key mappings.
	 */
	UPROPERTY(Transient)
	TSet<TObjectPtr<UInputMappingContext>> RegisteredMappingContexts;
};
