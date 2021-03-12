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
		
		/** Set a parameter (bool)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - bool value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, bool InValue) override;

		/** Set a parameter (bool array)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - bool array r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, TArray<bool>&& InValue) override;		

		/** Set a parameter (int32)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - in32 value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, int32 InValue) override;

		/** Set a parameter overloads (int32 array)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - int32 array r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, TArray<int32>&& InValue) override;		

		/** Set a parameter overloads (float)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - float value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, float InValue) override;

		/** Set a parameter overloads (float array)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - float array r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, TArray<float>&& InValue) override;
		
		/** Set a parameter overloads (string)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - string r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, FString&& InValue) override;

		/** Set a parameter overloads (string array)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - string array r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, TArray<FString>&& InValue) override;
		
		/** Set a parameter overloads (Audio::IProxyData)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Audio::IProxyData r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, Audio::IProxyDataPtr&& ) override;

		/** Set a parameter overloads (Audio::IProxyData array)
		 *
		 * @param InParameterName - Name of MetaSound instance parameter.
		 * @param InValue - Audio::IProxyData array r-value value used to construct parameter value.
		 *
		 * @return true on success, false on failure.
		 */
		bool SetParameter(const FName& InName, TArray<Audio::IProxyDataPtr>&& InValue) override;

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

