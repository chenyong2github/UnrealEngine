// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundAssetBase.h"


#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
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

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetasound.
	UEdGraph* GetGraph();
	const UEdGraph* GetGraph() const;
	UEdGraph& GetGraphChecked();
	const UEdGraph& GetGraphChecked() const;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetasound.
	void SetGraph(UEdGraph* InGraph);

	const FText& GetInputToolTip(FString InputName) const;
	const FText& GetOutputToolTip(FString OutputName) const;

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

	// Deletes Metasound's current metasound document, and replaces it with InClassDescription.
	void SetMetasoundDocument(const FMetasoundDocument& InDocument);
};