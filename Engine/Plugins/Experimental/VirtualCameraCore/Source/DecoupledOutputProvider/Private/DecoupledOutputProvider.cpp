// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoupledOutputProvider.h"

#include "DecoupledOutputProviderModule.h"
#include "IOutputProviderLogic.h"

namespace UE::DecoupledOutputProvider::Private
{
	/** Handles calling the of the super function. */
	class FOutputProviderEvent : public IOutputProviderEvent
	{
		UDecoupledOutputProvider& OutputProvider;
		bool bCalledSuper = false;
		const TFunctionRef<void()> SuperFunc;
	public:

		FOutputProviderEvent(UDecoupledOutputProvider& OutputProvider, TFunctionRef<void()> SuperFunc)
			: OutputProvider(OutputProvider)
			, SuperFunc(MoveTemp(SuperFunc))
		{}

		virtual ~FOutputProviderEvent() override
		{
			if (!bCalledSuper)
			{
				SuperFunc();
			}
		}
		
		virtual void ExecuteSuperFunction() override
		{
			bCalledSuper = true;
			SuperFunc();
		}
		
		virtual UDecoupledOutputProvider& GetOutputProvider() const override
		{
			return OutputProvider;
		}
	};
}

void UDecoupledOutputProvider::Initialize()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::Initialize(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnInitialize(EventScope);
}

void UDecoupledOutputProvider::Deinitialize()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::Deinitialize(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnDeinitialize(EventScope);
}

void UDecoupledOutputProvider::Tick(const float DeltaTime)
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this, DeltaTime](){ Super::Tick(DeltaTime); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnTick(EventScope, DeltaTime);
}

void UDecoupledOutputProvider::OnActivate()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::OnActivate(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnActivate(EventScope);
}

void UDecoupledOutputProvider::OnDeactivate()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::OnDeactivate(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnDeactivate(EventScope);
}

void UDecoupledOutputProvider::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	using namespace UE::DecoupledOutputProvider::Private;
	UDecoupledOutputProvider* CastThis = CastChecked<UDecoupledOutputProvider>(InThis);
	const auto SuperFunc = [InThis, &Collector](){ Super::AddReferencedObjects(InThis, Collector); };
	FOutputProviderEvent EventScope(*CastThis, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnAddReferencedObjects(EventScope, Collector);
}

void UDecoupledOutputProvider::BeginDestroy()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::BeginDestroy(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnBeginDestroy(EventScope);
}

void UDecoupledOutputProvider::Serialize(FArchive& Ar)
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this, &Ar](){ Super::Serialize(Ar); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnSerialize(EventScope, Ar);
}

void UDecoupledOutputProvider::PostLoad()
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this](){ Super::PostLoad(); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnPostLoad(EventScope);
}

#if WITH_EDITOR
void UDecoupledOutputProvider::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	using namespace UE::DecoupledOutputProvider::Private;
	const auto SuperFunc = [this, &PropertyChangedEvent](){ Super::PostEditChangeProperty(PropertyChangedEvent); };
	FOutputProviderEvent EventScope(*this, SuperFunc);
	FDecoupledOutputProviderModule::Get().OnPostEditChangeProperty(EventScope, PropertyChangedEvent);
}
#endif