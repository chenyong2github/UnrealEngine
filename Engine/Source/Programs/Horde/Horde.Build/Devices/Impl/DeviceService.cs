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
using System.Security.Claims;
using System.Diagnostics.CodeAnalysis;
using HordeServer.Notifications;
using System.Linq;
using HordeServer.Api;
using Microsoft.Extensions.Hosting;

namespace HordeServer.Services
{
	using DeviceId = StringId<IDevice>;
	using DevicePlatformId = StringId<IDevicePlatform>;
	using DevicePoolId = StringId<IDevicePool>;
	using JobId = ObjectId<IJob>;
	using UserId = ObjectId<IUser>;
	using ProjectId = StringId<IProject>;

	/// <summary>
	///  Device Pool Authorization (convenience class for pool ACL's)
	/// </summary>
	public class DevicePoolAuthorization
	{
		/// <summary>
		/// The device pool
		/// </summary>
		public IDevicePool Pool { get; private set; }

		/// <summary>
		///  Read access to pool
		/// </summary>
		public bool Read { get; private set; }

		/// <summary>
		///  Write access to pool
		/// </summary>
		public bool Write { get; private set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool"></param>
		/// <param name="Read"></param>
		/// <param name="Write"></param>
		public DevicePoolAuthorization(IDevicePool Pool, bool Read, bool Write)
		{
			this.Pool = Pool;
			this.Read = Read;
			this.Write = Write;				
		}
	}

	/// <summary>
	/// Platform map required by V1 API
	/// </summary>
	[SingletonDocument("6165a2e26fd5f104e31e6862")]
	public class DevicePlatformMapV1 : SingletonBase
	{
		/// <summary>
		/// Platform V1 => Platform Id
		/// </summary>
		public Dictionary<string, DevicePlatformId> PlatformMap { get; set; } = new Dictionary<string, DevicePlatformId>();

		/// <summary>
		/// Platform Id => Platform V1
		/// </summary>
		public Dictionary<DevicePlatformId, string> PlatformReverseMap { get; set; } = new Dictionary<DevicePlatformId, string>();

		/// <summary>
		/// Perfspec V1 => Model
		/// </summary>
		public Dictionary<DevicePlatformId, string> PerfSpecHighMap { get; set; } = new Dictionary<DevicePlatformId, string>();

	}


	/// <summary>
	/// Device management service
	/// </summary>
	public sealed class DeviceService : IHostedService, IDisposable
	{
		/// <summary>
		/// The ACL service instance
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Instance of the notification service
		/// </summary>
		INotificationService NotificationService;

		/// <summary>
		/// Singleton instance of the job service
		/// </summary>
		JobService JobService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// Singleton instance of the project service
		/// </summary>
		ProjectService ProjectService;

		/// <summary>
		/// The user collection instance
		/// </summary>
		IUserCollection UserCollection { get; set; }

		/// <summary>
		/// Log output writer
		/// </summary>
		ILogger<DeviceService> Logger;

		/// <summary>
		/// Device collection
		/// </summary>
		IDeviceCollection Devices;

		ITicker Ticker;

		/// <summary>
		/// Platform map V1 singleton
		/// </summary>
		ISingletonDocument<DevicePlatformMapV1> PlatformMapSingleton;

		/// <summary>
		/// Device service constructor
		/// </summary>
		public DeviceService(IDeviceCollection Devices, ISingletonDocument<DevicePlatformMapV1> PlatformMapSingleton, IUserCollection UserCollection, JobService JobService, ProjectService ProjectService, StreamService StreamService, AclService AclService, INotificationService NotificationService, IClock Clock, ILogger<DeviceService> Logger)
		{
			this.UserCollection = UserCollection;
			this.Devices = Devices;
            this.JobService = JobService;
			this.ProjectService = ProjectService;
            this.StreamService = StreamService;
            this.AclService = AclService;
            this.NotificationService = NotificationService;
			this.Ticker = Clock.AddSharedTicker<DeviceService>(TimeSpan.FromMinutes(1.0), TickAsync, Logger);
            this.Logger = Logger;
			
			this.PlatformMapSingleton = PlatformMapSingleton;

		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken) => Ticker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken cancellationToken) => Ticker.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Ticker.Dispose();

