// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IDMXControlConsoleFaderGroupElement.generated.h"

class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;

/** 
 * Interface used by elmeents that can be placed inside a Fixture Group.
 * Note, an Element has to be a set of consecutive channels.
 */
UINTERFACE()
class DMXCONTROLCONSOLE_API UDMXControlConsoleFaderGroupElement
	: public UInterface
{
	GENERATED_BODY()

};

class DMXCONTROLCONSOLE_API IDMXControlConsoleFaderGroupElement
{
	GENERATED_BODY()

public:
	/** Returns the Fader Group this Element resides in */
	virtual UDMXControlConsoleFaderGroup& GetOwnerFaderGroupChecked() const = 0;

	/** Returns the index of the Element in the Fader Group */
	virtual int32 GetIndex() const = 0;

	/** Returns the Faders of this Element */
	virtual const TArray<UDMXControlConsoleFaderBase*>& GetFaders() const = 0;

	/** Returns the index of the Element in the Fader Group */
	virtual int32 GetUniverseID() const = 0;

	/** Returns the first Channel of this Element */
	virtual int32 GetStartingAddress() const = 0;

	/** Returns the last Channel of this Element */
	virtual int32 GetEndingAddress() const = 0;

#if WITH_EDITOR
	/** Returns in Editor visibility state of the element */
	virtual bool GetIsVisibleInEditor() const { return bIsVisibleInEditor; }

	/** Sets in Editor visibility state of the element */
	virtual void SetIsVisibleInEditor(bool bVisible) { bIsVisibleInEditor = bVisible; }
#endif // WITH_EDITOR

	/** Destroys the Element */
	virtual void Destroy() = 0;

protected:
#if WITH_EDITOR
	/** In Editor visibility state of the Element */
	bool bIsVisibleInEditor = true;
#endif // WITH_EDITOR
};
