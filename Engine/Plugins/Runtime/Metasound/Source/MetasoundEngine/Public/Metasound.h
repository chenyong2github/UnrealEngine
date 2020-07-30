// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundAssetBase.h"


#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#include "Misc/AssertionMacros.h"
#endif // WITH_EDITORONLY_DATA

#include "Metasound.generated.h"

// Forward Declarations
class FEditPropertyChain;
struct FPropertyChangedEvent;


/**
 * This asset type is used for Metasound assets that can only be used as nodes in other Metasound graphs.
 * Because of this, they can have any inputs or outputs they need.
 */
UCLASS(hidecategories = object, BlueprintType)
class METASOUNDENGINE_API UMetasound : public UObject, public FMetasoundAssetBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = Hidden)
	FMetasoundDocument RootMetasoundDocument;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UEdGraph* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetasound(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITORONLY_DATA
	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetasound, RootMetasoundDocument);
	}

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetasound.
	virtual UEdGraph* GetGraph() override;
	virtual const UEdGraph* GetGraph() const override;
	virtual UEdGraph& GetGraphChecked() override;
	virtual const UEdGraph& GetGraphChecked() const override;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetasound.
	virtual void SetGraph(UEdGraph* InGraph) override;
#endif // WITH_EDITORONLY_DATA

	FMetasoundDocument& GetDocument() override
	{
		return RootMetasoundDocument;
	}

	const FMetasoundDocument& GetDocument() const override
	{
		return RootMetasoundDocument;
	}

	UObject* GetOwningAsset() const override
	{
		// Hack to allow handles to manipulate while providing
		// ability to inspect via a handle from the const version of GetRootGraphHandle()
		return const_cast<UObject*>(CastChecked<const UObject>(this));
	}

	// Updates the Metasound's metadata (name, author, etc).
	// @param InMetadata Metadata containing corrections to the class metadata.
	void SetMetadata(FMetasoundClassMetadata& InMetadata) override;

	// Get the most up to date archetype for metasound.
	virtual FMetasoundArchetype GetArchetype() const override;

	void PostLoad() override;

};