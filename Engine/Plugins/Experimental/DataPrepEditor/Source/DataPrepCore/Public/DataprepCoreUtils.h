// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"

#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

struct FScopedSlowTask;

class DATAPREPCORE_API FDataprepCoreUtils
{
public:
	static void PurgeObjects(TArray<UObject*> Objects);

	/**
	 * Checks if the input object is an asset even if transient or in a transient package
	 * @param Object	The object to check on
	 * @return	true if Object has the right flags or Object is of predefined classes
	 * @remark	This method is *solely* intended for objects manipulated by a Dataprep asset
	 */
	static bool IsAsset(UObject* Object);

	/** 
	 * Rename this object to a unique name, or change its outer.
	 * 
	 * @param	Object		The object to rename
	 * @param	NewName		The new name of the object, if null then NewOuter should be set
	 * @param	NewOuter	New Outer this object will be placed within, if null it will use the current outer
	 * @remark	This method is *solely* intended for objects manipulated by a Dataprep asset
	 */
	static void RenameObject(UObject* Object, const TCHAR* NewName = nullptr, UObject* NewOuter = nullptr)
	{
		if(Object != nullptr)
		{
			Object->Rename( NewName, NewOuter, REN_NonTransactional | REN_DontCreateRedirectors );
		}
	}

	/** 
	 * Move an object to the /Game/Transient package.
	 * 
	 * @param	Object		The object to move
	 * @remark	This method is *solely* intended for objects manipulated by a Dataprep asset
	 */
	static void MoveToTransientPackage(UObject* Object)
	{
		if(Object != nullptr)
		{
			Object->Rename( nullptr, GetTransientPackage(), REN_NonTransactional | REN_DontCreateRedirectors );
		}
	}

	class DATAPREPCORE_API FDataprepLogger : public IDataprepLogger
	{
	public:
		virtual ~FDataprepLogger() {}

		// Begin IDataprepLogger interface
		virtual void LogInfo(const FText& InLogText, const UObject& InObject) override;
		virtual void LogWarning(const FText& InLogText, const UObject& InObject) override;
		virtual void LogError(const FText& InLogText,  const UObject& InObject) override;
		// End IDataprepLogger interface

	};

	class DATAPREPCORE_API FDataprepProgressUIReporter : public IDataprepProgressReporter
	{
	public:
		FDataprepProgressUIReporter()
		{
		}

		virtual ~FDataprepProgressUIReporter()
		{
		}

		// Begin IDataprepProgressReporter interface
		virtual void BeginWork( const FText& InTitle, float InAmountOfWork ) override;
		virtual void EndWork() override;
		virtual void ReportProgress( float Progress, const FText& InMessage ) override;
		// End IDataprepProgressReporter interface

	private:
		TArray< TSharedPtr< FScopedSlowTask > > ProgressTasks;
	};

	class DATAPREPCORE_API FDataprepProgressTextReporter : public IDataprepProgressReporter
	{
	public:
		FDataprepProgressTextReporter()
			: TaskDepth(0)
		{
		}

		virtual ~FDataprepProgressTextReporter()
		{
		}

		// Begin IDataprepProgressReporter interface
		virtual void BeginWork( const FText& InTitle, float InAmountOfWork ) override;
		virtual void EndWork() override;
		virtual void ReportProgress( float Progress, const FText& InMessage ) override;

	private:
		int32 TaskDepth;
	};
};

