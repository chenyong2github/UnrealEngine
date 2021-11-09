// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using MongoDB.Bson.Serialization.Attributes;
using System.Threading.Tasks;

using HordeServer.Models;
using MongoDB.Driver;
using System;
using HordeServer.Utilities;
using System.Linq;

using MongoDB.Bson;
using System.Collections.Generic;

namespace HordeServer.Collections.Impl
{
	using DeviceId = StringId<IDevice>;
	using DevicePlatformId = StringId<IDevicePlatform>;
	using DevicePoolId = StringId<IDevicePool>;
	using UserId = ObjectId<IUser>;
	using ProjectId = StringId<IProject>;

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

			public DevicePlatformDocument(DevicePlatformId Id, string Name)
			{
				this.Id = Id;
				this.Name = Name;
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

			public DevicePoolDocument(DevicePoolId Id, string Name, DevicePoolType PoolType, List<ProjectId>? ProjectIds)
			{
				this.Id = Id;
				this.Name = Name;
                this.PoolType = PoolType;
				this.ProjectIds = ProjectIds;
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
			/// 
			/// </summary>
			public List<DeviceId> Devices { get; set; } = new List<DeviceId>();

			public DateTime CreateTimeUtc { get; set; }

			public DateTime UpdateTimeUtc { get; set; }

			// Legacy Guid
			public string LegacyGuid { get; set; } = null!;

			[BsonConstructor]
			private DeviceReservationDocument()
			{

			}

			public DeviceReservationDocument(ObjectId Id, DevicePoolId PoolId, List<DeviceId> Devices, DateTime CreateTimeUtc, string? Hostname, string? ReservationDetails, string? JobId, string? StepId)
			{
				this.Id = Id;
				this.PoolId = PoolId;
				this.Devices = Devices;
				this.CreateTimeUtc = CreateTimeUtc;
				this.UpdateTimeUtc = CreateTimeUtc;
				this.Hostname = Hostname;
				this.ReservationDetails = ReservationDetails;
                this.JobId = JobId;
                this.StepId = StepId;

                this.LegacyGuid = Guid.NewGuid().ToString();

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

			public DeviceDocument(DeviceId Id, DevicePlatformId PlatformId, DevicePoolId PoolId, string Name, bool Enabled, string? Address, string? ModelId, UserId? UserId)
			{
				this.Id = Id;
				this.PlatformId = PlatformId;
				this.PoolId = PoolId;
				this.Name = Name;
				this.Enabled = Enabled;
				this.Address = Address;
				this.ModelId = ModelId;
                this.ModifiedByUser = UserId?.ToString();
            }

		}

		readonly IMongoCollection<DevicePlatformDocument> Platforms;

		readonly IMongoCollection<DeviceDocument> Devices;

		readonly IMongoCollection<DevicePoolDocument> Pools;

		readonly IMongoCollection<DeviceReservationDocument> Reservations;

		/// <summary>
		/// Constructor
		/// </summary>
		public DeviceCollection(DatabaseService DatabaseService)
		{
			Devices = DatabaseService.GetCollection<DeviceDocument>("Devices");
			Platforms = DatabaseService.GetCollection<DevicePlatformDocument>("Devices.Platforms");
			Pools = DatabaseService.GetCollection<DevicePoolDocument>("Devices.Pools");
			Reservations = DatabaseService.GetCollection<DeviceReservationDocument>("Devices.Reservations");

			if (!DatabaseService.ReadOnlyMode)
			{
				Platforms.Indexes.CreateOne(new CreateIndexModel<DevicePlatformDocument>(Builders<DevicePlatformDocument>.IndexKeys.Ascending(x => x.Name), new CreateIndexOptions { Unique = true }));
				Pools.Indexes.CreateOne(new CreateIndexModel<DevicePoolDocument>(Builders<DevicePoolDocument>.IndexKeys.Ascending(x => x.Name), new CreateIndexOptions { Unique = true }));
				Devices.Indexes.CreateOne(new CreateIndexModel<DeviceDocument>(Builders<DeviceDocument>.IndexKeys.Ascending(x => x.Name), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <inheritdoc/>
		public async Task<IDevice?> TryAddDeviceAsync(DeviceId Id, string Name, DevicePlatformId PlatformId, DevicePoolId PoolId, bool? Enabled, string? Address, string? ModelId, UserId? UserId)
		{
			DeviceDocument NewDevice = new DeviceDocument(Id, PlatformId, PoolId, Name, Enabled ?? true, Address, ModelId, UserId);
			await Devices.InsertOneAsync(NewDevice);
			return NewDevice;
		}


		/// <inheritdoc/>
		public async Task<IDevicePlatform?> TryAddPlatformAsync(DevicePlatformId Id, string Name)
		{
			DevicePlatformDocument NewPlatform = new DevicePlatformDocument(Id, Name);

			try
			{
				await Platforms.InsertOneAsync(NewPlatform);
				return NewPlatform;
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
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
			List<DevicePlatformDocument> Results = await Platforms.Find(x => true).ToListAsync();
			return Results.OrderBy(x => x.Name).Select<DevicePlatformDocument, IDevicePlatform>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<bool> UpdatePlatformAsync(DevicePlatformId PlatformId, string[]? ModelIds)
		{
			UpdateDefinitionBuilder<DevicePlatformDocument> UpdateBuilder = Builders<DevicePlatformDocument>.Update;

			List<UpdateDefinition<DevicePlatformDocument>> Updates = new List<UpdateDefinition<DevicePlatformDocument>>();

			if (ModelIds != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Models, ModelIds.ToList()));
			}

			if (Updates.Count > 0)
			{
				await Platforms.FindOneAndUpdateAsync<DevicePlatformDocument>(x => x.Id == PlatformId, UpdateBuilder.Combine(Updates));
			}

			return true;

		}

		/// <inheritdoc/>
		public async Task<IDevicePool?> TryAddPoolAsync(DevicePoolId Id, string Name, DevicePoolType PoolType, List<ProjectId>? ProjectIds)
		{
			DevicePoolDocument NewPool = new DevicePoolDocument(Id, Name, PoolType, ProjectIds);

			try
			{
				await Pools.InsertOneAsync(NewPool);
				return NewPool;
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
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
		public async Task UpdatePoolAsync(DevicePoolId Id, List<ProjectId>? ProjectIds)
		{
			UpdateDefinitionBuilder<DevicePoolDocument> UpdateBuilder = Builders<DevicePoolDocument>.Update;

			List<UpdateDefinition<DevicePoolDocument>> Updates = new List<UpdateDefinition<DevicePoolDocument>>();

			if (ProjectIds != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.ProjectIds, ProjectIds));
			}

			if (Updates.Count > 0)
			{
				await Pools.FindOneAndUpdateAsync<DevicePoolDocument>(x => x.Id == Id, UpdateBuilder.Combine(Updates));
			}

		}

		/// <inheritdoc/>
		public async Task<List<IDevice>> FindAllDevicesAsync(List<DeviceId>? DeviceIds = null)
		{
			FilterDefinition<DeviceDocument> Filter = Builders<DeviceDocument>.Filter.Empty;
			if (DeviceIds != null)
			{
				Filter &= Builders<DeviceDocument>.Filter.In(x => x.Id, DeviceIds);
			}
			List<DeviceDocument> Results = await Devices.Find(Filter).ToListAsync();
			return Results.OrderBy(x => x.Name).Select<DeviceDocument, IDevice>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<List<IDevicePool>> FindAllPoolsAsync()
		{
			List<DevicePoolDocument> Results = await Pools.Find(x => true).ToListAsync();
			return Results.OrderBy(x => x.Name).Select<DevicePoolDocument, IDevicePool>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<IDevicePlatform?> GetPlatformAsync(DevicePlatformId PlatformId)
		{
			return await Platforms.Find<DevicePlatformDocument>(x => x.Id == PlatformId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevicePool?> GetPoolAsync(DevicePoolId PoolId)
		{
			return await Pools.Find<DevicePoolDocument>(x => x.Id == PoolId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevice?> GetDeviceAsync(DeviceId DeviceId)
		{
			return await Devices.Find<DeviceDocument>(x => x.Id == DeviceId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDevice?> GetDeviceByNameAsync(string DeviceName)
		{
			return await Devices.Find<DeviceDocument>(x => x.Name == DeviceName).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task CheckoutDeviceAsync(DeviceId DeviceId, UserId? CheckedOutByUserId)
		{
			UpdateDefinitionBuilder<DeviceDocument> UpdateBuilder = Builders<DeviceDocument>.Update;

			List<UpdateDefinition<DeviceDocument>> Updates = new List<UpdateDefinition<DeviceDocument>>();

            string? UserId = CheckedOutByUserId?.ToString();

            Updates.Add(UpdateBuilder.Set(x => x.CheckedOutByUser, string.IsNullOrEmpty(UserId) ? null : UserId));

			if (CheckedOutByUserId != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.CheckOutTime, DateTime.UtcNow));
			}

			await Devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == DeviceId, UpdateBuilder.Combine(Updates));
		}


		/// <inheritdoc/>
		public async Task UpdateDeviceAsync(DeviceId DeviceId, DevicePoolId? NewPoolId, string? NewName, string? NewAddress, string? NewModelId, string? NewNotes, bool? NewEnabled, bool? NewProblem, bool? NewMaintenance, UserId? ModifiedByUserId = null)
		{
			UpdateDefinitionBuilder<DeviceDocument> UpdateBuilder = Builders<DeviceDocument>.Update;

			List<UpdateDefinition<DeviceDocument>> Updates = new List<UpdateDefinition<DeviceDocument>>();

			DateTime UtcNow = DateTime.UtcNow;

			if (ModifiedByUserId != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.ModifiedByUser, ModifiedByUserId.ToString()));
			}

			if (NewPoolId != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.PoolId, NewPoolId.Value));
			}

			if (NewName != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Name, NewName));
			}

			if (NewEnabled != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Enabled, NewEnabled));
			}

			if (NewAddress != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Address, NewAddress));
			}

			if (!string.IsNullOrEmpty(NewModelId))
			{
				if (NewModelId == "Base")
				{
					Updates.Add(UpdateBuilder.Set(x => x.ModelId, null));
				}
				else
				{
					Updates.Add(UpdateBuilder.Set(x => x.ModelId, NewModelId));
				}				
			}

			if (NewNotes != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Notes, NewNotes));
			}

			if (NewProblem.HasValue)
			{
				DateTime? ProblemTime = null;
				if (NewProblem.Value)
				{
					ProblemTime = UtcNow;
				}

				Updates.Add(UpdateBuilder.Set(x => x.ProblemTimeUtc, ProblemTime));
			}

			if (NewMaintenance.HasValue)
			{
				DateTime? MaintenanceTime = null;
				if (NewMaintenance.Value)
				{
					MaintenanceTime = UtcNow;
				}

				Updates.Add(UpdateBuilder.Set(x => x.MaintenanceTimeUtc, MaintenanceTime));
			}


			if (Updates.Count > 0)
			{
				await Devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == DeviceId, UpdateBuilder.Combine(Updates));
			}
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteDeviceAsync(DeviceId DeviceId)
		{
			FilterDefinition<DeviceDocument> Filter = Builders<DeviceDocument>.Filter.Eq(x => x.Id, DeviceId);
			DeleteResult Result = await Devices.DeleteOneAsync(Filter);
			return Result.DeletedCount > 0;

		}