		/// <summary>
		/// Ticks service
		/// </summary>
		async ValueTask TickAsync(CancellationToken StoppingToken)
		{

			if (!StoppingToken.IsCancellationRequested)
			{
				await Devices.ExpireReservationsAsync();

				List<(UserId, IDevice)>? ExpireNotifications = await Devices.ExpireNotificatonsAsync();
				if (ExpireNotifications != null && ExpireNotifications.Count > 0)
				{
					foreach ((UserId, IDevice) ExpiredDevice in ExpireNotifications)
					{
						await NotifyDeviceServiceAsync($"Device {ExpiredDevice.Item2.PlatformId.ToString().ToUpperInvariant()} / {ExpiredDevice.Item2.Name} checkout will expire in 24 hours.  Please visit https://horde.devtools.epicgames.com/devices to renew the checkout if needed.", null, null, null, ExpiredDevice.Item1);
					}
				}

				List<(UserId, IDevice)>? ExpireCheckouts = await Devices.ExpireCheckedOutAsync();
				if (ExpireCheckouts != null && ExpireCheckouts.Count > 0)
				{
					foreach ((UserId, IDevice) ExpiredDevice in ExpireCheckouts)
					{
						await NotifyDeviceServiceAsync($"Device {ExpiredDevice.Item2.PlatformId.ToString().ToUpperInvariant()} / {ExpiredDevice.Item2.Name} checkout has expired.  The device has been returned to the shared pool and should no longer be accessed.  Please visit https://horde.devtools.epicgames.com/devices to checkout devices as needed.", null, null, null, ExpiredDevice.Item1);
					}
				}
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
		public Task<IDevicePool?> TryCreatePoolAsync(DevicePoolId Id, string Name, DevicePoolType PoolType, List<ProjectId>? ProjectIds)
		{
			return Devices.TryAddPoolAsync(Id, Name, PoolType, ProjectIds);
		}

		/// <summary>
		/// Update a device pool
		/// </summary>
		public Task UpdatePoolAsync(DevicePoolId Id, List<ProjectId>? ProjectIds)
		{
			return Devices.UpdatePoolAsync(Id, ProjectIds);
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
        /// <param name="UserId">User adding the device</param>
		/// <returns></returns>
		public Task<IDevice?> TryCreateDeviceAsync(DeviceId Id, string Name, DevicePlatformId PlatformId, DevicePoolId PoolId, bool? Enabled, string? Address, string? ModelId, UserId? UserId = null)
		{
			return Devices.TryAddDeviceAsync(Id, Name, PlatformId, PoolId, Enabled, Address, ModelId, UserId);
		}

		/// <summary>
		/// Update a device
		/// </summary>
		public Task UpdateDeviceAsync(DeviceId DeviceId, DevicePoolId? NewPoolId = null, string? NewName = null, string? NewAddress = null, string? NewModelId = null, string? NewNotes = null, bool? NewEnabled = null, bool? NewProblem = null, bool? NewMaintenance = null, UserId? ModifiedByUserId = null)
		{
			return Devices.UpdateDeviceAsync(DeviceId, NewPoolId, NewName, NewAddress, NewModelId, NewNotes, NewEnabled, NewProblem, NewMaintenance, ModifiedByUserId);
		}

		/// <summary>
		/// Checkout a device
		/// </summary>
		public Task CheckoutDeviceAsync(DeviceId DeviceId, UserId? UserId)
		{
            return Devices.CheckoutDeviceAsync(DeviceId, UserId);
        }

		/// <summary>
		/// Try to create a reservation satisfying the specified device platforms and models
		/// </summary>
		public Task<IDeviceReservation?> TryCreateReservationAsync(DevicePoolId Pool, List<DeviceRequestData> Request, string? Hostname = null, string? ReservationDetails = null, string? JobId = null, string? StepId = null)
		{
			return Devices.TryAddReservationAsync(Pool, Request, Hostname, ReservationDetails, JobId, StepId);
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
		/// Get a reservation from a device id
		/// </summary>
		public Task<IDeviceReservation?> TryGetDeviceReservation(DeviceId DeviceId)
		{
			return Devices.TryGetDeviceReservationAsync(DeviceId);
		}

		/// <summary>
		/// Get a list of existing device reservations
		/// </summary>
		public Task<List<IDeviceReservation>> GetReservationsAsync()
		{
			return Devices.FindAllReservationsAsync();
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="DeviceId"></param>
		/// <param name="JobId"></param>
		/// <param name="StepId"></param>
		/// <param name="UserId"></param>
		public async Task NotifyDeviceServiceAsync(string Message, DeviceId? DeviceId = null, string? JobId = null, string? StepId = null, UserId? UserId = null)
		{
			try 
			{
				IDevice? Device = null;
				IDevicePool? Pool = null;
				IJob? Job = null;
				IJobStep? Step = null;
				INode? Node = null;
				IStream? Stream = null;
				IUser? User = null;

				if (UserId.HasValue)
				{
					User = await UserCollection.GetUserAsync(UserId.Value);
					if (User == null)
					{
						Logger.LogError("Unable to send device notification, can't find User {UserId}", UserId.Value);
						return;
					}
				}

				if (DeviceId.HasValue)
				{
					Device = await GetDeviceAsync(DeviceId.Value);
					Pool = await GetPoolAsync(Device!.PoolId);
				}

				if (JobId != null)
				{
					Job = await JobService.GetJobAsync(new JobId(JobId));

					if (Job != null)
					{
						Stream = await StreamService.GetStreamAsync(Job.StreamId);

						if (StepId != null)
						{
							IGraph Graph = await JobService.GetGraphAsync(Job)!;

							SubResourceId StepIdValue = SubResourceId.Parse(StepId);
							IJobStepBatch? Batch = Job.Batches.FirstOrDefault(B => B.Steps.FirstOrDefault(S => S.Id == StepIdValue) != null);
							if (Batch != null)
							{
								Step = Batch.Steps.FirstOrDefault(S => S.Id == StepIdValue)!;
								INodeGroup Group = Graph.Groups[Batch.GroupIdx];
								Node = Group.Nodes[Step.NodeIdx];
							}
						}
					}
				}

				NotificationService.NotifyDeviceService(Message, Device, Pool, Stream, Job, Step, Node, User);

			}
			catch (Exception Ex)
			{
                Logger.LogError(Ex, "Error on device notification {Message}", Ex.Message);
            }
        }



		/// <summary>
		/// Authorize device action
		/// </summary>
		/// <param name="Action"></param>
		/// <param name="User"></param>
		/// <returns></returns>
		[SuppressMessage("Usage", "CA1801:Review unused parameters")]
		[SuppressMessage("Performance", "CA1822: Can be static ")]
		async public Task<bool> AuthorizeAsync(AclAction Action, ClaimsPrincipal User)
		{
			// Tihs is deprecated and device auth should be going through GetUserPoolAuthorizationsAsync

			if (Action == AclAction.DeviceRead)
			{
				return true;
			}

			if (User.IsInRole("Internal-Employees"))
			{
				return true;
			}

			if (await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return true;
			}

			return false;

		}

		/// <summary>
		/// Get list of pool authorizations for a user
		/// </summary>
		/// <param name="User"></param>
		/// <returns></returns>
		async public Task<List<DevicePoolAuthorization>> GetUserPoolAuthorizationsAsync(ClaimsPrincipal User)
		{
			
			List<DevicePoolAuthorization> AuthPools = new List<DevicePoolAuthorization>();

			List<IDevicePool> AllPools = await GetPoolsAsync();
			List<IProject> Projects = await ProjectService.GetProjectsAsync();

			// Set of projects associated with device pools
			HashSet<ProjectId> ProjectIds = new HashSet<ProjectId>(AllPools.Where(x => x.ProjectIds != null).SelectMany(x => x.ProjectIds!));

			Dictionary<ProjectId, bool> DeviceRead = new Dictionary<ProjectId, bool>();
			Dictionary<ProjectId, bool> DeviceWrite = new Dictionary<ProjectId, bool>();
			ProjectPermissionsCache PermissionsCache = new ProjectPermissionsCache();

			foreach (ProjectId ProjectId in ProjectIds)
			{

				if (Projects.Where(x => x.Id == ProjectId).FirstOrDefault() == null)
				{
					Logger.LogWarning("Device pool authorization references missing project id {ProjectId}", ProjectId);
					continue;
				}

				DeviceRead.Add(ProjectId,  await ProjectService.AuthorizeAsync(ProjectId, AclAction.DeviceRead, User, PermissionsCache));
				DeviceWrite.Add(ProjectId, await ProjectService.AuthorizeAsync(ProjectId, AclAction.DeviceWrite, User, PermissionsCache));
			}

			// for global pools which aren't associated with a project
			bool GlobalPoolAccess = User.IsInRole("Internal-Employees");

			if (!GlobalPoolAccess)
			{
				if (await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
				{
					GlobalPoolAccess = true;
				}
			}

			foreach (IDevicePool Pool in AllPools)
			{				
				if (Pool.ProjectIds == null || Pool.ProjectIds.Count == 0)
				{
					if (Pool.PoolType == DevicePoolType.Shared && !GlobalPoolAccess)
					{
						AuthPools.Add(new DevicePoolAuthorization(Pool, false, false));
						continue;
					}

					AuthPools.Add(new DevicePoolAuthorization(Pool, true, true));
					continue;
				}

				bool Read = false;
				bool Write = false;

				foreach (ProjectId ProjectId in Pool.ProjectIds)
				{
					bool Value;

					if (!Read && DeviceRead.TryGetValue(ProjectId, out Value))
					{
						Read = Value;
					}

					if (!Write && DeviceWrite.TryGetValue(ProjectId, out Value))
					{
						Write = Value;
					}
				}

				AuthPools.Add(new DevicePoolAuthorization(Pool, Read, Write));

			}

			return AuthPools;
		}

		/// <summary>
		/// Get list of pool authorizations for a user
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="User"></param>
		/// <returns></returns>
		async public Task<DevicePoolAuthorization?> GetUserPoolAuthorizationAsync(DevicePoolId Id, ClaimsPrincipal User)
		{
			List<DevicePoolAuthorization> Auth = await GetUserPoolAuthorizationsAsync(User);
			return Auth.Where(x => x.Pool.Id == Id).FirstOrDefault();
		}

		/// <summary>
		/// Update a device
		/// </summary>
		async public Task UpdateDevicePoolAsync(DevicePoolId PoolId, List<ProjectId>? ProjectIds)
		{
			await Devices.UpdatePoolAsync(PoolId, ProjectIds);
		}


		/// <summary>
		/// Get Platform mappings for V1 API
		/// </summary>		
		public async Task<DevicePlatformMapV1> GetPlatformMapV1()
		{
			return await PlatformMapSingleton.GetAsync();
		}

		/// <summary>
		/// Updates the platform mapping information required for the V1 api
		/// </summary>				
		public async Task<bool> UpdatePlatformMapAsync(UpdatePlatformMapRequest Request)
		{

			List<IDevicePlatform> Platforms = await GetPlatformsAsync();

			// Update the platform map
			for (int i = 0; i < 10; i++)
			{
				DevicePlatformMapV1 Instance = await PlatformMapSingleton.GetAsync();

				Instance.PlatformMap = new Dictionary<string, DevicePlatformId>();
				Instance.PlatformReverseMap = new Dictionary<DevicePlatformId, string>();
				Instance.PerfSpecHighMap = new Dictionary<DevicePlatformId, string>();

				foreach(KeyValuePair<string, string> Entry in Request.PlatformMap)
				{
					IDevicePlatform? Platform = Platforms.FirstOrDefault(P => P.Id == new DevicePlatformId(Entry.Value));
					if (Platform == null)
					{
						throw new Exception($"Unknowm platform in map {Entry.Key} : {Entry.Value}");
					}

					Instance.PlatformMap.Add(Entry.Key, Platform.Id);
				}

				foreach (KeyValuePair<string, string> Entry in Request.PlatformReverseMap)
				{
					IDevicePlatform? Platform = Platforms.FirstOrDefault(P => P.Id == new DevicePlatformId(Entry.Key));
					if (Platform == null)
					{
						throw new Exception($"Unknowm platform in reverse map {Entry.Key} : {Entry.Value}");
					}

					Instance.PlatformReverseMap.Add(Platform.Id, Entry.Value);
				}

				foreach (KeyValuePair<string, string> Entry in Request.PerfSpecHighMap)
				{
					IDevicePlatform? Platform = Platforms.FirstOrDefault(P => P.Id == new DevicePlatformId(Entry.Key));
					if (Platform == null)
					{
						throw new Exception($"Unknowm platform in spec map {Entry.Key} : {Entry.Value}");
					}

					Instance.PerfSpecHighMap.Add(Platform.Id, Entry.Value);
				}


				if (await PlatformMapSingleton.TryUpdateAsync(Instance))
				{
					return true;
				}

				Thread.Sleep(1000);
			}

			return false;

		}

	}
}