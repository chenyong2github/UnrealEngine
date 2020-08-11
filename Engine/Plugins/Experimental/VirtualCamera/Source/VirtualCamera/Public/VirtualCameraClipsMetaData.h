// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "VirtualCameraClipsMetaData.generated.h"

/**
 * Clips meta-data that is stored on ULevelSequence assets that are recorded through the virtual camera. 
 * Meta-data is retrieved through ULevelSequence::FindMetaData<UVirtualCameraClipsMetaData>()
 */


UCLASS(BlueprintType)
class UVirtualCameraClipsMetaData : public UObject
{
public: 
	GENERATED_BODY()
	UVirtualCameraClipsMetaData(const FObjectInitializer& ObjInit);

public:
	/**
	 * @return The focal length for this clip
	 */
	UFUNCTION(BlueprintCallable, Category = "Clips")
	float GetFocalLength() const;

	/**
	* @return Whether or not the clip is selected.  
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	bool GetSelected() const;

	/**
	* @return The name of the clip's recorded level. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	FString GetRecordedLevelName() const;

	/**
	* @return The initial frame of the clip
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	int GetFrameCountStart() const;

	/**
	* @return The final frame of the clip
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	int GetFrameCountEnd() const;

public:
	/**
	* Set the focal length associated with this clip. 
	* @note: Used for tracking. Does not update the StreamedCameraComponent. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	void SetFocalLength(float InFocalLength);
	
	/**
	* Set if this clip is 'selected'
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	void SetSelected(bool InSelected);

	/**
	* Set the name of the level that the clip was recorded in. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	void SetRecordedLevelName(FString InLevelName);

	/**
	* Set the initial frame of the clip used for calculating duration.
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	void SetFrameCountStart(int InFrame); 

	/**
	* Set the final frame of the clip used for calculating duration.
	*/
	UFUNCTION(BlueprintCallable, Category = "Clips")
	void SetFrameCountEnd(int InFrame);

private:
	/** The focal length of the streamed camera used to record the take */
	UPROPERTY()
	float FocalLength;

	/** Whether or not the take was marked as 'selected' */
	UPROPERTY()
	bool bIsSelected; 

	/** The name of the level that the clip was recorded in */
	UPROPERTY()
	FString RecordedLevelName; 

	/** The initial frame of the clip used for calculating duration. */
	UPROPERTY()
	int FrameCountStart;

	/** The last frame of the clip used for calculating duration. */
	UPROPERTY()
	int FrameCountEnd; 

};