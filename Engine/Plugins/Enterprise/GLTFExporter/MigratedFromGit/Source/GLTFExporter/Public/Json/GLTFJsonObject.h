// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonValue.h"
#include "Json/GLTFJsonWriter.h"

struct GLTFEXPORTER_API IGLTFJsonObject : IGLTFJsonValue
{
	virtual void WriteValue(IGLTFJsonWriter& Writer) const override final
	{
		Writer.StartObject();
		WriteObject(Writer);
		Writer.EndObject();
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const = 0;
};
