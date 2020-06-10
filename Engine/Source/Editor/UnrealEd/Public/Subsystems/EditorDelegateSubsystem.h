// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"

#include "EditorDelegateSubsystem.generated.h"

/** delegate type for before edit cut actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditCutActorsBegin);
/** delegate type for after edit cut actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditCutActorsEnd);
/** delegate type for before edit copy actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditCopyActorsBegin);
/** delegate type for after edit copy actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditCopyActorsEnd);
/** delegate type for before edit paste actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditPasteActorsBegin);
/** delegate type for after edit paste actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditPasteActorsEnd);
/** delegate type for before edit duplicate actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDuplicateActorsBegin);
/** delegate type for after edit duplicate actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDuplicateActorsEnd);
/** delegate type for before delete actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeleteActorsBegin);
/** delegate type for after delete actors is handled */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeleteActorsEnd);

/**
* UEditorDelegateSubsystem
* Subsystem for exposing editor delegates to scripts,
*/
UCLASS()
class UNREALED_API UEditorDelegateSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEditorDelegateSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditCutActorsBegin OnEditCutActorsBegin;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditCutActorsEnd OnEditCutActorsEnd;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditCopyActorsBegin OnEditCopyActorsBegin;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditCopyActorsEnd OnEditCopyActorsEnd;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditPasteActorsBegin OnEditPasteActorsBegin;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditPasteActorsEnd OnEditPasteActorsEnd;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnEditCutActorsBegin OnDuplicateActorsBegin;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnDuplicateActorsEnd OnDuplicateActorsEnd;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnDeleteActorsBegin OnDeleteActorsBegin;

	UPROPERTY(BlueprintAssignable, Category = "Editor Scripting | Level Utility")
	FOnDeleteActorsEnd OnDeleteActorsEnd;

private:
	
	/** To fire before an Actor is Cut */
	void BroadcastEditCutActorsBegin();

	/** To fire after an Actor is Cut */
	void BroadcastEditCutActorsEnd();

	/** To fire before an Actor is Copied */
	void BroadcastEditCopyActorsBegin();

	/** To fire after an Actor is Copied */
	void BroadcastEditCopyActorsEnd();

	/** To fire before an Actor is Pasted */
	void BroadcastEditPasteActorsBegin();

	/** To fire after an Actor is Pasted */
	void BroadcastEditPasteActorsEnd();

	/** To fire before an Actor is duplicated */
	void BroadcastDuplicateActorsBegin();

	/** To fire after an Actor is duplicated */
	void BroadcastDuplicateActorsEnd();

	/** To fire before an Actor is Deleted */
	void BroadcastDeleteActorsBegin();

	/** To fire after an Actor is Deleted */
	void BroadcastDeleteActorsEnd();
};
