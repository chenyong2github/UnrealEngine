// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"
#include "LearningLog.h"

namespace UE::Learning
{
	/**
	* Training Device
	*/
	enum class ETrainerDevice : uint8
	{
		CPU = 0,
		GPU = 1,
	};

	/**
	* Type of response from a Trainer
	*/
	enum class ETrainerResponse : uint8
	{
		// The communication was successful
		Success = 0,

		// The communication send or received was unexpected
		Unexpected = 1,

		// Training is complete
		Completed = 2,

		// Training is stopped
		Stopped = 3,

		// The communication timed-out
		Timeout = 4,
	};

	namespace Trainer
	{
		/**
		* Default Timeout to use during communication.
		*/
		static constexpr float DefaultTimeout = 10.0f;

		/**
		* Default Log Settings to use during communication.
		*/
		static constexpr ELogSetting DefaultLogSettings = ELogSetting::Silent;

		/**
		* Default IP to use for networked training
		*/
		static constexpr const TCHAR* DefaultIp = TEXT("127.0.0.1");

		/**
		* Default Port to use for networked training
		*/
		static constexpr uint32 DefaultPort = 48491;

		/**
		* Converts a ETrainerDevice into a string.
		*/
		LEARNINGTRAINING_API const TCHAR* GetDeviceString(const ETrainerDevice Device);

		/**
		* Converts a ETrainerResponse into a string for use in logging and error messages.
		*/
		LEARNINGTRAINING_API const TCHAR* ResponseString(const ETrainerResponse Response);

		/**
		* Compute the discount factor that corresponds to a particular HalfLife and DeltaTime.
		*
		* @param HalfLife		Time by which the reward should be discounted by half
		* @param DeltaTime		DeltaTime taken upon each step of the environment
		* @returns				Corresponding discount factor
		*/
		LEARNINGTRAINING_API float DiscountFactorFromHalfLife(const float HalfLife, const float DeltaTime);

		/**
		* Compute the discount factor that corresponds to a particular HalfLife provided in terms of number of steps
		*
		* @param HalfLifeSteps	Number of steps taken at which the reward should be discounted by half
		* @returns				Corresponding discount factor
		*/
		LEARNINGTRAINING_API float DiscountFactorFromHalfLifeSteps(const int32 HalfLifeSteps);

#if WITH_EDITOR
		LEARNINGTRAINING_API FString DefaultEditorPythonExecutablePath();
		LEARNINGTRAINING_API FString DefaultEditorSitePackagesPath();
		LEARNINGTRAINING_API FString DefaultEditorPythonContentPath();
		LEARNINGTRAINING_API FString DefaultEditorIntermediatePath();
#endif
	}

}