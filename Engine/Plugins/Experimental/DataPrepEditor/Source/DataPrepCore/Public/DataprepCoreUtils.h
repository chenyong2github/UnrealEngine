// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"

#include "FeedbackContextEditor.h"
#include "HAL/FeedbackContextAnsi.h"

#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

class UDataprepAsset;

struct FScopedSlowTask;

class DATAPREPCORE_API FDataprepCoreUtils
{
public:

	/**
	 * Return the dataprep asset that own the object, if the object is part of a dataprep asset
	 * @return nullptr if the object is not a part of a dataprep asset
	 */
	static UDataprepAsset* GetDataprepAssetOfObject(UObject* Object);

	/** Delete the objects and do the manipulation required to safely delete the assets */
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

	/**
	 * Helper function to build assets for use in the Dataprep pipeline

	 * @param	Assets					Array of weak pointer on the assets to be build
	 * @param	ProgressReporterPtr		Pointer to a IDataprepProgressReporter interface. This pointer can be invalid.
	 */
	static void BuildAssets(const TArray< TWeakObjectPtr<UObject> >& Assets, const TSharedPtr< IDataprepProgressReporter >& ProgressReporterPtr );

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

	class DATAPREPCORE_API FDataprepFeedbackContext : public FFeedbackContextEditor
	{
	public:
		/** 
		 * We want to override this method in order to cache the cancel result and not clear it,
		 * so it can be checked multiple times with the correct result!
		 * (FFeedbackContextEditor::ReceivedUserCancel clears it)
		 */
		virtual bool ReceivedUserCancel() override 
		{ 
			if ( !bTaskWasCancelledCache )
			{
				bTaskWasCancelledCache = FFeedbackContextEditor::ReceivedUserCancel();
			}
			return bTaskWasCancelledCache;
		}

	private:
		bool bTaskWasCancelledCache = false;
	};

	class DATAPREPCORE_API FDataprepProgressUIReporter : public IDataprepProgressReporter
	{
	public:
		FDataprepProgressUIReporter()
			: bIsCancelled(false)
		{
		}

		FDataprepProgressUIReporter( TSharedRef<FFeedbackContext> InFeedbackContext )
			: FeedbackContext(InFeedbackContext)
			, bIsCancelled(false)
		{
		}

		virtual ~FDataprepProgressUIReporter()
		{
		}

		// Begin IDataprepProgressReporter interface
		virtual void BeginWork( const FText& InTitle, float InAmountOfWork, bool bInterruptible = true ) override;
		virtual void EndWork() override;
		virtual void ReportProgress( float Progress, const FText& InMessage ) override;
		virtual bool IsWorkCancelled() override;
		virtual FFeedbackContext* GetFeedbackContext() const override;
		// End IDataprepProgressReporter interface

	private:
		TArray< TSharedPtr< FScopedSlowTask > > ProgressTasks;
		TSharedPtr< FFeedbackContext > FeedbackContext;
		bool bIsCancelled;
	};

	class DATAPREPCORE_API FDataprepProgressTextReporter : public IDataprepProgressReporter
	{
	public:
		FDataprepProgressTextReporter()
			: TaskDepth(0)
			, FeedbackContext( new FFeedbackContextAnsi )
		{
		}

		virtual ~FDataprepProgressTextReporter()
		{
		}

		// Begin IDataprepProgressReporter interface
		virtual void BeginWork( const FText& InTitle, float InAmountOfWork, bool bInterruptible = true ) override;
		virtual void EndWork() override;
		virtual void ReportProgress( float Progress, const FText& InMessage ) override;
		virtual bool IsWorkCancelled() override;
		virtual FFeedbackContext* GetFeedbackContext() const override;

	private:
		int32 TaskDepth;
		TUniquePtr<FFeedbackContextAnsi> FeedbackContext;
	};
};

