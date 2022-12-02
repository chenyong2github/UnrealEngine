// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Types/UIFParentWidget.h"

class UUIFrameworkPlayerComponent;
class UUIFrameworkPresenter;
class UUIFrameworkWidget;

/**
 * 
 */
class UIFRAMEWORK_API FUIFrameworkModule : public IModuleInterface
{
public:
	/**
	 * Set the new widget parent.
	 * It will attach the widget to the correct ReplicationOwner and add it to the WidgetTree.
	 * If the ReplicationOwner cannot be changed, a duplicate of the widget will be created and the AuthorityOnReattachWidgets will be broadcast.
	 * This function is recursive if the owner changed.
	 */
	static UUIFrameworkWidget* AuthorityAttachWidget(FUIFrameworkParentWidget Parent, UUIFrameworkWidget* Child);
	static bool AuthorityCanWidgetBeAttached(FUIFrameworkParentWidget Parent, UUIFrameworkWidget* Child);
	/**
	 * Will remove the widget from the tree and the replication owner.
	 */
	static void AuthorityDetachWidgetFromParent(UUIFrameworkWidget* Child);

	//~ this should be a project setting
	static void SetPresenterClass(TSubclassOf<UUIFrameworkPresenter> Director);
	static TSubclassOf<UUIFrameworkPresenter> GetPresenterClass();

private:
	static void AuthorityDetachWidgetFromParentInternal(UUIFrameworkWidget* Child, bool bTemporary);
	//static UUIFrameworkWidget* AuthorityRenameRecursive(UUIFrameworkPlayerComponent* ReplicationOwner, UUIFrameworkWidget* Widget, UObject* NewOuter);
	//static void AuthoritySetParentReplicationOwnerRecursive(UUIFrameworkWidget* Widget);
};