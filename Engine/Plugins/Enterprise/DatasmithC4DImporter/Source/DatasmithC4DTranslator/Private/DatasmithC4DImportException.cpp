// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#ifdef _MELANGE_SDK_

#include "DatasmithC4DImportException.h"

DatasmithC4DImportException::DatasmithC4DImportException(const FString& Message) : Message(Message)
{
}

DatasmithC4DImportException::~DatasmithC4DImportException()
{
}

FString DatasmithC4DImportException::GetMessage() const
{
	return Message;
}

#endif //_MELANGE_SDK_