// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;

using HordeServer.Models;
using HordeServer.Collections;
using System;
using HordeServer.Utilities;
using System.Threading;

using Microsoft.Extensions.Logging;
using HordeCommon;
using MongoDB.Bson;
using System.Collections.Generic;

using DeviceId = HordeServer.Utilities.StringId<HordeServer.Models.IDevice>;
using DevicePlatformId = HordeServer.Utilities.StringId<HordeServer.Models.IDevicePlatform>;
using DevicePoolId = HordeServer.Utilities.StringId<HordeServer.Models.IDevicePool>;
using System.Security.Claims;
using System.Diagnostics.CodeAnalysis;

namespace HordeServer.Services
{
	/// <summary>
	/// Device management service
	/// </summary>
	public class DeviceService : TickedBackgroundService
	{
		/// <summary>
		/// The ACL service instance
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Log output writer
		/// </summary>
		ILogger<DeviceService> Logger;

		/// <summary>
		/// Device collection
		/// </summary>
		IDeviceCollection Devices;

		/// <summary>
		/// Device service constructor
		/// </summary>
		public DeviceService(IDeviceCollection Devices, AclService AclService, ILogger<DeviceService> Logger)
			: base(TimeSpan.FromMinutes(1.0), Logger)
		{
			this.Devices = Devices;
			this.AclService = AclService;
			this.Logger = Logger;
		}

		/// <summary>
		/// Ticks service
		/// </summary>
		protected override async Task TickAsync(CancellationToken StoppingToken)
		{

			if (!StoppingToken.IsCancellationRequested)
			{
				await Devices.ExpireReservationsAsync();
			}

		}

		/// <summary>
		/// Create a new device platform
		/// </summary>
		public Task<IDevicePlatform?> TryCreatePlatformAsync(DevicePlatformId Id, string Name)
		{
			return Devices.TryAddPlatformAsync(Id, Name);
		}

		/// <summary>
		/// Get a list of existing device platforms
		/// </summary>
		public Task<List<IDevicePlatform>> GetPlatformsAsync()
		{
			return Devices.FindAllPlatformsAsync();
		}

		/// <summary>
		/// Update an existing platform
		/// </summary>
		public Task<bool> UpdatePlatformAsync(DevicePlatformId PlatformId, string[]? ModelIds)
		{
			return Devices.UpdatePlatformAsync(PlatformId, ModelIds);
		}

		/// <summary>
		/// Get a specific device platform
		/// </summary>
		public Task<IDevicePlatform?> GetPlatformAsync(DevicePlatformId Id)
		{
			return Devices.GetPlatformAsync(Id);
		}

		/// <summary>
		/// Get a device pool by id
		/// </summary>
		public Task<IDevicePool?> GetPoolAsync(DevicePoolId Id)
		{
			return Devices.GetPoolAsync(Id);
		}

		/// <summary>
		/// Create a new device pool
		/// </summary>
		public Task<IDevicePool?> TryCreatePoolAsync(DevicePoolId Id, string Name)
		{
			return Devices.TryAddPoolAsync(Id, Name);
		}

		/// <summary>
		/// Get a list of existing device pools
		/// </summary>
		public Task<List<IDevicePool>> GetPoolsAsync()
		{
			return Devices.FindAllPoolsAsync();
		}

		/// <summary>
		/// Get a list of devices, optionally filtered to provided ids
		/// </summary>
		public Task<List<IDevice>> GetDevicesAsync(List<DeviceId>? DeviceIds = null)
		{
			return Devices.FindAllDevicesAsync(DeviceIds);
		}

		/// <summary>
		/// Get a specific device
		/// </summary>
		public Task<IDevice?> GetDeviceAsync(DeviceId Id)
		{
			return Devices.GetDeviceAsync(Id);
		}

		/// <summary>
		/// Get a device by name
		/// </summary>
		public Task<IDevice?> GetDeviceByNameAsync(string DeviceName)
		{
			return Devices.GetDeviceByNameAsync(DeviceName);
		}

		/// <summary>
		/// Delete a device
		/// </summary>
		public async Task<bool> DeleteDeviceAsync(DeviceId Id)
		{
			return await Devices.DeleteDeviceAsync(Id);
		}

		/// <summary>
		/// Create a new device
		/// </summary>
		/// <param name="Id">Unique id of the device</param>
		/// <param name="Name">Friendly name of the device</param>
		/// <param name="PlatformId">The device platform</param>
		/// <param name="PoolId">Which pool to add the device</param>
		/// <param name="Enabled">Whether the device is enabled</param>
		/// <param name="Address">Address or hostname of device</param>
		/// <param name="ModelId">Vendor model id</param>
		/// <returns></returns>
		public Task<IDevice?> TryCreateDeviceAsync(DeviceId Id, string Name, DevicePlatformId PlatformId, DevicePoolId PoolId, bool? Enabled, string? Address, string? ModelId)
		{
			return Devices.TryAddDeviceAsync(Id, Name, PlatformId, PoolId, Enabled, Address, ModelId);
		}

		/// <summary>
		/// Update a device
		/// </summary>
		public Task UpdateDeviceAsync(DeviceId DeviceId, DevicePoolId? NewPoolId = null, string? NewName = null, string? NewAddress = null, string? NewModelId = null, string? NewNotes = null, bool? NewEnabled = null, bool? NewProblem = null, bool? NewMaintenance = null)
		{
			return Devices.UpdateDeviceAsync(DeviceId, NewPoolId, NewName, NewAddress, NewModelId, NewNotes, NewEnabled, NewProblem, NewMaintenance);
		}

		/// <summary>
		/// Try to create a reservation satisfying the specified device platforms and models
		/// </summary>
		public Task<IDeviceReservation?> TryCreateReservationAsync(DevicePoolId Pool, List<DeviceRequestData> Request, string? Hostname = null, string? ReservationDetails = null)
		{
			return Devices.TryAddReservationAsync(Pool, Request, Hostname, ReservationDetails);
		}

		/// <summary>
		/// Update/renew an existing reservation
		/// </summary>
		public Task<bool> TryUpdateReservationAsync(ObjectId Id)
		{
			return Devices.TryUpdateReservationAsync(Id);
		}

		/// <summary>
		///  Delete an existing reservation
		/// </summary>
		public Task<bool> DeleteReservationAsync(ObjectId Id)
		{
			return Devices.DeleteReservationAsync(Id);
		}

		/// <summary>
		/// Get a reservation from a legacy guid
		/// </summary>
		public Task<IDeviceReservation?> TryGetReservationFromLegacyGuidAsync(string LegacyGuid)
		{
			return Devices.TryGetReservationFromLegacyGuidAsync(LegacyGuid);
		}

		/// <summary>
		/// Get a list of existing device reservations
		/// </summary>
		public Task<List<IDeviceReservation>> GetReservationsAsync()
		{
			return Devices.FindAllReservationsAsync();
		}

		/// <summary>
		/// Authorize device action
		/// </summary>
		/// <param name="Action"></param>
		/// <param name="User"></param>
		/// <returns></returns>
		[SuppressMessage("Usage", "CA1801:Review unused parameters")]
		[SuppressMessage("Performance", "CA1822: Can be static ")]
		public Task<bool> AuthorizeAsync(AclAction Action, ClaimsPrincipal User)
		{
			// Setup ACL's for pool and platform access, https://jira.it.epicgames.com/browse/UE-117224/			
			// allow reads, though restrict writing to internal employees
			if (Action == AclAction.DeviceRead)
			{
				return Task.FromResult(true);
			}
			return Task.FromResult(User.IsInRole("Internal-Employees"));			
		}

	}
}