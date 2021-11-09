// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using DeviceId = StringId<IDevice>;
	using DevicePlatformId = StringId<IDevicePlatform>;
	using DevicePoolId = StringId<IDevicePool>;
	using UserId = ObjectId<IUser>;
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Device reservation request data
	/// </summary>
	public class DeviceRequestData
	{
		/// <summary>
		/// The platform of the device to reserve
		/// </summary>
		public DevicePlatformId PlatformId { get; set; }


		/// <summary>
		/// Models to include for this request
		/// </summary>
		public List<string> IncludeModels { get; set; }

		/// <summary>
		/// Models to exclude for this request
		/// </summary>
		public List<string> ExcludeModels { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public DeviceRequestData(DevicePlatformId PlatformId, List<string>? IncludeModels = null, List<string>? ExcludeModels = null)
		{
			this.PlatformId = PlatformId;
			this.IncludeModels = IncludeModels ?? new List<string>();
			this.ExcludeModels = ExcludeModels ?? new List<string>();
		}
	}

	/// <summary>
	/// A collection for device management
	/// </summary>
	public interface IDeviceCollection
	{
		// PLATFORMS

		/// <summary>
		/// Add a platform 
		/// </summary>
		/// <param name="Id">The id of the platform</param>
		/// <param name="Name">The friendly name of the platform</param>
		Task<IDevicePlatform?> TryAddPlatformAsync(DevicePlatformId Id, string Name);

		/// <summary>
		/// Get a list of all available device platforms
		/// </summary>		
		Task<List<IDevicePlatform>> FindAllPlatformsAsync();

		/// <summary>
		/// Get a specific platform by id
		/// </summary>
		Task<IDevicePlatform?> GetPlatformAsync(DevicePlatformId PlatformId);

		/// <summary>
		/// Update a device platform
		/// </summary>
		/// <param name="platformId">The id of the device</param>
		/// <param name="ModelIds">The available model ids for the platform</param>
		Task<bool> UpdatePlatformAsync(DevicePlatformId platformId, string[]? ModelIds);

		// POOLS

		/// <summary>
		/// Add a new device pool to the collection
		/// </summary>
		/// <param name="Id">The id of the new pool</param>
		/// <param name="Name">The friendly name of the new pool</param>
		/// <param name="PoolType">The pool type</param>
		/// <param name="ProjectIds">Projects associated with this pool</param>
		Task<IDevicePool?> TryAddPoolAsync(DevicePoolId Id, string Name, DevicePoolType PoolType, List<ProjectId>? ProjectIds );

		/// <summary>
		/// Update a device pool
		/// </summary>
		/// <param name="Id">The id of the device pool to update</param>
		/// <param name="ProjectIds">Associated project ids</param>
		Task UpdatePoolAsync(DevicePoolId Id, List<ProjectId>? ProjectIds);

		/// <summary>
		/// Get a pool by id
		/// </summary>
		Task<IDevicePool?> GetPoolAsync(DevicePoolId PoolId);

		/// <summary>
		/// Gets a list of existing device pools
		/// </summary>		
		Task<List<IDevicePool>> FindAllPoolsAsync();

		// DEVICES

		/// <summary>
		/// Get a device by id
		/// </summary>
		Task<IDevice?> GetDeviceAsync(DeviceId DeviceId);

		/// <summary>
		/// Get a device by name
		/// </summary>
		Task<IDevice?> GetDeviceByNameAsync(string DeviceName);

		/// <summary>
		/// Get a list of all devices
		/// </summary>
		/// <param name="DeviceIds">Optional list of device ids to get</param>
		Task<List<IDevice>> FindAllDevicesAsync(List<DeviceId>? DeviceIds);

		/// <summary>
		/// Adds a new device
		/// </summary>
		/// <param name="Id">The device id</param>
		/// <param name="Name">The name of the device (unique)</param>
		/// <param name="PlatformId">The platform of the device</param>
		/// <param name="PoolId">Which pool to add it to</param>
		/// <param name="Enabled">Whather the device is enabled by default</param>
		/// <param name="Address">The network address or hostname the device can be reached at</param>
		/// <param name="ModelId">The vendor model id of the device</param>
        /// <param name="UserId">The user adding the device</param>
		Task<IDevice?> TryAddDeviceAsync(DeviceId Id, string Name, DevicePlatformId PlatformId, DevicePoolId PoolId, bool? Enabled, string? Address, string? ModelId, UserId? UserId);

		/// <summary>
		/// Update a device
		/// </summary>
		/// <param name="DeviceId">The id of the device to update</param>
		/// <param name="NewPoolId">The new pool to assign</param>
		/// <param name="NewName">The new name to assign</param>		
		/// <param name="NewAddress">The new device address or hostname</param>
		/// <param name="NewModelId">The new model id</param>
		/// <param name="NewNotes">The devices markdown notes</param>
		/// <param name="NewEnabled">Whether the device is enabled or not</param>
		/// <param name="NewProblem">Whether to set or clear problem state</param>
		/// <param name="NewMaintenance">Whether to set or clear maintenance state</param>
        /// <param name="ModifiedByUserId">The user who is updating the device</param>
		Task UpdateDeviceAsync(DeviceId DeviceId, DevicePoolId? NewPoolId, string? NewName, string? NewAddress, string? NewModelId, string? NewNotes, bool? NewEnabled, bool? NewProblem, bool? NewMaintenance, UserId? ModifiedByUserId = null);

		/// <summary>
		/// Delete a device from the collection
		/// </summary>
		/// <param name="DeviceId">The id of the device to delete</param>
		Task<bool> DeleteDeviceAsync(DeviceId DeviceId);

		/// <summary>
        /// Checkout or checkin the specified device
        /// </summary>
        /// <param name="DeviceId"></param>
        /// <param name="CheckedOutByUserId"></param>
        /// <returns></returns>
        Task CheckoutDeviceAsync(DeviceId DeviceId, UserId? CheckedOutByUserId);

        // RESERVATIONS

        /// <summary>
        /// Create a new reseveration in the pool with the specified devices
        /// </summary>
        /// <param name="PoolId">The pool of devices to use for the new reservation</param>
        /// <param name="Request">The requested devices for the reservation</param>
        /// <param name="Hostname">The hostname of the machine making the reservation</param>
        /// <param name="ReservationDetails">The details of the reservation</param>
        /// <param name="JobId">The Job Id associated with the job</param>
        /// <param name="StepId">The Step Id associated with the job</param>
        Task<IDeviceReservation?> TryAddReservationAsync(DevicePoolId PoolId, List<DeviceRequestData> Request, string? Hostname, string? ReservationDetails, string? JobId, string? StepId);

		/// <summary>
		/// Gets a reservation by guid for legacy clients
		/// </summary>
		/// <param name="LegacyGuid">YThe legacy guid of the reservation</param>
		Task<IDeviceReservation?> TryGetReservationFromLegacyGuidAsync(string LegacyGuid);

        /// <summary>
        /// Gets a reservation by device id
        /// </summary>
        /// <param name="Id">A device contained in reservation</param>
        Task<IDeviceReservation?> TryGetDeviceReservationAsync(DeviceId Id);

        /// <summary>
        /// Get a list of all reservations
        /// </summary>
        Task<List<IDeviceReservation>> FindAllReservationsAsync();

		/// <summary>
		/// Updates a reservation to the current time, for expiration
		/// </summary>
		/// <param name="Id">The id of the reservation to update</param>
		public Task<bool> TryUpdateReservationAsync(ObjectId Id);

		/// <summary>
		/// Deletes a reservation and releases reserved devices
		/// </summary>
		/// <param name="Id">The id of the reservation to delete</param>
		public Task<bool> DeleteReservationAsync(ObjectId Id);

		/// <summary>
		/// Deletes expired reservations
		/// </summary>
		public Task<bool> ExpireReservationsAsync();

	}

}