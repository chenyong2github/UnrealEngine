// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Projects;
using Horde.Build.Server;
using Horde.Build.Users;
using Horde.Build.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Devices
{
	using DeviceId = StringId<IDevice>;
	using DevicePlatformId = StringId<IDevicePlatform>;
	using DevicePoolId = StringId<IDevicePool>;
	using ProjectId = StringId<IProject>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of device documents
	/// </summary>
	public class DeviceCollection : IDeviceCollection
	{

		/// <summary>
		/// Document representing a device platform
		/// </summary>
		class DevicePlatformDocument : IDevicePlatform
		{
			[BsonRequired, BsonId]
			public DevicePlatformId Id { get; set; }

			public string Name { get; set; }

			public List<string> Models { get; set; } = new List<string>();

			IReadOnlyList<string> IDevicePlatform.Models => Models;

			[BsonConstructor]
			private DevicePlatformDocument()
			{
				Name = null!;
			}

			public DevicePlatformDocument(DevicePlatformId id, string name)
			{
				Id = id;
				Name = name;
			}
		}

		/// <summary>
		/// Document representing a pool of devices
		/// </summary>
		class DevicePoolDocument : IDevicePool
		{
			[BsonRequired, BsonId]
			public DevicePoolId Id { get; set; }

			[BsonRequired]
            public DevicePoolType PoolType { get; set; }

			[BsonIgnoreIfNull]
			public List<ProjectId>? ProjectIds { get; set; }

			[BsonRequired]
			public string Name { get; set; } = null!;

			[BsonIgnoreIfNull]
			public Acl? Acl { get; set; }

			[BsonConstructor]
			private DevicePoolDocument()
			{
            }

			public DevicePoolDocument(DevicePoolId id, string name, DevicePoolType poolType, List<ProjectId>? projectIds)
			{
				Id = id;
				Name = name;
                PoolType = poolType;
				ProjectIds = projectIds;
            }
		}

		/// <summary>
		/// Document representing a reservation of devices
		/// </summary>
		class DeviceReservationDocument : IDeviceReservation
		{
			/// <summary>
			/// Randomly generated unique id for this reservation.
			/// </summary>
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonRequired]
			public DevicePoolId PoolId { get; set; }

			[BsonIgnoreIfNull]
			public string? JobId { get; set; }

			[BsonIgnoreIfNull]
			public string? StepId { get; set; }

            /// <summary>
            /// Reservations held by a user, requires a token like download code
            /// </summary>
            [BsonIgnoreIfNull]
			public UserId? UserId { get; set; }

            /// <summary>
            /// The hostname of the machine which has made the reservation
            /// </summary>
            [BsonIgnoreIfNull]
			public string? Hostname { get; set; }

			/// <summary>
			/// Optional string holding details about the reservation
			/// </summary>
			[BsonIgnoreIfNull]
			public string? ReservationDetails { get; set; }

			/// <summary>
			/// DeviceIds in the reservation
			/// </summary>
			public List<DeviceId> Devices { get; set; } = new List<DeviceId>();

			public List<string> RequestedDevicePlatforms { get; set; } = new List<string>();

			public DateTime CreateTimeUtc { get; set; }

			public DateTime UpdateTimeUtc { get; set; }

			// Legacy Guid
			public string LegacyGuid { get; set; } = null!;

			[BsonConstructor]
			private DeviceReservationDocument()
			{

			}

			public DeviceReservationDocument(ObjectId id, DevicePoolId poolId, List<DeviceId> devices, List<string> requestedDevicePlatforms, DateTime createTimeUtc, string? hostname, string? reservationDetails, string? jobId, string? stepId)
			{
				Id = id;
				PoolId = poolId;
				Devices = devices;
				RequestedDevicePlatforms = requestedDevicePlatforms;
				CreateTimeUtc = createTimeUtc;
				UpdateTimeUtc = createTimeUtc;
				Hostname = hostname;
				ReservationDetails = reservationDetails;
                JobId = jobId;
                StepId = stepId;

                LegacyGuid = Guid.NewGuid().ToString();
			}
		}

		/// <summary>
		/// Concrete implementation of an device document
		/// </summary>
		class DeviceDocument : IDevice
		{
			[BsonRequired, BsonId]
			public DeviceId Id { get; set; }

			public DevicePlatformId PlatformId { get; set; }

			public DevicePoolId PoolId { get; set; }

			public string Name { get; set; } = null!;

			public bool Enabled { get; set; }

			[BsonIgnoreIfNull]
			public string? ModelId { get; set; }

			[BsonIgnoreIfNull]
			public string? Address { get; set; }

			[BsonIgnoreIfNull]
			public string? CheckedOutByUser { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? CheckOutTime { get; set; }

			[BsonIgnoreIfNull]
			public bool? CheckoutExpiringNotificationSent { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? ProblemTimeUtc { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? MaintenanceTimeUtc { get; set; }

			/// <summary>
			///  The last time this device was reserved, used to cycle devices for reservations
			/// </summary>
			[BsonIgnoreIfNull]
			public DateTime? ReservationTimeUtc { get; set; }

			[BsonIgnoreIfNull]
			public string? ModifiedByUser { get; set; }

			[BsonIgnoreIfNull]
			public string? Notes { get; set; }

			[BsonIgnoreIfNull]
			public List<DeviceUtilizationTelemetry>? Utilization { get; set; }

			[BsonIgnoreIfNull]
			public Acl? Acl { get; set; }

			[BsonConstructor]
			private DeviceDocument()
			{
			}

			public DeviceDocument(DeviceId id, DevicePlatformId platformId, DevicePoolId poolId, string name, bool enabled, string? address, string? modelId, UserId? userId)
			{
				Id = id;
				PlatformId = platformId;
				PoolId = poolId;
				Name = name;
				Enabled = enabled;
				Address = address;
				ModelId = modelId;
                ModifiedByUser = userId?.ToString();
            }
		}

		readonly IMongoCollection<DevicePlatformDocument> _platforms;

		readonly IMongoCollection<DeviceDocument> _devices;

		readonly IMongoCollection<DevicePoolDocument> _pools;

		readonly IMongoCollection<DeviceReservationDocument> _reservations;

		readonly int _checkoutDays = 7;

		/// <summary>
		/// Constructor
		/// </summary>
		public DeviceCollection(MongoService mongoService)
		{
			_devices = mongoService.GetCollection<DeviceDocument>("Devices", keys => keys.Ascending(x => x.Name), unique: true);
			_platforms = mongoService.GetCollection<DevicePlatformDocument>("Devices.Platforms", keys => keys.Ascending(x => x.Name), unique: true);
			_pools = mongoService.GetCollection<DevicePoolDocument>("Devices.Pools", keys => keys.Ascending(x => x.Name), unique: true);
			_reservations = mongoService.GetCollection<DeviceReservationDocument>("Devices.Reservations");
		}

		/// <inheritdoc/>
		public async Task<IDevice?> TryAddDeviceAsync(DeviceId id, string name, DevicePlatformId platformId, DevicePoolId poolId, bool? enabled, string? address, string? modelId, UserId? userId)
		{
			DeviceDocument newDevice = new DeviceDocument(id, platformId, poolId, name, enabled ?? true, address, modelId, userId);
			await _devices.InsertOneAsync(newDevice);
			return newDevice;
		}

		/// <inheritdoc/>
		public async Task<IDevicePlatform?> TryAddPlatformAsync(DevicePlatformId id, string name)
		{
			DevicePlatformDocument newPlatform = new DevicePlatformDocument(id, name);

			try
			{
				await _platforms.InsertOneAsync(newPlatform);
				return newPlatform;
			}
			catch (MongoWriteException ex)
			{
				if (ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return null;
				}
				else
				{
					throw;
				}
			}
		}

		/// <inheritdoc/>
		public async Task<List<IDevicePlatform>> FindAllPlatformsAsync()
		{
			List<DevicePlatformDocument> results = await _platforms.Find(x => true).ToListAsync();
			return results.OrderBy(x => x.Name).Select<DevicePlatformDocument, IDevicePlatform>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<bool> UpdatePlatformAsync(DevicePlatformId platformId, string[]? modelIds)
		{
			UpdateDefinitionBuilder<DevicePlatformDocument> updateBuilder = Builders<DevicePlatformDocument>.Update;

			List<UpdateDefinition<DevicePlatformDocument>> updates = new List<UpdateDefinition<DevicePlatformDocument>>();

			if (modelIds != null)
			{
				updates.Add(updateBuilder.Set(x => x.Models, modelIds.ToList()));
			}

			if (updates.Count > 0)
			{
				await _platforms.FindOneAndUpdateAsync<DevicePlatformDocument>(x => x.Id == platformId, updateBuilder.Combine(updates));
			}

			return true;

		}

		/// <inheritdoc/>
		public async Task<IDevicePool?> TryAddPoolAsync(DevicePoolId id, string name, DevicePoolType poolType, List<ProjectId>? projectIds)
		{
			DevicePoolDocument newPool = new DevicePoolDocument(id, name, poolType, projectIds);

			try
			{
				await _pools.InsertOneAsync(newPool);
				return newPool;
			}
			catch (MongoWriteException ex)
			{
				if (ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return null;
				}
				else
				{
					throw;
				}
			}
		}

		/// <inheritdoc/>
		public async Task UpdatePoolAsync(DevicePoolId id, List<ProjectId>? projectIds)
		{
			UpdateDefinitionBuilder<DevicePoolDocument> updateBuilder = Builders<DevicePoolDocument>.Update;

			List<UpdateDefinition<DevicePoolDocument>> updates = new List<UpdateDefinition<DevicePoolDocument>>();

			if (projectIds != null)
			{
				updates.Add(updateBuilder.Set(x => x.ProjectIds, projectIds));
			}

			if (updates.Count > 0)
			{
				await _pools.FindOneAndUpdateAsync<DevicePoolDocument>(x => x.Id == id, updateBuilder.Combine(updates));
			}
		}

		/// <inheritdoc/>
		public async Task<List<IDevice>> FindAllDevicesAsync(List<DeviceId>? deviceIds = null)
		{
			FilterDefinition<DeviceDocument> filter = Builders<DeviceDocument>.Filter.Empty;
			if (deviceIds != null)
			{
				filter &= Builders<DeviceDocument>.Filter.In(x => x.Id, deviceIds);
			}
			List<DeviceDocument> results = await _devices.Find(filter).ToListAsync();
			return results.OrderBy(x => x.Name).Select<DeviceDocument, IDevice>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<List<IDevicePool>> FindAllPoolsAsync()
		{
			List<DevicePoolDocument> results = await _pools.Find(x => true).ToListAsync();
			return results.OrderBy(x => x.Name).Select<DevicePoolDocument, IDevicePool>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<IDevicePlatform?> GetPlatformAsync(DevicePlatformId platformId)
		{
			return await _platforms.Find<DevicePlatformDocument>(x => x.Id == platformId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevicePool?> GetPoolAsync(DevicePoolId poolId)
		{
			return await _pools.Find<DevicePoolDocument>(x => x.Id == poolId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevice?> GetDeviceAsync(DeviceId deviceId)
		{
			return await _devices.Find<DeviceDocument>(x => x.Id == deviceId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevice?> GetDeviceByNameAsync(string deviceName)
		{
			return await _devices.Find<DeviceDocument>(x => x.Name == deviceName).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task CheckoutDeviceAsync(DeviceId deviceId, UserId? checkedOutByUserId)
		{
			UpdateDefinitionBuilder<DeviceDocument> updateBuilder = Builders<DeviceDocument>.Update;

			List<UpdateDefinition<DeviceDocument>> updates = new List<UpdateDefinition<DeviceDocument>>();

            string? userId = checkedOutByUserId?.ToString();

            updates.Add(updateBuilder.Set(x => x.CheckedOutByUser, String.IsNullOrEmpty(userId) ? null : userId));			

			if (checkedOutByUserId != null)
			{
				updates.Add(updateBuilder.Set(x => x.CheckOutTime, DateTime.UtcNow));
				updates.Add(updateBuilder.Set(x => x.CheckoutExpiringNotificationSent, false));
			}
			else
			{
				updates.Add(updateBuilder.Set(x => x.CheckoutExpiringNotificationSent, true));
			}

			await _devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == deviceId, updateBuilder.Combine(updates));
		}

		/// <inheritdoc/>
		public async Task UpdateDeviceAsync(DeviceId deviceId, DevicePoolId? newPoolId, string? newName, string? newAddress, string? newModelId, string? newNotes, bool? newEnabled, bool? newProblem, bool? newMaintenance, UserId? modifiedByUserId = null)
		{
			UpdateDefinitionBuilder<DeviceDocument> updateBuilder = Builders<DeviceDocument>.Update;

			List<UpdateDefinition<DeviceDocument>> updates = new List<UpdateDefinition<DeviceDocument>>();

			DateTime utcNow = DateTime.UtcNow;

			if (modifiedByUserId != null)
			{
				updates.Add(updateBuilder.Set(x => x.ModifiedByUser, modifiedByUserId.ToString()));
			}

			if (newPoolId != null)
			{
				updates.Add(updateBuilder.Set(x => x.PoolId, newPoolId.Value));
			}

			if (newName != null)
			{
				updates.Add(updateBuilder.Set(x => x.Name, newName));
			}

			if (newEnabled != null)
			{
				updates.Add(updateBuilder.Set(x => x.Enabled, newEnabled));
			}

			if (newAddress != null)
			{
				updates.Add(updateBuilder.Set(x => x.Address, newAddress));
			}

			if (!String.IsNullOrEmpty(newModelId))
			{
				if (newModelId == "Base")
				{
					updates.Add(updateBuilder.Set(x => x.ModelId, null));
				}
				else
				{
					updates.Add(updateBuilder.Set(x => x.ModelId, newModelId));
				}				
			}

			if (newNotes != null)
			{
				updates.Add(updateBuilder.Set(x => x.Notes, newNotes));
			}

			if (newProblem.HasValue)
			{
				DateTime? problemTime = null;
				if (newProblem.Value)
				{
					problemTime = utcNow;
				}

				updates.Add(updateBuilder.Set(x => x.ProblemTimeUtc, problemTime));
			}

			if (newMaintenance.HasValue)
			{
				DateTime? maintenanceTime = null;
				if (newMaintenance.Value)
				{
					maintenanceTime = utcNow;
				}

				updates.Add(updateBuilder.Set(x => x.MaintenanceTimeUtc, maintenanceTime));
			}

			if (updates.Count > 0)
			{
				await _devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == deviceId, updateBuilder.Combine(updates));
			}
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteDeviceAsync(DeviceId deviceId)
		{
			FilterDefinition<DeviceDocument> filter = Builders<DeviceDocument>.Filter.Eq(x => x.Id, deviceId);
			DeleteResult result = await _devices.DeleteOneAsync(filter);
			return result.DeletedCount > 0;

		}

		/// <inheritdoc/>
		public async Task<List<IDeviceReservation>> FindAllDeviceReservationsAsync(DevicePoolId? poolId = null)
		{
			return await _reservations.Find(a => poolId == null || a.PoolId == poolId).ToListAsync<DeviceReservationDocument, IDeviceReservation>();
		}

		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryAddReservationAsync(DevicePoolId poolId, List<DeviceRequestData> request, string? hostname, string? reservationDetails, string? jobId, string? stepId)
		{

			if (request.Count == 0)
			{
				return null;
			}

			DevicePoolDocument? pool = await _pools.Find<DevicePoolDocument>(x => x.Id == poolId).FirstOrDefaultAsync();

			if (pool == null || pool.PoolType != DevicePoolType.Automation)
			{
                return null;
            }
			
			HashSet<DeviceId> allocated = new HashSet<DeviceId>();
			Dictionary<DeviceId, string> platformRequestMap = new Dictionary<DeviceId, string>();

			List<DeviceReservationDocument> poolReservations = await _reservations.Find(x => x.PoolId == poolId).ToListAsync();

			DateTime reservationTimeUtc = DateTime.UtcNow;

			// Get available devices
			List<DeviceDocument> poolDevices = await _devices.Find(x =>
				x.PoolId == poolId &&
				x.Enabled &&
				x.MaintenanceTimeUtc == null).ToListAsync();

			// filter out problem devices
			poolDevices = poolDevices.FindAll(x => (x.ProblemTimeUtc == null || ((reservationTimeUtc - x.ProblemTimeUtc).Value.TotalMinutes > 30)));

			// filter out currently reserved devices
			poolDevices = poolDevices.FindAll(x => poolReservations.FirstOrDefault(p => p.Devices.Contains(x.Id)) == null);

			// sort to use last reserved first to cycle devices
			poolDevices.Sort((a, b) =>
			{
				DateTime? aTime = a.ReservationTimeUtc;
				DateTime? bTime = b.ReservationTimeUtc;

				if (aTime == bTime)
				{
					return 0;
				}

				if (aTime == null && bTime != null)
				{
					return -1;
				}
				if (aTime != null && bTime == null)
				{
					return 1;
				}

				return aTime < bTime ? -1 : 1;

			});

			foreach (DeviceRequestData data in request)
			{
				DeviceDocument? device = poolDevices.FirstOrDefault(a =>
				{

					if (allocated.Contains(a.Id) || a.PlatformId != data.PlatformId)
					{
						return false;
					}

					if (data.IncludeModels.Count > 0 && (a.ModelId == null || !data.IncludeModels.Contains(a.ModelId)))
					{
						return false;
					}

					if (data.ExcludeModels.Count > 0 && (a.ModelId != null && data.ExcludeModels.Contains(a.ModelId)))
					{
						return false;
					}

					return true;
				});

				if (device == null)
				{
					// can't fulfill request
					return null;
				}

				allocated.Add(device.Id);
				platformRequestMap.Add(device.Id, data.RequestedPlatform);
			}

			// update reservation time and utilization for allocated devices 
			foreach (DeviceId id in allocated)
			{
				DeviceDocument device = poolDevices.First((device) => device.Id == id);

				List<DeviceUtilizationTelemetry>? utilization = device.Utilization;

				if (utilization == null)
				{
					utilization = new List<DeviceUtilizationTelemetry>();
				}

				// keep up to 100, maintaining order
				if (utilization.Count > 99)
				{
					utilization = utilization.GetRange(0, 99);
				}

				utilization.Insert(0, new DeviceUtilizationTelemetry(reservationTimeUtc) { JobId = jobId, StepId = stepId });

				UpdateDefinitionBuilder<DeviceDocument> deviceBuilder = Builders<DeviceDocument>.Update;
				List<UpdateDefinition<DeviceDocument>> deviceUpdates = new List<UpdateDefinition<DeviceDocument>>();

				deviceUpdates.Add(deviceBuilder.Set(x => x.ReservationTimeUtc, reservationTimeUtc));
				deviceUpdates.Add(deviceBuilder.Set(x => x.Utilization, utilization));

				await _devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == id, deviceBuilder.Combine(deviceUpdates));
			}

			List<DeviceId> deviceIds = allocated.ToList();
			List<string> requestedPlatforms = deviceIds.Select(x => platformRequestMap[x]).ToList();

			// Create new reservation
			DeviceReservationDocument newReservation = new DeviceReservationDocument(ObjectId.GenerateNewId(), poolId, deviceIds, requestedPlatforms, reservationTimeUtc, hostname, reservationDetails, jobId, stepId);
			await _reservations.InsertOneAsync(newReservation);

			return newReservation;

		}

		/// <inheritdoc/>
		public async Task<bool> TryUpdateReservationAsync(ObjectId id)
		{
			UpdateResult result = await _reservations.UpdateOneAsync(x => x.Id == id, Builders<DeviceReservationDocument>.Update.Set(x => x.UpdateTimeUtc, DateTime.UtcNow));
			return result.ModifiedCount == 1;
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteReservationAsync(ObjectId id)
		{
			FilterDefinition<DeviceReservationDocument> filter = Builders<DeviceReservationDocument>.Filter.Eq(x => x.Id, id);
			DeleteResult result = await _reservations.DeleteOneAsync(filter);
			return result.DeletedCount > 0;
		}

		/// <summary>
		/// Deletes expired reservations
		/// </summary>
		public async Task<bool> ExpireReservationsAsync()
		{
			List<IDeviceReservation> reserves = await _reservations.Find(a => true).ToListAsync<DeviceReservationDocument, IDeviceReservation>();

			DateTime utcNow = DateTime.UtcNow;

			reserves = reserves.FindAll(r => (utcNow - r.UpdateTimeUtc).TotalMinutes > 10).ToList();

			if (reserves.Count > 0)
			{
				DeleteResult result = await _reservations.DeleteManyAsync(Builders<DeviceReservationDocument>.Filter.In(x => x.Id, reserves.Select(y => y.Id)));
				return (result.DeletedCount > 0);
			}

			return false;

		}

		/// <summary>
		/// Deletes expired user checkouts
		/// </summary>
		public async Task<List<(UserId, IDevice)>?> ExpireCheckedOutAsync()
		{
			FilterDefinition<DeviceDocument> filter = Builders<DeviceDocument>.Filter.Empty;
			filter &= Builders<DeviceDocument>.Filter.Where(x => x.CheckedOutByUser != null && x.CheckOutTime != null);

			List<DeviceDocument> checkedOutDevices = await _devices.Find(filter).ToListAsync();

			DateTime utcNow = DateTime.UtcNow;
			List<DeviceDocument> expiredDevices = checkedOutDevices.FindAll(x => (utcNow - x.CheckOutTime!.Value).TotalDays >= _checkoutDays).ToList();

			if (expiredDevices.Count > 0)
			{

				FilterDefinition<DeviceDocument> updateFilter = Builders<DeviceDocument>.Filter.In(x => x.Id, expiredDevices.Select(y => y.Id));

				UpdateDefinitionBuilder<DeviceDocument> deviceBuilder = Builders<DeviceDocument>.Update;
				List<UpdateDefinition<DeviceDocument>> deviceUpdates = new List<UpdateDefinition<DeviceDocument>>();

				deviceUpdates.Add(deviceBuilder.Set(x => x.CheckedOutByUser, null));
				deviceUpdates.Add(deviceBuilder.Set(x => x.CheckOutTime, null));
				deviceUpdates.Add(deviceBuilder.Set(x => x.CheckoutExpiringNotificationSent, true));

				UpdateResult result = await _devices.UpdateManyAsync(Builders<DeviceDocument>.Filter.In(x => x.Id, expiredDevices.Select(y => y.Id)), deviceBuilder.Combine(deviceUpdates));

				if (result.ModifiedCount > 0)
				{
					return expiredDevices.Select(x => (UserId.Parse(x.CheckedOutByUser!), (IDevice) x)).ToList();
				}
			}

			return null;

		}

		/// <summary>
		/// Gets a list of users to notify  that their device is about to expire in the next 24 hours
		/// </summary>
		public async Task<List<(UserId, IDevice)>?> ExpireNotificatonsAsync()
		{
			FilterDefinition<DeviceDocument> filter = Builders<DeviceDocument>.Filter.Empty;
			filter &= Builders<DeviceDocument>.Filter.Where(x => x.CheckedOutByUser != null && x.CheckoutExpiringNotificationSent != true);

			List<DeviceDocument> checkedOutDevices = await _devices.Find(filter).ToListAsync();

			DateTime utcNow = DateTime.UtcNow;
			List<DeviceDocument> expiredDevices = checkedOutDevices.FindAll(x => (utcNow - x.CheckOutTime!.Value).TotalDays >= (_checkoutDays - 1)).ToList();

			if (expiredDevices.Count > 0)
			{
				FilterDefinition<DeviceDocument> updateFilter = Builders<DeviceDocument>.Filter.In(x => x.Id, expiredDevices.Select(y => y.Id));

				UpdateDefinitionBuilder<DeviceDocument> deviceBuilder = Builders<DeviceDocument>.Update;
				List<UpdateDefinition<DeviceDocument>> deviceUpdates = new List<UpdateDefinition<DeviceDocument>>();

				deviceUpdates.Add(deviceBuilder.Set(x => x.CheckoutExpiringNotificationSent, true));

				UpdateResult result = await _devices.UpdateManyAsync(Builders<DeviceDocument>.Filter.In(x => x.Id, expiredDevices.Select(y => y.Id)), deviceBuilder.Combine(deviceUpdates));

				if (result.ModifiedCount > 0)
				{
					return expiredDevices.Select(x => (UserId.Parse(x.CheckedOutByUser!), (IDevice)x)).ToList();
				}
			}

			return null;
		}

		/// <inheritdoc/>
		public async Task<List<IDeviceReservation>> FindAllReservationsAsync()
		{
			List<DeviceReservationDocument> results = await _reservations.Find(x => true).ToListAsync();
			return results.OrderBy(x => x.CreateTimeUtc.Ticks).Select<DeviceReservationDocument, IDeviceReservation>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryGetReservationFromLegacyGuidAsync(string legacyGuid)
		{
			return await _reservations.Find<DeviceReservationDocument>(r => r.LegacyGuid == legacyGuid).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryGetDeviceReservationAsync(DeviceId id)
		{
			List<DeviceReservationDocument> results = await _reservations.Find(x => true).ToListAsync();
			return results.FirstOrDefault(r => r.Devices.Contains(id));
        }
	}
}
