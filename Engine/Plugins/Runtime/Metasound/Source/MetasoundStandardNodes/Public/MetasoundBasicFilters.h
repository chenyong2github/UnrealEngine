// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

#include "MetasoundFacade.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FLadderFilterNode : public FNodeFacade
	{
	public:
		// public node api needs to define two conversion constructors:
		// 1.) from FString
		FLadderFilterNode(const FString& InInstanceName, const FGuid& InInstanceID);
		// 2.) From an NodeInitData struct
		FLadderFilterNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FStateVariableFilterNode : public FNodeFacade
	{
	public:
		// public node api needs to define two conversion constructors:
		// 1.) from FString
		FStateVariableFilterNode(const FString& InInstanceName, const FGuid& InInstanceID);
		// 2.) From an NodeInitData struct
		FStateVariableFilterNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FOnePoleLowPassFilterNode : public FNodeFacade
	{
	public:
		// public node api needs to define two conversion constructors:
		// 1.) from FString
		FOnePoleLowPassFilterNode(const FString& InInstanceName, const FGuid& InInstanceID);
		// 2.) From an NodeInitData struct
		FOnePoleLowPassFilterNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FOnePoleHighPassFilterNode : public FNodeFacade
	{
	public:
		// public node api needs to define two conversion constructors:
		// 1.) from FString
		FOnePoleHighPassFilterNode(const FString& InInstanceName, const FGuid& InInstanceID);
		// 2.) From an NodeInitData struct
		FOnePoleHighPassFilterNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FBiquadFilterNode : public FNodeFacade
	{
	public:
		// public node api needs to define two conversion constructors:
		// 1.) from FString
		FBiquadFilterNode(const FString& InInstanceName, const FGuid& InInstanceID);
		// 2.) From an NodeInitData struct
		FBiquadFilterNode(const FNodeInitData& InInitData);
	};

} // namespace Metasound
