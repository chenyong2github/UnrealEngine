// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"


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
class METASOUNDENGINE_API UMetaSound : public UObject, public FMetasoundAssetBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendDocument MetasoundDocument;

	// Returns document object responsible for serializing asset
	Metasound::Frontend::FDocumentAccessPtr GetDocument() override;

	// Returns document object responsible for serializing asset
	Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const override;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UEdGraph* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSound(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITORONLY_DATA
	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetaSound, MetasoundDocument);
	}

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetaSound.
	virtual UEdGraph* GetGraph() override;
	virtual const UEdGraph* GetGraph() const override;
	virtual UEdGraph& GetGraphChecked() override;
	virtual const UEdGraph& GetGraphChecked() const override;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetaSound.
	virtual void SetGraph(UEdGraph* InGraph) override;
#endif // WITH_EDITORONLY_DATA

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

	const TArray<FMetasoundFrontendArchetype>& GetPreferredMetasoundArchetypes() const override;

	bool IsMetasoundArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const override;

	const FMetasoundFrontendArchetype& GetPreferredMetasoundArchetype(const FMetasoundFrontendDocument& InDocument) const override;

};
