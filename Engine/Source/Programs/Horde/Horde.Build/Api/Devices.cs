// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using HordeServer.Models;

namespace HordeServer.Api
{

	/// <summary>
	/// Create device platform request
	/// </summary>
	public class CreateDevicePlatformRequest
	{
		/// <summary>
		/// The name of the platform
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

	}

	/// <summary>
	/// Create device platform response object
	/// </summary>
	public class CreateDevicePlatformResponse
	{
		/// <summary>
		/// Id of newly created platform
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public CreateDevicePlatformResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Update requesty object for a device platform
	/// </summary>
	public class UpdateDevicePlatformRequest
	{
		/// <summary>
		/// The vendor model ids for the platform
		/// </summary>
		public string[] ModelIds { get; set; } = null!;
	}


	/// <summary>
	/// Get object response which describes a device platform
	/// </summary>
	public class GetDevicePlatformResponse
	{
		/// <summary>
		/// Unique id of device platform
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Friendly name of device platform
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Platform vendor models
		/// </summary>
		public string[] ModelIds { get; set; }

		/// <summary>
		/// Response constructor
		/// </summary>
		public GetDevicePlatformResponse(string Id, string Name, string[] ModelIds)
		{
			this.Id = Id;
			this.Name = Name;
			this.ModelIds = ModelIds;
		}

	}

	/// <summary>
	/// Device pool creation request object
	/// </summary>
	public class CreateDevicePoolRequest
	{
		/// <summary>
		/// The name for the new pool
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// The name for the new pool
		/// </summary>
		[Required]
		public DevicePoolType PoolType { get; set; }

		/// <summary>
		/// Projects associated with this device pool
		/// </summary>
		public List<string>? ProjectIds { get; set; }

	}

	/// <summary>
	/// Device pool update request object
	/// </summary>
	public class UpdateDevicePoolRequest
	{
		/// <summary>
		/// Id of the device pool to update
		/// </summary>
		public string Id { get; set; } = null!;

		/// <summary>
		/// Projects associated with this device pool
		/// </summary>
		public List<string>? ProjectIds { get; set; }

	}

	/// <summary>
	/// Device pool creation response object
	/// </summary>
	public class CreateDevicePoolResponse
	{
		/// <summary>
		/// Id of the newly created device pool
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public CreateDevicePoolResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Device pool response object
	/// </summary>
	public class GetDevicePoolResponse
	{
		/// <summary>
		/// Id of  the device pool
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the device pool
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Type of the device pool
		/// </summary>
		public DevicePoolType PoolType { get; set; }

		/// <summary>
		/// Whether there is write access to the pool
		/// </summary>
		public bool WriteAccess { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetDevicePoolResponse(string Id, string Name, DevicePoolType PoolType, bool WriteAccess)
		{
			this.Id = Id;
			this.Name = Name;
            this.PoolType = PoolType;
			this.WriteAccess = WriteAccess;
        }
	}

	// Devices 

	/// <summary>
	/// Device creation request object
	/// </summary>
	public class CreateDeviceRequest
	{
		/// <summary>
		/// The platform of the device 
		/// </summary>
		[Required]
		public string PlatformId { get; set; } = null!;

		/// <summary>
		/// The pool to assign the device
		/// </summary>
		[Required]
		public string PoolId { get; set; } = null!;

		/// <summary>
		/// The friendly name of the device
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Whether to create the device in enabled state
		/// </summary>
		public bool? Enabled { get; set; }

		/// <summary>
		/// The network address of the device
		/// </summary>
		public string? Address { get; set; }

		/// <summary>
		/// The vendor model id of the device
		/// </summary>
		public string? ModelId { get; set; }

	}


	/// <summary>
	/// Device creation response object
	/// </summary>
	public class CreateDeviceResponse
	{
		/// <summary>
		/// The id of the newly created device
		/// </summary>
		[Required]
		public string Id { get; set; } = null!;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id"></param>
		public CreateDeviceResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Get response object which describes a device
	/// </summary>
	public class GetDeviceUtilizationResponse
	{
		/// <summary>
		/// The job id which utilized device
		/// </summary>
		public string? JobId { get; set;}

