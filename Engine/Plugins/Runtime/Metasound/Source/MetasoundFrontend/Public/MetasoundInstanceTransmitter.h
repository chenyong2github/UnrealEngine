// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioExtensionPlugin.h"
#include "MetasoundDataReference.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"

namespace Metasound
{
	/** FMetasoundInstanceTransmitter provides a communication interface for 
	 * sending values to a MetaSound instance. It relies on the send/receive transmission
	 * system to ferry data from the transmitter to the MetaSound instance. Data will
	 * be safely ushered across thread boundaries in scenarios where the instance
	 * transmitter and metasound instance live on different threads. 
	 */
	class METASOUNDFRONTEND_API FMetasoundInstanceTransmitter : public IAudioInstanceTransmitter
	{
		FMetasoundInstanceTransmitter(const FMetasoundInstanceTransmitter&) = delete;
		FMetasoundInstanceTransmitter& operator=(const FMetasoundInstanceTransmitter&) = delete;
	public:
		
		/** FSendInfo describes the MetaSounds input parameters as well as the 
		 * necessary information to route data to the instances inputs. 
		 */
		struct FSendInfo
		{
			/** Global address of instance input. */
			FSendAddress Address;

			/** Name of parameter on MetaSound instance. */
			FName ParameterName;

			/** Type name of parameter on MetaSound instance. */
			FName TypeName;
		};

		/** Initialization parameters for a FMetasoundInstanceTransmitter. */
		struct FInitParams
		{
			/** FOperatorSettings must match the operator settings of the MetaSound 
			 * instance to ensure proper operation. */
			FOperatorSettings OperatorSettings;

			/** ID of the MetaSound instance.  */
			uint64 InstanceID;

			/** Available input parameters on MetaSound instance. */
			TArray<FSendInfo> Infos;

			FInitParams(const FOperatorSettings& InSettings, uint64 InInstanceID, const TArray<FSendInfo>& InInfos=TArray<FSendInfo>())
			: OperatorSettings(InSettings)
			, InstanceID(InInstanceID)
			, Infos(InInfos)
			{
			}

		};

		FMetasoundInstanceTransmitter(const FMetasoundInstanceTransmitter::FInitParams& InInitParams);
		virtual ~FMetasoundInstanceTransmitter() = default;

		/** Returns ID of the MetaSound instance associated with this transmitter. */
		uint64 GetInstanceID() const override;

		/** Set a float parameter on the MetaSound instance by name. 
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Value to set. 
		 *
		 * @return true on success, false on failure.
		 */
		bool SetFloatParameter(const FName& InParameterName, float InValue) override;

		/** Set a int parameter on the MetaSound instance by name. 
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Value to set. 
		 *
		 * @return true on success, false on failure.
		 */
		bool SetIntParameter(const FName& InParameterName, int32 InValue) override;

		/** Set a bool parameter on the MetaSound instance by name. 
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Value to set. 
		 *
		 * @return true on success, false on failure.
		 */
		bool SetBoolParameter(const FName& InParameterName, bool InValue) override;
		
		/** Set a parameter using a literal.
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Literal value used to construct paramter value. 
		 *
		 * @return true on success, false on failure. 
		 */
		bool SetParameterWithLiteral(const FName& InParameterName, const FLiteral& InValue);

		/** Duplicate this transmitter interface. The transmitters association with
		 * the MetaSound instance will be maintained. */
		TUniquePtr<IAudioInstanceTransmitter> Clone() const override;

	private:
		// Find FSendInfo by parameter name. 
		const FSendInfo* FindSendInfo(const FName& InParameterName) const;

		// Find ISender by parameter name. 
		ISender* FindSender(const FName& InParameterName);

		// Create and store a new ISender for the given FSendInfo.
		ISender* AddSender(const FSendInfo& InInfo);

		// Create a new ISender from FSendInfo.
		TUniquePtr<ISender> CreateSender(const FSendInfo& InInfo) const;

		TArray<FSendInfo> SendInfos;
		FOperatorSettings OperatorSettings;
		uint64 InstanceID;

		TMap<FName, TUniquePtr<ISender>> InputSends;
	};
}

