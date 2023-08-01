// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Layouts/DMXControlConsoleEditorLayoutsBase.h"

#include "DMXControlConsoleEditorLayouts.generated.h"

class UDMXControlConsoleData;
class UDMXControlConsoleEditorGlobalLayoutBase;
class UDMXControlConsoleEditorGlobalLayoutDefault;
class UDMXControlConsoleEditorGlobalLayoutUser;


/** Control Console container class for layouts data */
UCLASS()
class UDMXControlConsoleEditorLayouts
	: public UDMXControlConsoleEditorLayoutsBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UDMXControlConsoleEditorLayouts();

	/** Adds a new User Layout */
	UDMXControlConsoleEditorGlobalLayoutUser* AddUserLayout(const FString& LayoutName);

	/** Deletes the given User Layout */
	void DeleteUserLayout(UDMXControlConsoleEditorGlobalLayoutUser* UserLayout);

	/** Finds the User Layout with the given name, if valid */
	UDMXControlConsoleEditorGlobalLayoutUser* FindUserLayoutByName(const FString& LayoutName) const;

	/** Clears UserLayouts array */
	void ClearUserLayouts();

	/** Gets reference to Default Layout */
	UDMXControlConsoleEditorGlobalLayoutDefault* GetDefaultLayout() const { return DefaultLayout; }

	/** Gets User Layouts array */
	const TArray<UDMXControlConsoleEditorGlobalLayoutUser*>& GetUserLayouts() const { return UserLayouts; }

	/** Gets reference to active Layout */
	UDMXControlConsoleEditorGlobalLayoutBase* GetActiveLayout() const { return ActiveLayout; }

	/** Sets the active Layout */
	void SetActiveLayout(UDMXControlConsoleEditorGlobalLayoutBase* InLayout);

	/** Updates the default Layout to the given Control Console Data */
	void UpdateDefaultLayout(const UDMXControlConsoleData* ControlConsoleData);

	/** Subscribes this Layout to Fixture Patch delegates */
	void SubscribeToFixturePatchDelegates();

	/** Subscribes this Layout from Fixture Patch delegates */
	void UnsubscribeFromFixturePatchDelegates();

private:
	/** Reference to Default Layout */
	UPROPERTY()
	TObjectPtr<UDMXControlConsoleEditorGlobalLayoutDefault> DefaultLayout;

	/** Array of User Layouts */
	UPROPERTY()
	TArray<TObjectPtr<UDMXControlConsoleEditorGlobalLayoutUser>> UserLayouts;

	/** Reference to active Layout in use */
	UPROPERTY()
	TObjectPtr<UDMXControlConsoleEditorGlobalLayoutBase> ActiveLayout;
};
