// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Interface.h"
#include "MetasoundFrontendDocument.h"

#include "MetasoundDocumentInterface.generated.h"


// Forward Declarations
struct FMetaSoundFrontendDocumentBuilder;


// UInterface for all MetaSound UClasses that implement a MetaSound document
// as a means for accessing data via scripting, execution, or node class generation.
UINTERFACE(BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class METASOUNDFRONTEND_API UMetaSoundDocumentInterface : public UInterface
{
	GENERATED_BODY()
};

class METASOUNDFRONTEND_API IMetaSoundDocumentInterface : public IInterface
{
	GENERATED_BODY()

public:
	virtual const FMetasoundFrontendDocument& GetDocument() const = 0;

private:
	virtual FMetasoundFrontendDocument& GetDocument() = 0;

	friend struct FMetaSoundFrontendDocumentBuilder;
};