		/// <inheritdoc/>
		public async Task<List<IDeviceReservation>> FindAllDeviceReservationsAsync(DevicePoolId? PoolId = null)
		{
			return await Reservations.Find(A => PoolId == null || A.PoolId == PoolId).ToListAsync<DeviceReservationDocument, IDeviceReservation>();
		}

		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryAddReservationAsync(DevicePoolId PoolId, List<DeviceRequestData> Request, string? Hostname, string? ReservationDetails, string? JobId, string? StepId)
		{

			if (Request.Count == 0)
			{
				return null;
			}

			DevicePoolDocument? Pool = await Pools.Find<DevicePoolDocument>(x => x.Id == PoolId).FirstOrDefaultAsync();

			if (Pool == null || Pool.PoolType != DevicePoolType.Automation)
			{
                return null;
            }
			
			HashSet<DeviceId> Allocated = new HashSet<DeviceId>();

			List<DeviceReservationDocument> PoolReservations = await Reservations.Find(x => x.PoolId == PoolId).ToListAsync();

			DateTime ReservationTimeUtc = DateTime.UtcNow;

			// Get available devices
			List<DeviceDocument> PoolDevices = await Devices.Find(x =>
				x.PoolId == PoolId &&
				x.Enabled &&
				x.MaintenanceTimeUtc == null).ToListAsync();

			// filter out problem devices
			PoolDevices = PoolDevices.FindAll(x => (x.ProblemTimeUtc == null || ((ReservationTimeUtc - x.ProblemTimeUtc).Value.TotalMinutes > 30)));

			// filter out currently reserved devices
			PoolDevices = PoolDevices.FindAll(x => PoolReservations.FirstOrDefault(p => p.Devices.Contains(x.Id)) == null);

			// sort to use last reserved first to cycle devices
			PoolDevices.Sort((A, B) =>
			{
				DateTime? ATime = A.ReservationTimeUtc;
				DateTime? BTime = B.ReservationTimeUtc;

				if (ATime == BTime)
				{
					return 0;
				}

				if (ATime == null && BTime != null)
				{
					return -1;
				}
				if (ATime != null && BTime == null)
				{
					return 1;
				}

				return ATime < BTime ? -1 : 1;

			});

			foreach (DeviceRequestData Data in Request)
			{
				DeviceDocument? Device = PoolDevices.FirstOrDefault(A =>
				{

					if (Allocated.Contains(A.Id) || A.PlatformId != Data.PlatformId)
					{
						return false;
					}

					if (Data.IncludeModels.Count > 0 && (A.ModelId == null || !Data.IncludeModels.Contains(A.ModelId)))
					{
						return false;
					}

					if (Data.ExcludeModels.Count > 0 && (A.ModelId != null && Data.ExcludeModels.Contains(A.ModelId)))
					{
						return false;
					}


					return true;
				});

				if (Device == null)
				{
					// can't fulfill request
					return null;
				}

				Allocated.Add(Device.Id);
			}

			// update reservation time and utilization for allocated devices 
			foreach (DeviceId Id in Allocated)
			{
				DeviceDocument Device = PoolDevices.First((Device) => Device.Id == Id);

				List<DeviceUtilizationTelemetry>? Utilization = Device.Utilization;

				if (Utilization == null)
				{
					Utilization = new List<DeviceUtilizationTelemetry>();
				}

				// keep up to 100, maintaining order
				if (Utilization.Count > 99)
				{
					Utilization = Utilization.GetRange(0, 99);
				}

				Utilization.Insert(0, new DeviceUtilizationTelemetry(ReservationTimeUtc) { JobId = JobId, StepId = StepId });

				UpdateDefinitionBuilder<DeviceDocument> DeviceBuilder = Builders<DeviceDocument>.Update;
				List<UpdateDefinition<DeviceDocument>> DeviceUpdates = new List<UpdateDefinition<DeviceDocument>>();

				DeviceUpdates.Add(DeviceBuilder.Set(x => x.ReservationTimeUtc, ReservationTimeUtc));
				DeviceUpdates.Add(DeviceBuilder.Set(x => x.Utilization, Utilization));

				await Devices.FindOneAndUpdateAsync<DeviceDocument>(x => x.Id == Id, DeviceBuilder.Combine(DeviceUpdates));
			}

			// Create new reservation
			DeviceReservationDocument NewReservation = new DeviceReservationDocument(ObjectId.GenerateNewId(), PoolId, Allocated.ToList(), ReservationTimeUtc, Hostname, ReservationDetails, JobId, StepId);
			await Reservations.InsertOneAsync(NewReservation);

			return NewReservation;

		}

