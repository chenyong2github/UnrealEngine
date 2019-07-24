// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"

#include "Engine/World.h"

#include "DataPrepContentConsumer.generated.h"

/**
 * Abstract class providing the minimal services required for a DataprepConsumer
 * 
 * Use the SDataprepConsumerWidget class to detail the properties of this class
 */
UCLASS(Experimental, Abstract, config = EditorSettings, HideCategories = (DataprepConsumerInternal))
class DATAPREPCORE_API UDataprepContentConsumer : public UObject
{
	GENERATED_BODY()

public:

	struct ConsumerContext
	{
		ConsumerContext() {}

		ConsumerContext& SetWorld( UWorld* InWorld )
		{ 
			WorldPtr = TWeakObjectPtr<UWorld>(InWorld);
			return *this;
		}

		ConsumerContext& SetAssets( TArray< TWeakObjectPtr< UObject > >& InAssets )
		{
			Assets.Empty(InAssets.Num());
			Assets.Append(InAssets);
			return *this;
		}

		ConsumerContext& SetProgressReporter( const TSharedPtr< IDataprepProgressReporter >& InProgressReporter )
		{
			ProgressReporterPtr = InProgressReporter;
			return *this;
		}

		ConsumerContext& SetLogger( const TSharedPtr< IDataprepLogger >& InLogger )
		{
			LoggerPtr = InLogger;
			return *this;
		}

		ConsumerContext& SetTransientContentFolder( const FString& InTransientContentFolder )
		{
			TransientContentFolder = InTransientContentFolder;
			return *this;
		}

		/** Hold onto the world the consumer will process */
		TWeakObjectPtr< UWorld > WorldPtr;

		/** Array of assets the consumer will process */
		TArray< TWeakObjectPtr< UObject > > Assets;

		/** Path to transient content folder where were created */
		FString TransientContentFolder;

		/** Hold onto the reporter that the consumer should use to report progress */
		TSharedPtr< IDataprepProgressReporter > ProgressReporterPtr;
		
		/** Hold onto the logger that the consumer should use to log messages */
		TSharedPtr<  IDataprepLogger > LoggerPtr;
	};

	UDataprepContentConsumer();

	/**
	 * Initialize the consumer to be ready for the next call to the Run method.
	 * @param Context : The world the consumer must process on. This world must be assumed to be transient.
	 * @param InAssets : Array of assets referenced or not by the given world. Those assets must be assumed to be transient.
	 * @param OutReason : Text containing description of failure if the initialization failed
	 * @return true if the initialization was successful, false otherwise
	 * @remark A copy of the context is made by the consumer. The context is cleared by a call to Reset
	 * @remark If TargetContentFolder member is empty, it is set to the package path of the consumer
	 * @remark The consumer is expected to remove objects it has consumed from the world and/or assets' array
	 */
	virtual bool Initialize( const ConsumerContext& Context, FString& OutReason );

	/**
	 * Requests the consumer to perform its operation.
	 */
	virtual bool Run() { return Context.WorldPtr.IsValid() || Context.Assets.Num() > 0 ? true : false; }

	/**
	 * Clean up the objects used by the consumer. This call follows a call to Run.
	 * Note: The consumer must assume that the world and assets it has not consumed are about to be deleted.
	 */
	virtual void Reset();

	/** Name used by the UI to be displayed. */
	virtual const FText& GetLabel() const { return FText::GetEmpty(); }

	/**
	 * Text briefly describing what the consumer is doing with the world and assets it consumes.
	 * Note: This text will be used as a tooltip in the UI.
	 */
	virtual const FText& GetDescription() const { return FText::GetEmpty(); }

	/**
	 * Sets the name of the level the consumer should move objects to if applicable.
	 * @param InLevelName : New name for the consumer's level.
	 * @param OutReason : String explaining reason of failure to set level name
	 * @return true if the name has been successfully set
	 * @remark if InLevelName is empty or equal to 'current' (case insensitive), no change is made
	 */
	virtual bool SetLevelName(const FString& InLevelName, FText& OutReason );

	/**
	 * Sets the path of the package the consumer should move assets to if applicable.
	 * Generally, this package path is substituted to the temporary path the assets are in
	 * @param InTargetContentFolder : Path of the package to save any assets in
	 * @return true if the assignment has been successful, false otherwise
	 * @remark if InPackagePath is empty the package path of the consumer is used
	 */
	virtual bool SetTargetContentFolder(const FString& InTargetContentFolder );

	const FString& GetLevelName() { return LevelName; }

	const FString& GetTargetContentFolder() { return TargetContentFolder; }

	/**
	 * Allow an observer to be notified when one of the properties of the consumer changes
	 * @return The delegate that will be broadcasted when the consumer changed
	 */
	DECLARE_EVENT(UDataprepContentConsumer, FDataprepConsumerChanged)
	FDataprepConsumerChanged& GetOnChanged()
	{
		return OnChanged;
	}

protected:

	// Start of helper functions to log messages and report progress
	void LogInfo(const FText& Message)
	{
		if ( Context.LoggerPtr.IsValid() )
		{
			Context.LoggerPtr->LogInfo( Message, *this );
		}
	}

	void LogWarning(const FText& Message)
	{
		if ( Context.LoggerPtr.IsValid() )
		{
			Context.LoggerPtr->LogWarning( Message, *this );
		}
	}

	void LogError(const FText& Message)
	{
		if ( Context.LoggerPtr.IsValid() )
		{
			Context.LoggerPtr->LogError( Message, *this );
		}
	}
	// End of helper functions to log messages

protected:
	UPROPERTY( EditAnywhere, Category = DataprepConsumerInternal )
	FString TargetContentFolder;

	UPROPERTY( EditAnywhere, Category = DataprepConsumerInternal )
	FString LevelName;

	/** Context which the consumer will run with */
	ConsumerContext Context;

	/** Delegate to broadcast changes to the consumer */
	FDataprepConsumerChanged OnChanged;
};