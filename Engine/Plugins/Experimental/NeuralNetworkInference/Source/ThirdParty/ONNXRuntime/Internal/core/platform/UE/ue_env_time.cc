// Copyright Epic Games, Inc. All Rights Reserved.

#include "core/platform/env_time.h"
#include "Misc/DateTime.h"
#include <algorithm>


namespace onnxruntime {

namespace {

class UnrealEnvironmentEnvTime : public EnvTime {

 public:

  UnrealEnvironmentEnvTime() 
  {
	  // Fixing the starting time to the Unix Epoch
	  UtcEpoch = FDateTime::FromUnixTimestamp(0);
  };

  uint64_t NowMicros() override 
  {
	  Diff = FDateTime::Now() - UtcEpoch;
	  uint64_t RetVal = static_cast<uint64_t>(Diff.GetTotalMicroseconds());
	  return RetVal;
  }

 private:
	 FDateTime UtcEpoch;
	 FTimespan Diff;
};

}  // namespace

EnvTime* EnvTime::Default() {
	static UnrealEnvironmentEnvTime DefaultTimeEnv;
	return &DefaultTimeEnv;
}

bool GetMonotonicTimeCounter(TIME_SPEC* Value) {
	FTimespan Diff = FDateTime::Now() - FDateTime::FromUnixTimestamp(0);
	*Value = static_cast<TIME_SPEC>(Diff.GetTotalMicroseconds());
	return true;
}

void SetTimeSpecToZero(TIME_SPEC* Value) {
	*Value = 0;
}

void AccumulateTimeSpec(TIME_SPEC* Base, const TIME_SPEC* Start, const TIME_SPEC* End) {
	*Base += std::max<TIME_SPEC>(0, *End - *Start);
}

//Return the interval in seconds.
//If the function fails, the return value is zero
double TimeSpecToSeconds(const TIME_SPEC* Value) {
	constexpr double Normalizer = 1 / 1000000.0;
	double RetVal = static_cast<double>(*Value) * Normalizer;
	return RetVal;
}

}  // namespace onnxruntime
