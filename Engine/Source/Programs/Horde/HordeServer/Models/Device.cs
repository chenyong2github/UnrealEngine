// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using MongoDB.Bson;
using System.Collections.Generic;

using DeviceId = HordeServer.Utilities.StringId<HordeServer.Models.IDevice>;
using DevicePlatformId = HordeServer.Utilities.StringId<HordeServer.Models.IDevicePlatform>;
using DevicePoolId = HordeServer.Utilities.StringId<HordeServer.Models.IDevicePool>;

namespace HordeServer.Models
{
	
	/// <summary>
	/// A reservation containing one or more devices
	/// </summary>
	public interface IDeviceReservation
	{
		/// <summary>
		/// Randomly generated unique id for this reservation
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// Which device pool the reservation is in
		/// </summary>
		public DevicePoolId PoolId { get; }

		/// <summary>
		/// JobID holding reservation
		/// </summary>
		public ObjectId? JobId { get; }

		/// <summary>
		/// Job step id holding reservation
		/// </summary>
		public SubResourceId? StepId { get; }

		/// <summary>
		/// Reservations held by a user, requires a token
		/// </summary>
		public ObjectId? UserId { get; }

		/// <summary>
		/// The hostname of machine holding reservation
		/// </summary>
		string? Hostname { get; }

		/// <summary>
		/// The hostname of machine holding reservation
		/// </summary>
		string? ReservationDetails { get; }

		/// <summary>
		/// The UTC time when the reservation was created
		/// </summary>
		DateTime CreateTimeUtc { get; }

		/// <summary>
		/// The last update time for reservation renewal (for expiration)
		/// </summary>
		DateTime UpdateTimeUtc { get; }

		/// <summary>
		/// The reserved devices
		/// </summary>
		public List<DeviceId> Devices { get; }

		/// <summary>
		/// The legacy reservation system guid, to be removed once can update Gauntlet client in all streams
		/// </summary>
		public string LegacyGuid { get; }
	}


	/// <summary>
	/// A device platform 
	/// </summary>
	public interface IDevicePlatform
	{
		/// <summary>
		/// Unique identifier of device platform 
		/// </summary>
		public DevicePlatformId Id { get; }
		
		/// <summary>
		/// Device platform name, for example Android, PS5, etc
		/// </summary>
		string Name { get; }

		/// <summary>
		/// A list of valid models for the platform
		/// </summary>
		public IReadOnlyList<string>? Models { get; }

	}


	/// <summary>
	/// A logical pool of devices
	/// </summary>
	public interface IDevicePool
	{
		/// <summary>
		/// Unique identifier of pool
		/// </summary>
		public DevicePoolId Id { get; }

		/// <summary>
		/// Friendly name of the pool
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Acl access to the pool
		/// </summary>
		public Acl? Acl { get; }
	}

	/// <summary>
	/// A physical device
	/// </summary>
	public interface IDevice
	{
		/// <summary>
		/// The unique id of the device
		/// </summary>
		public DeviceId Id { get; }

		/// <summary>
		/// The platform of the device
		/// </summary>
		public DevicePlatformId PlatformId { get; }

		/// <summary>
		/// Which pool the device belongs to
		/// </summary>
		public DevicePoolId PoolId { get; }

		/// <summary>
		/// Friendly name of the device
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// The model of the device
		/// </summary>
		public string? ModelId { get; }


		/// <summary>
		/// Address if the device supports shared network connections
		/// </summary>
		public string? Address { get; }

		/// <summary>
		/// Whether the device is currently enabled
		/// </summary>
		public bool Enabled { get; }

		/// <summary>
		/// Name of the user that last modified this device
		/// </summary>
		public string? ModifiedByUser { get; }

		/// <summary>
		/// The last time a problem was reported
		/// </summary>
		public DateTime? ProblemTimeUtc { get; }

		/// <summary>
		/// The time device was marked for maintenance
		/// </summary>
		public DateTime? MaintenanceTimeUtc { get; }

		/// <summary>
		/// Markdown notes for device if any 
		/// </summary>
		public string? Notes { get; }

		/// <summary>
		/// ACL for modifying this device
		/// </summary>
		public Acl? Acl { get; }
	}
}







