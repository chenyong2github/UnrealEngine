// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

struct CORE_API FHierarchicalLogArchive : private FArchiveProxy
{
public:
	FHierarchicalLogArchive(FArchive& InInnerArchive);

	struct FIndentScope
	{
		FIndentScope(FHierarchicalLogArchive* InAr) : Ar(InAr) { Ar->Indentation++; }
		~FIndentScope() { check(Ar->Indentation); Ar->Indentation--; }
		FHierarchicalLogArchive* Ar;
	};

	template <typename FmtType, typename... Types>
	void Printf(const FmtType& Fmt, Types... Args)
	{
		WriteLine(FString::Printf(Fmt, Args...), false);
	}

	template <typename FmtType, typename... Types>
	UE_NODISCARD FIndentScope PrintfIndent(const FmtType& Fmt, Types... Args)
	{
		WriteLine(FString::Printf(Fmt, Args...), true);
		return FIndentScope(this);
	}

private:
	void WriteLine(const FString& InLine, bool bIndent = false);

	int32 Indentation;
};