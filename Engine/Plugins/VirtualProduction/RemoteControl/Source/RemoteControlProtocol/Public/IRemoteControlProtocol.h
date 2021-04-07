// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/StructOnScope.h"

class URemoteControlPreset;
struct FRemoteControlProtocolEntity;

using FRemoteControlProtocolEntityPtr = TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>>;
using FRemoteControlProtocolEntityWeakPtr = TWeakPtr<TStructOnScope<FRemoteControlProtocolEntity>>;

/**
 * Interface for remote control protocol
 */
class REMOTECONTROLPROTOCOL_API IRemoteControlProtocol : public TSharedFromThis<IRemoteControlProtocol>
{
public:
	/** Virtual destructor */
	virtual ~IRemoteControlProtocol() {}

	/** Add custom initializing of the protocol */
	virtual void Init() {}

	/**
	 * Add the new protocol entity. That is created based on protocol specific script struct
	 * @param InProperty exposed property
	 * @param InOwner The preset that owns this protocol entity.
	 * @param InPresetName unique id of exposed property
	 * @return A shared pointer protocol entity struct on scope
	 */
	virtual FRemoteControlProtocolEntityPtr CreateNewProtocolEntity(FProperty* InProperty, URemoteControlPreset* InOwner, FGuid InPropertyId) const = 0;

	/** Get protocol specific Script Struct class */
	virtual UScriptStruct* GetProtocolScriptStruct() const = 0;

	/** Get range input template Property */
	virtual FProperty* GetRangeInputTemplateProperty() const;

	/**
	 * Bind the protocol entity to the protocol
	 * @param InEntityPtr protocol entity struct on scope pointer
	 */
	virtual void Bind(FRemoteControlProtocolEntityPtr InEntityPtr) = 0;

	/**
	 * Unbind the protocol entity from the protocol
	 * @param InEntityPtr protocol entity struct on scope pointer
	 */
	virtual void Unbind(FRemoteControlProtocolEntityPtr InEntityPtr) = 0;

	/** Unbind all protocol entities from the protocol */
	virtual void UnbindAll() = 0;
};
