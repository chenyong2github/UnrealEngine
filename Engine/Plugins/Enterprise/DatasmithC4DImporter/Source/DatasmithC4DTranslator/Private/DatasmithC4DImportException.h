// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class DatasmithC4DImportException
{
public:
	DatasmithC4DImportException(const FString& Message);
	virtual ~DatasmithC4DImportException();
	FString GetMessage() const;

protected:
	FString Message;
};

#define DatasmithC4DImportCheck(expr) if (!(expr)) throw DatasmithC4DImportException(#expr);