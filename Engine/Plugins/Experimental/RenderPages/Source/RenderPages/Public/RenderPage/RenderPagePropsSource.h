// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RemoteControlPreset.h"
#include "RenderPagePropsSource.generated.h"


/////////////////////////////////////////////////////
// Enums

/**
 * The type of the properties source.
 * In other words, where the properties come from that each render page can have.
 */
UENUM(BlueprintType)
enum class ERenderPagePropsSourceType : uint8
{
	Local = 0 UMETA(DisplayName = "Local Source"),
	RemoteControl = 1 UMETA(DisplayName = "Remote Control Preset")
};


/////////////////////////////////////////////////////
// Base classes

/**
 * The base class of the render page property abstraction.
 */
UCLASS(Abstract, HideCategories=Object)
class RENDERPAGES_API URenderPagePropBase : public UObject
{
	GENERATED_BODY()
};

/**
 * The base class of the render page properties abstraction.
 */
UCLASS(Abstract, HideCategories=Object)
class RENDERPAGES_API URenderPagePropsBase : public UObject
{
	GENERATED_BODY()

public:
	/** Returns all props. */
	virtual TArray<URenderPagePropBase*> GetAll() const { return TArray<URenderPagePropBase*>(); }
};

/**
 * The base class of the render page properties source abstraction.
 */
UCLASS(Abstract, BlueprintType, HideCategories=Object)
class RENDERPAGES_API URenderPagePropsSourceBase : public UObject
{
	GENERATED_BODY()

public:
	/** Returns the ID, which is randomly generated when an instance of this class is constructed. */
	FGuid GetId() const { return Id; }

	/** Randomly generates a new ID. */
	void GenerateNewId() { Id = FGuid::NewGuid(); }


	/** Returns the type of this properties source. */
	virtual ERenderPagePropsSourceType GetType() const { return ERenderPagePropsSourceType::Local; }

	/** Sets the properties source. */
	virtual void SetSourceOrigin(UObject* SourceOrigin) {}

	/** Returns the collection of properties (that this properties source contains). */
	virtual URenderPagePropsBase* GetProps() const { return NewObject<URenderPagePropsBase>(const_cast<URenderPagePropsSourceBase*>(this)); }

protected:
	UPROPERTY()
	FGuid Id = FGuid::NewGuid();
};


/////////////////////////////////////////////////////
// Local source classes

/**
 * The local properties implementation of the render page property abstraction.
 */
UCLASS(HideCategories=Object)
class RENDERPAGES_API URenderPagePropLocal : public URenderPagePropBase
{
	GENERATED_BODY()
};

/**
 * The local properties implementation of the render page properties abstraction.
 */
UCLASS(HideCategories=Object)
class RENDERPAGES_API URenderPagePropsLocal : public URenderPagePropsBase
{
	GENERATED_BODY()
};

/**
 * The local properties implementation of the render page properties source abstraction.
 */
UCLASS(BlueprintType, HideCategories=Object)
class RENDERPAGES_API URenderPagePropsSourceLocal : public URenderPagePropsSourceBase
{
	GENERATED_BODY()

public:
	//~ Begin URenderPagePropsSourceBase interface
	virtual ERenderPagePropsSourceType GetType() const override { return ERenderPagePropsSourceType::Local; }
	virtual URenderPagePropsBase* GetProps() const override { return NewObject<URenderPagePropsLocal>(const_cast<URenderPagePropsSourceLocal*>(this)); }
	//~ End URenderPagePropsSourceBase interface
};


/////////////////////////////////////////////////////
// Remote control source classes

/**
 * The remote control properties implementation of the render page property abstraction.
 */
UCLASS(HideCategories=Object)
class RENDERPAGES_API URenderPagePropRemoteControl : public URenderPagePropBase
{
	GENERATED_BODY()

public:
	/** Gets the value (as bytes) of the given property (remote control entity). Returns true if the operation was successful, false otherwise. */
	static bool GetValueOfEntity(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray);

