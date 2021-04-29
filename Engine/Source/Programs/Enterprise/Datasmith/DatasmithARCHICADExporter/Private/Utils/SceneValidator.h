// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

#include <map>

BEGIN_NAMESPACE_UE_AC

class FSceneValidator
{
  public:
	enum TInfoLevel
	{
		kBug,
		kError,
		kWarning,
		kVerbose,
		kInfoLevelMax
	};
	static const utf8_t* LevelName(TInfoLevel Level);

	FSceneValidator(const TSharedRef< IDatasmithScene >& InScene);

	TSharedRef< IDatasmithScene > Scene;

	FString GetElementTypes(const IDatasmithElement& InElement);

	FString GetElementsDescription(const IDatasmithElement& InElement);

	void CheckElementsName();

	void CheckActorsName(const IDatasmithActorElement& InActor);

	void CheckDependances();

	void CheckActorsDependances(const IDatasmithActorElement& InActor);

	class FNamePtr
	{
	  public:
		FNamePtr() {}

		FNamePtr(const TCHAR* InName)
			: Name(InName)
		{
		}

		bool operator<(const FNamePtr& InOther) const { return FCString::Strcmp(Name, InOther.Name) < 0; }

		const TCHAR* Name = nullptr;
	};

	typedef std::map< FNamePtr, const IDatasmithElement* > MapNameToElement;

	MapNameToElement NameToElementMap;

	class FUsage
	{
	  public:
		bool bExist;
		bool bIsRefered;
	};

	typedef std::map< FNamePtr, FUsage > FMapNameToUsage;

	FMapNameToUsage TexturesUsages;
	FMapNameToUsage MaterialsUsages;
	FMapNameToUsage MeshesUsages;
	FMapNameToUsage ActorsUsages;
	FMapNameToUsage LevelSequencesUsages;

	void AddElements(const IDatasmithElement& InElement, FMapNameToUsage* IOMap = nullptr);

	template < typename FmtType, typename... Types >
	void AddMessage(TInfoLevel Level, const FmtType& Fmt, Types... Args)
	{
		AddMessageImpl(Level, FString::Printf(Fmt, Args...));
	}

	void AddMessageImpl(TInfoLevel Level, const FString& Message);

	void PrintReports(TInfoLevel Level);

	class FMessage
	{
	  public:
		FMessage(TInfoLevel InLevel, const FString& InMessage)
			: Level(InLevel)
			, Message(InMessage)
		{
		}

		TInfoLevel Level;
		FString	   Message;
	};

	std::vector< FMessage > Messages;

	uint32_t MessagesCounts[kInfoLevelMax] = {};
};

END_NAMESPACE_UE_AC