		/// <summary>
		/// The job's step id
		/// </summary>
		public string? StepId { get; set;}

		/// <summary>
		/// The time device was reserved
		/// </summary>
		public DateTime ReservationStartUtc { get; set;}

		/// <summary>
		/// The time device was freed
		/// </summary>
		public DateTime? ReservationFinishUtc { get; set;}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Telemetry"></param>
		public GetDeviceUtilizationResponse(DeviceUtilizationTelemetry Telemetry)
		{
			this.JobId = Telemetry.JobId;
			this.StepId = Telemetry.StepId;
			this.ReservationStartUtc = Telemetry.ReservationStartUtc;
			this.ReservationFinishUtc = Telemetry.ReservationFinishUtc;
		}

	}


	/// <summary>
	/// Get response object which describes a device
	/// </summary>
	public class GetDeviceResponse
	{
		/// <summary>
		/// The unique id of the device
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The platform of the device
		/// </summary>
		public string PlatformId { get; set; }

		/// <summary>
		/// The pool the device belongs to
		/// </summary>
		public string PoolId { get; set; }

		/// <summary>
		/// The friendly name of the device
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Whether the device is currently enabled
		/// </summary>
		public bool Enabled { get; set; }

		/// <summary>
		///  The address of the device (if it allows network connections)
		/// </summary>
		public string? Address { get; set; }

		/// <summary>
		/// The vendor model id of the device
		/// </summary>
		public string? ModelId { get; set; }

		/// <summary>
		/// Any notes provided for the device
		/// </summary>
		public string? Notes { get; set; }

		/// <summary>
		/// If the device has a marked problem
		/// </summary>
		public DateTime? ProblemTime { get; set; }

		/// <summary>
		/// If the device is in maintenance mode
		/// </summary>
		public DateTime? MaintenanceTime { get; set; }

		/// <summary>
		/// The user id that has the device checked out
		/// </summary>
		public string? CheckedOutByUserId { get; set; }

		/// <summary>
		/// The last time the device was checked out
		/// </summary>
		public DateTime? CheckOutTime { get; set; }

		/// <summary>
		/// The last user to modifiy the device
		/// </summary>
		public string? ModifiedByUser { get; set; }

		/// <summary>
		///  Device Utilization data
		/// </summary>
		public List<GetDeviceUtilizationResponse>? Utilization { get; set;}


	/// <summary>
	/// Device response constructor
	/// </summary>
	public GetDeviceResponse(string Id, string PlatformId, string PoolId, string Name, bool Enabled, string? Address, string? ModelId, string? ModifiedByUser, string? Notes, DateTime? ProblemTime, DateTime? MaintenanceTime, List<DeviceUtilizationTelemetry>? Utilization, string? CheckedOutByUser = null, DateTime? CheckOutTime = null)
		{
			this.Id = Id;
			this.Name = Name;
			this.PlatformId = PlatformId;
			this.PoolId = PoolId;
			this.Enabled = Enabled;
			this.Address = Address;
			this.ModelId = ModelId;
            this.ModifiedByUser = ModifiedByUser;
            this.Notes = Notes;
			this.ProblemTime = ProblemTime;
			this.MaintenanceTime = MaintenanceTime;
			this.Utilization = Utilization?.Select(U => new GetDeviceUtilizationResponse(U)).ToList();
            this.CheckedOutByUserId = CheckedOutByUser;
            this.CheckOutTime = CheckOutTime;
        }
	}

	/// <summary>
	/// Device update request object
	/// </summary>
	public class UpdateDeviceRequest
	{
		/// <summary>
		/// The device pool id
		/// </summary>
		public string? PoolId { get; set; }

