// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE_NP {

// Unique server assigned ID for a network sim instance
struct FNetworkPredictionAsyncID
{
	FNetworkPredictionAsyncID() = default;
	explicit FNetworkPredictionAsyncID(int32 InSpawnID) : SpawnID(InSpawnID) { }

	bool IsValid() const { return SpawnID != 0; }
	void Reset() { SpawnID = 0; }
	bool IsClientGenerated() const { return SpawnID < 0; }

	operator int32() const { return SpawnID; }
	bool operator<  (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID < rhs.SpawnID); }
	bool operator<= (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID <= rhs.SpawnID); }
	bool operator>  (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID > rhs.SpawnID); }
	bool operator>= (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID >= rhs.SpawnID); }
	bool operator== (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID == rhs.SpawnID); }
	bool operator!= (const FNetworkPredictionAsyncID &rhs) const { return(this->SpawnID != rhs.SpawnID); }

private:
	int32 SpawnID=0;
};

} // namespace UE_NP