	/** Set the value (as bytes) of the given property (remote control entity). Returns true if the operation was successful, false otherwise. */
	static bool SetValueOfEntity(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray);

	/** Tests if it can set the value (as bytes) of the given property (remote control entity). Returns true if the set operation would likely be successful, false otherwise. */
	static bool CanSetValueOfEntity(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray);

public:
	/** Sets the initial values of this instance. */
	void Initialize(const TSharedPtr<FRemoteControlEntity>& InRemoteControlEntity);

	/** Returns the property, which is a remote control entity (which can be a field or a function). */
	TSharedPtr<FRemoteControlEntity> GetRemoteControlEntity() const { return RemoteControlEntity; }

	/** Gets the value (as bytes) of this property. Returns true if the operation was successful, false otherwise. */
	bool GetValue(TArray<uint8>& OutBinaryArray) const { return GetValueOfEntity(RemoteControlEntity, OutBinaryArray); }

	/** Set the value (as bytes) of this property. Returns true if the operation was successful, false otherwise. */
	bool SetValue(const TArray<uint8>& BinaryArray) { return SetValueOfEntity(RemoteControlEntity, BinaryArray); }

	/** Tests if it can set the value (as bytes) of this property. Returns true if the set operation would likely be successful, false otherwise. */
	bool CanSetValue(const TArray<uint8>& BinaryArray) { return CanSetValueOfEntity(RemoteControlEntity, BinaryArray); }

protected:
	/** The property, which is a remote control entity (which can be a field or a function). */
	TSharedPtr<FRemoteControlEntity> RemoteControlEntity = nullptr;
};

/**
 * The remote control properties implementation of the render page properties abstraction.
 */
UCLASS(HideCategories=Object)
class RENDERPAGES_API URenderPagePropsRemoteControl : public URenderPagePropsBase
{
	GENERATED_BODY()

public:
	/** Sets the initial values of this instance. */
	void Initialize(URemoteControlPreset* InRemoteControlPreset);

	//~ Begin URenderPagePropsBase interface
	virtual TArray<URenderPagePropBase*> GetAll() const override;
	//~ End URenderPagePropsBase interface

	/** Returns all props, casted to URenderPagePropRemoteControl, for ease of use. */
	TArray<URenderPagePropRemoteControl*> GetAllCasted() const;

	/** Returns the remote control preset. */
	URemoteControlPreset* GetRemoteControlPreset() const { return (IsValid(RemoteControlPreset) ? RemoteControlPreset : nullptr); }

protected:
	/** The source of properties, which is a remote control preset. */
	UPROPERTY()
	TObjectPtr<URemoteControlPreset> RemoteControlPreset = nullptr;
};

/**
 * The remote control properties implementation of the render page properties source abstraction.
 */
UCLASS(BlueprintType, HideCategories=Object)
class RENDERPAGES_API URenderPagePropsSourceRemoteControl : public URenderPagePropsSourceBase
{
	GENERATED_BODY()

public:
	//~ Begin URenderPagePropsSourceBase interface
	virtual ERenderPagePropsSourceType GetType() const override { return ERenderPagePropsSourceType::RemoteControl; }
	virtual void SetSourceOrigin(UObject* SourceOrigin) override;
	virtual URenderPagePropsRemoteControl* GetProps() const override;
	//~ End URenderPagePropsSourceBase interface

	/** Returns (in the out parameter) the preset groups that are available in this remote control preset. */
	void GetAvailablePresetGroups(TArray<FName>& OutPresetGroups) const;

protected:
	/** The source of properties, which is a remote control preset. */
	UPROPERTY(VisibleInstanceOnly, Category="Render Pages|Remote Control")
	TObjectPtr<URemoteControlPreset> RemoteControlPreset = nullptr;

	/** The preset group (of the remote control preset) that we should obtain the properties from. */
	UPROPERTY(VisibleInstanceOnly, Category="Render Pages|Remote Control")
	FName ActivePresetGroup;
};