		/// <summary>
		/// The device name
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// IP address or hostname of device
		/// </summary>
		public string? Address { get; set; }

		/// <summary>
		/// Device vendor model id
		/// </summary>
		public string? ModelId { get; set; }

		/// <summary>
		/// Markdown notes
		/// </summary>
		public string? Notes { get; set; }

		/// <summary>
		/// Whether device is enabled
		/// </summary>
		public bool? Enabled { get; set; }

		/// <summary>
		/// Whether the device is in maintenance mode
		/// </summary>
		public bool? Maintenance { get; set; }

		/// <summary>
		/// Whether to set or clear any device problem state
		/// </summary>
		public bool? Problem { get; set; }
	}

	/// <summary>
	/// Device checkout request object
	/// </summary>
	public class CheckoutDeviceRequest
	{
		/// <summary>
        /// Whether to checkout or in the device
        /// </summary>
        public bool Checkout { get; set; }
    }


	/// <summary>
	/// Device reservation request object
	/// </summary>
	public class DeviceReservationRequest
	{
		/// <summary>
		/// Device reservation platform id
		/// </summary>
		[Required]
		public string PlatformId { get; set; } = null!;

		/// <summary>
		/// The optional vendor model ids to include for this device
		/// </summary>
		public List<string>? IncludeModels { get; set; }

		/// <summary>
		/// The optional vendor model ids to exclude for this device
		/// </summary>
		public List<string>? ExcludeModels { get; set; }

	}

	/// <summary>
	/// Reservation request object
	/// </summary>
	public class CreateDeviceReservationRequest
	{
		/// <summary>
		/// What pool to reserve devices in
		/// </summary>
		[Required]
		public string PoolId { get; set; } = null!;

		/// <summary>
		/// Devices to reserve
		/// </summary>
		[Required]
		public List<DeviceReservationRequest> Devices { get; set; } = null!;

	}

	/// <summary>
	/// Device reservation response object
	/// </summary>
	public class CreateDeviceReservationResponse
	{
		/// <summary>
		/// The reservation id of newly created reservation
		/// </summary>
		public string Id { get; set; } = null!;

		/// <summary>
		/// The devices that were reserved
		/// </summary>
		public List<GetDeviceResponse> Devices { get; set; } = null!;

	}

	/// <summary>
	/// A reservation containing one or more devices
	/// </summary>
	public class GetDeviceReservationResponse
	{
		/// <summary>
		/// Randomly generated unique id for this reservation
		/// </summary>
		[Required]
		public string Id { get; set; } = null!;

		/// <summary>
		/// Which device pool the reservation is in
		/// </summary>
		[Required]
		public string PoolId { get; set; } = null!;

		/// <summary>
		/// The reserved devices
		/// </summary>
		[Required]
		public List<string> Devices { get; set; } = null!;

		/// <summary>
		/// JobID holding reservation
		/// </summary>
		public string? JobId { get; set; }

		/// <summary>
		/// Job step id holding reservation
		/// </summary>
		public string? StepId { get; set; }

		/// <summary>
		/// Reservations held by a user, requires a token
		/// </summary>
		public string? UserId { get; set; }

		/// <summary>
		/// The hostname of machine holding reservation
		/// </summary>
		public string? Hostname { get; set; }

		/// <summary>
		/// The optional reservation details 
		/// </summary>
		public string? ReservationDetails { get; set; }

		/// <summary>
		/// The UTC time when the reservation was created
		/// </summary>
		public DateTime CreateTimeUtc { get; set; }

		/// <summary>
		/// The legacy reservation system guid, to be removed once can update Gauntlet client in all streams
		/// </summary>
		public string LegacyGuid { get; set; } = null!;
	}


	// Legacy clients

	/// <summary>
	/// Reservation request for legacy clients
	/// </summary>
	public class LegacyCreateReservationRequest
	{
		/// <summary>
		/// The device types to reserve, these are mapped to platforms
		/// </summary>
		[Required]
		public string[] DeviceTypes { get; set; } = null!;

