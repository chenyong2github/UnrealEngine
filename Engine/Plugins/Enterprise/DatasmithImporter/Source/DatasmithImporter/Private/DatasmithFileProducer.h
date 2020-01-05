// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepContentProducer.h"

#include "DatasmithContentEditorModule.h"
#include "DatasmithImportContext.h"
#include "DatasmithImportOptions.h"
#include "DatasmithScene.h"
#include "Translators/DatasmithTranslatableSource.h"

#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/StrongObjectPtr.h"

#include "DatasmithFileProducer.generated.h"

class IDetailLayoutBuilder;

UCLASS(Experimental, HideCategories = (DatasmithProducer_Internal))
class DATASMITHIMPORTER_API UDatasmithFileProducer : public UDataprepContentProducer
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void PostEditUndo() override;
	virtual void PostInitProperties() override;
	// End of UObject interface

	/** Update producer with newly selected filename */
	void SetFilename( const FString& InFilename );

	const FString& GetFilePath() const { return FilePath; }

	/** Load default settings for file producer in DatasmithImporter.ini */
	static void LoadDefaultSettings();

	// Begin UDataprepContentProducer overrides
	virtual const FText& GetLabel() const override;
	virtual const FText& GetDescription() const override;
	virtual FString GetNamespace() const override;
	virtual bool Supersede(const UDataprepContentProducer* OtherProducer) const override;

protected:
	virtual bool Initialize() override;
	virtual bool Execute(TArray< TWeakObjectPtr< UObject > >& OutAssets) override;
	virtual void Reset() override;
	// End UDataprepContentProducer overrides

	UPROPERTY( EditAnywhere, Category = DatasmithProducer_Internal )
	FString FilePath;

private:
	/** Fill up world with content of Datasmith scene element */
	void SceneElementToWorld();

	/** Fill up world with content of Datasmith scene element */
	void PreventNameCollision();

	/** Update the name of the producer based on the filename */
	void UpdateName();

private:
	TUniquePtr< FDatasmithImportContext > ImportContextPtr;
	TUniquePtr< FDatasmithTranslatableSceneSource > TranslatableSourcePtr;
	TUniquePtr< FDataprepWorkReporter > ProgressTaskPtr;

	TStrongObjectPtr< UDatasmithScene > DatasmithScenePtr;

	TArray< TWeakObjectPtr< UObject > > Assets;

	static FDatasmithTessellationOptions DefaultTessellationOptions;
	static FDatasmithImportBaseOptions DefaultImportOptions;

	friend class SDatasmithFileProducerFileProperty;
	friend class UDatasmithDirProducer;
};

// Customization of the details of the Datasmith Scene for the data prep editor.
class DATASMITHIMPORTER_API FDatasmithFileProducerDetails : public IDetailCustomization
{
public:
	static TSharedRef< IDetailCustomization > MakeDetails() { return MakeShared<FDatasmithFileProducerDetails>(); };

	/** Called when details should be customized */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
};

UCLASS(Experimental, HideCategories = (DatasmithDirProducer_Internal))
class DATASMITHIMPORTER_API UDatasmithDirProducer : public UDataprepContentProducer
{
	GENERATED_BODY()

protected:
	UDatasmithDirProducer();

public:

	// Begin UObject interface
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostInitProperties() override;
	// End of UObject interface

	// End UObject interface

	/** Update producer with newly selected folder name */
	void SetFolderName( const FString& InFolderName );

	// Begin UDataprepContentProducer overrides
	virtual const FText& GetLabel() const override;
	virtual const FText& GetDescription() const override;
	virtual FString GetNamespace() const override;
	virtual bool Supersede(const UDataprepContentProducer* OtherProducer) const override;

protected:
	virtual bool Initialize() override;
	virtual bool Execute(TArray< TWeakObjectPtr< UObject > >& OutAssets) override;
	virtual void Reset() override;
	// End UDataprepContentProducer overrides

	UPROPERTY( EditAnywhere, Category = DatasmithDirProducer_Internal )
	FString FolderPath;

	UPROPERTY( EditAnywhere, Category = DatasmithDirProducer, meta = (ToolTip = "semi-column separated string containing the extensions to consider. By default, set to * to get all extensions") )
	FString ExtensionString;

	UPROPERTY( EditAnywhere, Category = DatasmithDirProducer, meta = (ToolTip = "If checked, sub-directories will be traversed") )
	bool bRecursive;

	/** Called if ExtensionString has changed */
	void OnExtensionsChanged();

	/** Called if bRecursive has changed */
	void OnRecursivityChanged();

private:
	/** Helper function to extract set of extensions based on content of ExtensionString and supported formats */
	void UpdateExtensions();

	/** Helper function to get all matching files in FolderPath based on extensions set */
	TSet<FString> GetSetOfFiles() const;

	/** Update the name of the producer based on the directory name */
	void UpdateName();

private:
	/** Indicates if ExtensionString contains "*.*" */
	bool bHasWildCardSearch;

	/** Set of extensions to look for */
	TSet< FString > FixedExtensionSet;

	/** Set of files matching folder and extensions */
	TSet< FString > FilesToProcess;

	TStrongObjectPtr< UDatasmithFileProducer > FileProducer;

	static TSet< FString > SupportedFormats;

	friend class SDatasmithDirProducerFolderProperty;
	friend class FDatasmithDirProducerDetails;
};

// Customization of the details of the Datasmith Scene for the data prep editor.
class DATASMITHIMPORTER_API FDatasmithDirProducerDetails : public IDetailCustomization
{
public:
	static TSharedRef< IDetailCustomization > MakeDetails() { return MakeShared<FDatasmithDirProducerDetails>(); };

	/** Called when details should be customized */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
};