		/// <inheritdoc/>
		public async Task<bool> TryUpdateReservationAsync(ObjectId Id)
		{
			UpdateResult Result = await Reservations.UpdateOneAsync(x => x.Id == Id, Builders<DeviceReservationDocument>.Update.Set(x => x.UpdateTimeUtc, DateTime.UtcNow));
			return Result.ModifiedCount == 1;
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteReservationAsync(ObjectId Id)
		{
			FilterDefinition<DeviceReservationDocument> Filter = Builders<DeviceReservationDocument>.Filter.Eq(x => x.Id, Id);
			DeleteResult Result = await Reservations.DeleteOneAsync(Filter);
			return Result.DeletedCount > 0;
		}

		/// <summary>
		/// Deletes expired reservations
		/// </summary>
		public async Task<bool> ExpireReservationsAsync()
		{
			List<IDeviceReservation> Reserves = await Reservations.Find(A => true).ToListAsync<DeviceReservationDocument, IDeviceReservation>();

			DateTime UtcNow = DateTime.UtcNow;

			Reserves = Reserves.FindAll(R => (UtcNow - R.UpdateTimeUtc).TotalMinutes > 10).ToList();

			if (Reserves.Count > 0)
			{
				DeleteResult Result = await Reservations.DeleteManyAsync(Builders<DeviceReservationDocument>.Filter.In(x => x.Id, Reserves.Select(y => y.Id)));
				return (Result.DeletedCount > 0);
			}

			return false;

		}

		/// <inheritdoc/>
		public async Task<List<IDeviceReservation>> FindAllReservationsAsync()
		{
			List<DeviceReservationDocument> Results = await Reservations.Find(x => true).ToListAsync();
			return Results.OrderBy(x => x.CreateTimeUtc.Ticks).Select<DeviceReservationDocument, IDeviceReservation>(x => x).ToList();
		}


		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryGetReservationFromLegacyGuidAsync(string LegacyGuid)
		{
			return await Reservations.Find<DeviceReservationDocument>(R => R.LegacyGuid == LegacyGuid).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IDeviceReservation?> TryGetDeviceReservationAsync(DeviceId Id)
		{
			List<DeviceReservationDocument> Results = await Reservations.Find(x => true).ToListAsync();
			return Results.FirstOrDefault(R => R.Devices.Contains(Id));
        }

	}

}

