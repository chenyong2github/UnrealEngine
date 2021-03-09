// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "4MLSpace.h"
#include "4MLAgentElement.generated.h"


class U4MLAgent;
struct F4MLDescription;

UCLASS(abstract)
class UE4ML_API U4MLAgentElement : public UObject
{
	GENERATED_BODY()
public:
	U4MLAgentElement(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	const U4MLAgent& GetAgent() const;
	uint32 GetElementID() const { return ElementID; }
	const FString& GetNickname() const { return Nickname; }

	/** @return owning agent's avatar */
	AActor* GetAvatar() const;

	/** @return the pawn associated with the owning agent. If owning agent's avatar 
	 *	is a pawn then that gets retrieved, if not the function will check if 
	 *	it's a controller and if so retrieve its pawn */
	APawn* GetPawnAvatar() const;

	/** @return the controller associated with the owning agent. If owning agent's 
	 *	avatar is a controller then that gets retrieved, if not the function will 
	 *	check if it's a pawn and if so retrieve its controller */
	AController* GetControllerAvatar() const;

	/** Fetches both the pawn and the controller associated with the current agent.
	 *	It's like both calling @see GetPawnAvatar and @see GetControllerAvatar 
	 *	@return true if at least one of the fetched pair is non-null */
	bool GetPawnAndControllerAvatar(APawn*& OutPawn, AController*& OutController) const;

	virtual FString GetDescription() const { return Description; }

	/** Called before object's destruction. Can be called as part of new agent 
	 *	config application when old actuator get destroyed */
	virtual void Shutdown() {}
	
	void SetNickname(const FString& NewNickname) { Nickname = NewNickname; }
	virtual void Configure(const TMap<FName, FString>& Params);
	virtual void OnAvatarSet(AActor* Avatar) {}

	virtual void UpdateSpaceDef();
	const F4ML::FSpace& GetSpaceDef() const { return *SpaceDef; }

#if WITH_GAMEPLAY_DEBUGGER
	virtual void DescribeSelfToGameplayDebugger(class FGameplayDebuggerCategory& DebuggerCategory) const;
#endif // WITH_GAMEPLAY_DEBUGGER

protected:
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const PURE_VIRTUAL(U4MLAgentElement::ConstructSpaceDef, return MakeShareable(new F4ML::FSpace_Dummy()); );

protected:
	// can be queried by remote clients 
	FString Description;

	TSharedRef<F4ML::FSpace> SpaceDef;

	/** @note this is not a common counter, meaning Sensors and Actuators (for 
	 *	example) track the ID separately */
	UPROPERTY()
	uint32 ElementID;

	/** User-configured name for this element, mostly for debugging purposes but 
	 *	comes in handy when fetching observation/action spaces descriptions.
	 *	Defaults to UE4 instance name*/
	UPROPERTY()
	FString Nickname; 

#if WITH_GAMEPLAY_DEBUGGER
	// displayed in debugging tools and logging
	mutable FString DebugRuntimeString;
#endif // WITH_GAMEPLAY_DEBUGGER
};

struct FAgentElementSort
{
	bool operator()(const U4MLAgentElement* A, const U4MLAgentElement* B) const
	{
		return A && (!B || (A->GetElementID() < B->GetElementID()));
	}
	bool operator()(const U4MLAgentElement& A, const U4MLAgentElement& B) const
	{
		return (A.GetElementID() < B.GetElementID());
	}
};