		/// <summary>
		/// The hostname of machine reserving devices 
		/// </summary>
		[Required]
		public string Hostname { get; set; } = null!;

		/// <summary>
		/// The duration of reservation
		/// </summary>
		[Required]
		public string Duration { get; set; } = null!;

		/// <summary>
		/// Reservation details string
		/// </summary>
		public string? ReservationDetails { get; set; } = null;

		/// <summary>
		/// The PoolId of reservation request 
		/// </summary>
		public string? PoolId { get; set; } = null;

		/// <summary>
		/// The JobId of reservation request 
		/// </summary>
		public string? JobId { get; set; } = null;

		/// <summary>
		/// The StepId of reservation request 
		/// </summary>
		public string? StepId { get; set; } = null;

	}

	/// <summary>
	/// Reservation response for legacy clients
	/// </summary>
	public class GetLegacyReservationResponse
	{
		/// <summary>
		/// The names of the devices that were reserved
		/// </summary>
		[Required]
		public string[] DeviceNames { get; set; } = null!;

		/// <summary>
		/// The corresponding perf specs of the reserved devices
		/// </summary>
		[Required]
		public string[] DevicePerfSpecs { get; set; } = null!;

		/// <summary>
		/// The host name of the machine making the reservation
		/// </summary>
		[Required]
		public string HostName { get; set; } = null!;

		/// <summary>
		/// The start time of the reservation
		/// </summary>
		[Required]
		public string StartDateTime { get; set; } = null!;

		/// <summary>
		/// The duration of the reservation (before renew)
		/// </summary>
		[Required]
		public string Duration { get; set; } = null!;

		/// <summary>
		/// The JobId of reservation request 
		/// </summary>
		public string? JobId { get; set; } = null;

		/// <summary>
		/// The StepId of reservation request 
		/// </summary>
		public string? StepId { get; set; } = null;


		/// <summary>
		/// The legacy guid of the reservation
		/// </summary>
		[Required]
		[SuppressMessage("Compiler", "CA1720: Identifier 'Guid' contains type name")]
		public string Guid { get; set; } = null!;
	}

	/// <summary>
	/// Device response for legacy clients
	/// </summary>
	public class GetLegacyDeviceResponse
	{
		/// <summary>
		/// The  name of the reserved device
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// The (legacy) type of the device, mapped from platform id
		/// </summary>
		[Required]
		public string Type { get; set; } = null!;

		/// <summary>
		/// The IP or hostname of device
		/// </summary>
		[Required]
		public string IPOrHostName { get; set; } = null!;

		/// <summary>
		/// The (legacy) perf spec of the device
		/// </summary>
		[Required]
		public string PerfSpec { get; set; } = null!;

		/// <summary>
		/// The available start time which is parsed client side
		/// </summary>
		[Required]
		public string AvailableStartTime { get; set; } = null!;

		/// <summary>
		/// The available end time which is parsed client side
		/// </summary>
		[Required]
		public string AvailableEndTime { get; set; } = null!;

		/// <summary>
		/// Whether device is enabled
		/// </summary>
		[Required]
		public bool Enabled { get; set; }

		/// <summary>
		/// Associated device data
		/// </summary>
		[Required]
		public string DeviceData { get; set; } = null!;
	}

	/// <summary>
	/// Platform mapping update for V1 devices
	/// </summary>
	public class UpdatePlatformMapRequest
	{
		/// <summary>
		/// Platform name => platform id
		/// </summary>
		[Required]
		public Dictionary<string, string> PlatformMap { get; set; } = null!;

		/// <summary>
		/// Platform id => name
		/// </summary>
		[Required]
		public Dictionary<string, string> PlatformReverseMap { get; set; } = null!;

		/// <summary>
		/// Perfspec v1 => model names
		/// </summary>
		[Required]
		public Dictionary<string, string> PerfSpecHighMap { get; set; } = null!;

	}


}
