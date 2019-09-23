// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "uLang/Common/Containers/SharedPointerArray.h" // for TSRefArray<>
#include "uLang/Diagnostics/Glitch.h"
#include "uLang/IDESupport/IIdeAutoCompleteProxy.h"
#include "uLang/CompilerPasses/CompilerTypes.h" // for SAstSnippet, CSyntaxSemanticMap
#include "uLang/Toolchain/ProgramBuildManager.h"

/* IVplIde
 *****************************************************************************/

/**
 * Controller interface for the VPL to interact with the compiler toolchain (getting errors, etc.).
 * @TODO: Merge IVplDataSource into this.
 */
class IVplIde
{
public:
	/** 
	 * Hook for the VPL editor to know when a compile has been triggered, and finished.
	 */
	DECLARE_MULTICAST_DELEGATE(FOnVplBuildComplete);
	virtual FOnVplBuildComplete& OnVplBuildComplete() = 0;

	/**
	 * Returns an interface object, providing code-completion methods.
	 */
	virtual uLang::TSPtr<uLang::IIdeAutoCompleteProxy> GetAutoCompleteProxy() const = 0;

	/** 
	 * Query to check the warning/error state of the last compile.
	 */
	virtual bool HasAnyGlitches() const = 0;

	/** 
	 * Accessor to the 'Glitches' (build warnings/errors) from the last compile.
	 */
	virtual const uLang::TSRefArray<uLang::SGlitch>& GetGlitches() const = 0;

	/** 
	 *  Accessor for the shared symbol table, used by the IDE (and compiler toolchain).
	 */
	virtual const uLang::TSRef<uLang::CSymbolTable>& GetSymbolTable() const = 0;

	/**
	 * Accessor for the build manager (which maintains the persistent program database, etc.).
	 */
	virtual uLang::TSRef<uLang::CProgramBuildManager> GetBuildManagerRef() const = 0;

	/**
	 * Compiles all data-sources down to their semantic expression tree representation.
	 * @return An AST-to-SemanticExpression mapping for the VPL to lookup type information.
	 */
	virtual const uLang::CSyntaxSemanticMap& GenAutoCompleteInfo() = 0;
};

/* IVplDataSource
 *****************************************************************************/

class IVplDataSource
{
public:
	virtual ~IVplDataSource() {}

	virtual TSharedRef<IVplIde> GetOwningEnvironment() const = 0;
	virtual const uLang::SAstSnippet& GenAst() = 0;
	virtual void  OnAstMutated(const uLang::SAstSnippet& NewAst) = 0;

	DECLARE_MULTICAST_DELEGATE(FOnDataUpdate);
	FOnDataUpdate& OnDataUpdate() { return OnDataUpdate_Delegate; }

protected:
	FOnDataUpdate OnDataUpdate_Delegate;
};