// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Extensions.NETCore.Setup;
using Amazon.Runtime;
using Amazon.S3;
using Amazon.S3.Model;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Claims;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Interface for a artifact storage implementation
	/// </summary>
	public interface IArtifactStorage
	{
		/// <summary>
		/// Gets an artifact from a storage device
		/// </summary>
		/// <param name="JobId">the job id</param>
		/// <param name="StepId">the step id</param>
		/// <param name="Name">The artifact name</param>
		/// <returns>Stream to an artifact if found, null otherwise</returns>
		System.IO.Stream OpenReadStream(ObjectId JobId, SubResourceId? StepId, string Name);

		/// <summary>
		/// Writes artifact data
		/// </summary>
		/// <param name="JobId">the job id</param>
		/// <param name="StepId">the step id</param>
		/// <param name="Name">The artifact name</param>
		/// <param name="Data">The data to write</param>
		/// <returns>Whether or not the update completed</returns>
		Task WriteAsync(ObjectId JobId, SubResourceId? StepId, string Name, System.IO.Stream Data);
	}

	/// <summary>
	/// FileStorage implementation using the file system
	/// </summary>
	public sealed class FSExternalArtifactStorage : IArtifactStorage
	{
		/// <summary>
		/// Base directory for artifacts
		/// </summary>
		private readonly DirectoryInfo BaseDir;

		/// <summary>
		/// Information Logger
		/// </summary>
		private readonly ILogger<ArtifactService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CurrentSettings">Current Horde Settings</param>
		/// <param name="Logger">Logger interface</param>
		public FSExternalArtifactStorage(ServerSettings CurrentSettings, ILogger<ArtifactService> Logger)
		{
			this.Logger = Logger;
			this.BaseDir = new DirectoryInfo(CurrentSettings.LocalStorageDir);

			Directory.CreateDirectory(this.BaseDir.FullName);
		}

		/// <inheritdoc/>
		public System.IO.Stream OpenReadStream(ObjectId JobId, SubResourceId? StepId, string Name)
		{
			string FilePath = Path.Combine(BaseDir.ToString(), JobId.ToString(), StepId.ToString() ?? string.Empty, Name);
			FileInfo ArtifactFile = new FileInfo(FilePath);
			try
			{
				return new FileStream(ArtifactFile.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 4096, true);
			}
			catch (FileNotFoundException)
			{
				Logger.LogError("Tried to find artifact that didn't exist at {0}!", ArtifactFile.FullName);
				throw;
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(ObjectId JobId, SubResourceId? StepId, string Name, System.IO.Stream Data)
		{
			string FilePath = Path.Combine(BaseDir.FullName, JobId.ToString(), StepId.ToString() ?? string.Empty, Name);
			FileInfo ArtifactFile = new FileInfo(FilePath);
			Directory.CreateDirectory(ArtifactFile.Directory.FullName);
			using (FileStream FileWriter = File.Open(ArtifactFile.FullName, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await Data.CopyToAsync(FileWriter);
			}
		}
	}

	/// <summary>
	/// FileStorage implementation using an s3 bucket
	/// </summary>
	public sealed class S3ExternalArtifactStorage : IArtifactStorage
	{
		/// <summary>
		/// S3 Client
		/// </summary>
		private readonly IAmazonS3 Client;

		/// <summary>
		/// S3 bucket name
		/// </summary>
		private readonly string BucketName;

		/// <summary>
		/// Information Logger
		/// </summary>
		private readonly ILogger<ArtifactService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CurrentSettings">Current Horde Settings</param>
		/// <param name="Configuration">Configuration</param>
		/// <param name="Logger">Logger interface</param>
		public S3ExternalArtifactStorage(ServerSettings CurrentSettings, IConfiguration Configuration, ILogger<ArtifactService> Logger)
		{
			this.Logger = Logger;
			AWSOptions Options = Configuration.GetAWSOptions();
			Options.Credentials = GetCredentials(CurrentSettings);

			this.Client = Options.CreateServiceClient<IAmazonS3>();
			this.BucketName = CurrentSettings.S3ArtifactBucketName;
		}

		enum AWSCredentialsType
		{
			Basic,
			AssumeRole,
			AssumeRoleWebIdentity
		}

		/// <summary>
		/// Gets auth credentials based on type
		/// </summary>
		/// <param name="Settings">Horde settings</param>
		/// <returns></returns>
		public AWSCredentials GetCredentials(ServerSettings Settings)
		{
			AWSCredentialsType AuthType;
			if (!Enum.TryParse(Settings.S3CredentialType, true, out AuthType))
			{
				Logger.LogError("Could not determine auth type from appsettings. Should be Basic, AssumeRole, or AssumeRoleWebIdentity!");
				return null!;
			}
			switch (AuthType)
			{
				case AWSCredentialsType.Basic:
					return new BasicAWSCredentials(Settings.S3ClientKeyId, Settings.S3ClientSecret);
				case AWSCredentialsType.AssumeRole:
					return new AssumeRoleAWSCredentials(FallbackCredentialsFactory.GetCredentials(), Settings.S3AssumeArn, "Horde");
				case AWSCredentialsType.AssumeRoleWebIdentity:
					return AssumeRoleWithWebIdentityCredentials.FromEnvironmentVariables();
				default:
					return null!;
			}
		}

		/// <inheritdoc/>
		public System.IO.Stream OpenReadStream(ObjectId JobId, SubResourceId? StepId, string Name)
		{
			string BucketPath = Path.Combine(JobId.ToString(), StepId.ToString() ?? string.Empty, Name);
			try
			{
				GetObjectRequest NewGetRequest = new GetObjectRequest
				{
					BucketName = this.BucketName,
					Key = BucketPath
				};

				return this.Client.GetObjectAsync(NewGetRequest).Result.ResponseStream;
			}
			catch (AmazonS3Exception Ex)
			{
				if (Ex.ErrorCode == "NoSuchKey")
				{
					Logger.LogError("Tried to find artifact that didn't exist at {0}!", BucketPath);
				}
				throw;
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(ObjectId JobId, SubResourceId? StepId, string Name, System.IO.Stream Stream)
		{
			const int MinPartSize = 5 * 1024 * 1024;

			string Key = Path.Combine(JobId.ToString(), StepId.ToString() ?? string.Empty, Name);

			long StreamLen = Stream.Length;
			if (StreamLen < MinPartSize)
			{
				// Read the data into memory
				byte[] Buffer = new byte[StreamLen];
				await ReadExactLengthAsync(Stream, Buffer, (int)StreamLen);

				// Upload it to S3
				using (MemoryStream InputStream = new MemoryStream(Buffer))
				{
					PutObjectRequest UploadRequest = new PutObjectRequest();
					UploadRequest.BucketName = BucketName;
					UploadRequest.Key = Key;
					UploadRequest.InputStream = InputStream;
					await Client.PutObjectAsync(UploadRequest);
				}
			}
			else
			{
				// Initiate a multi-part upload
				InitiateMultipartUploadRequest InitiateRequest = new InitiateMultipartUploadRequest();
				InitiateRequest.BucketName = BucketName;
				InitiateRequest.Key = Key;

				InitiateMultipartUploadResponse InitiateResponse = await Client.InitiateMultipartUploadAsync(InitiateRequest);
				try
				{
					// Buffer for reading the data
					byte[] Buffer = new byte[MinPartSize];

					// Upload all the parts
					List<PartETag> PartTags = new List<PartETag>();
					for(long StreamPos = 0; StreamPos < StreamLen; )
					{
						// Read the next chunk of data into the buffer
						int BufferLen = (int)Math.Min((long)MinPartSize, StreamLen - StreamPos);
						await ReadExactLengthAsync(Stream, Buffer, BufferLen);
						StreamPos += BufferLen;

						// Upload the part
						using (MemoryStream InputStream = new MemoryStream(Buffer, 0, BufferLen))
						{
							UploadPartRequest PartRequest = new UploadPartRequest();
							PartRequest.BucketName = BucketName;
							PartRequest.Key = Key;
							PartRequest.UploadId = InitiateResponse.UploadId;
							PartRequest.InputStream = InputStream;
							PartRequest.PartSize = BufferLen;
							PartRequest.PartNumber = PartTags.Count + 1;
							PartRequest.IsLastPart = (StreamPos == StreamLen);

							UploadPartResponse PartResponse = await Client.UploadPartAsync(PartRequest);
							PartTags.Add(new PartETag(PartResponse.PartNumber, PartResponse.ETag));
						}
					}

					// Mark the upload as complete
					CompleteMultipartUploadRequest CompleteRequest = new CompleteMultipartUploadRequest();
					CompleteRequest.BucketName = BucketName;
					CompleteRequest.Key = Key;
					CompleteRequest.UploadId = InitiateResponse.UploadId;
					CompleteRequest.PartETags = PartTags;
					await Client.CompleteMultipartUploadAsync(CompleteRequest);
				}
				catch
				{
					// Abort the upload
					AbortMultipartUploadRequest AbortRequest = new AbortMultipartUploadRequest();
					AbortRequest.BucketName = BucketName;
					AbortRequest.Key = Key;
					AbortRequest.UploadId = InitiateResponse.UploadId;
					await Client.AbortMultipartUploadAsync(AbortRequest);

					throw;
				}
			}
		}

		/// <summary>
		/// Reads data of an exact length into a stream
		/// </summary>
		/// <param name="Stream">The stream to read from</param>
		/// <param name="Buffer">The buffer to read into</param>
		/// <param name="Length">Length of the data to read</param>
		/// <returns>Async task</returns>
		static async Task ReadExactLengthAsync(System.IO.Stream Stream, byte[] Buffer, int Length)
		{
			int BufferPos = 0;
			while (BufferPos < Length)
			{
				int BytesRead = await Stream.ReadAsync(Buffer, BufferPos, Length - BufferPos);
				if (BytesRead == 0)
				{
					throw new InvalidOperationException("Unexpected end of stream");
				}
				BufferPos += BytesRead;
			}
		}
	}

	/// <summary>
	/// Wraps functionality for manipulating artifacts
	/// </summary>
	public class ArtifactService
	{
		private enum ProviderTypes
		{
			S3,
			FileSystem
		};

		/// <summary>
		/// Backend storage container
		/// </summary>
		private readonly IArtifactStorage ExternalStorageProvider;

		/// <summary>
		/// Information Logger
		/// </summary>
		private readonly ILogger<ArtifactService> Logger;

		/// <summary>
		/// Collection of log documents
		/// </summary>
		private readonly IMongoCollection<Artifact> Artifacts;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service</param>
		/// <param name="Configuration">The configuration</param>
		/// <param name="Settings">Connection settings</param>
		/// <param name="Logger">Log interface</param>
		public ArtifactService(DatabaseService DatabaseService, IOptionsMonitor<ServerSettings> Settings, IConfiguration Configuration, ILogger<ArtifactService> Logger)
		{
			this.Logger = Logger;

			// Initialize Cache provider
			ServerSettings CurrentSettings = Settings.CurrentValue;
			switch (CurrentSettings.ExternalStorageProviderType)
			{
				case StorageProviderType.FileSystem:
					this.ExternalStorageProvider = new FSExternalArtifactStorage(CurrentSettings, Logger);
					break;
				case StorageProviderType.S3:
					this.ExternalStorageProvider = new S3ExternalArtifactStorage(CurrentSettings, Configuration, Logger);
					break;
				default:
					// shouldn't be possible to get here, but default to file system worst case.
					this.ExternalStorageProvider = new FSExternalArtifactStorage(CurrentSettings, Logger);
					break;
			}

			// Initialize Artifacts table
			Artifacts = DatabaseService.Artifacts;
			if (!DatabaseService.ReadOnlyMode)
			{
				Artifacts.Indexes.CreateOne(new CreateIndexModel<Artifact>(Builders<Artifact>.IndexKeys.Ascending(x => x.JobId)));
			}
		}

		/// <summary>
		/// Creates a new artifact
		/// </summary>
		/// <param name="JobId">Unique id of the job that owns this artifact</param>
		/// <param name="StepId">Optional Step id</param>
		/// <param name="Name">Name of artifact</param>
		/// <param name="MimeType">Type of artifact</param>
		/// <param name="Data">The data to write</param>
		/// <returns>The new log file document</returns>
		public async Task<Artifact> CreateArtifactAsync(ObjectId JobId, SubResourceId? StepId, string Name, string MimeType, System.IO.Stream Data)
		{
			// upload first
			string ArtifactName = ValidateName(Name);
			await ExternalStorageProvider.WriteAsync(JobId, StepId, ArtifactName, Data);

			// then create entry
			Artifact NewArtifact = new Artifact(JobId, StepId, ArtifactName, Data.Length, MimeType);
			await Artifacts.InsertOneAsync(NewArtifact);
			return NewArtifact;
		}

		/// <summary>
		/// Gets all the available artifacts for a job
		/// </summary>
		/// <param name="JobId">Unique id of the job to query</param>
		/// <param name="StepId">Unique id of the Step to query</param>
		/// <param name="Name">Name of the artifact</param>
		/// <returns>List of artifact documents</returns>
		public async Task<List<Artifact>> GetArtifactsAsync(ObjectId? JobId, SubResourceId? StepId, string? Name)
		{
			FilterDefinitionBuilder<Artifact> Builder = Builders<Artifact>.Filter;

			FilterDefinition<Artifact> Filter = FilterDefinition<Artifact>.Empty;
			if (JobId != null)
			{
				Filter &= Builder.Eq(x => x.JobId, JobId.Value);
			}
			if (StepId != null)
			{
				Filter &= Builder.Eq(x => x.StepId, StepId.Value);
			}
			if (Name != null)
			{
				Filter &= Builder.Eq(x => x.Name, Name);
			}

			return await Artifacts.Find(Filter).ToListAsync();
		}

		/// <summary>
		/// Gets a specific list of artifacts based on id
		/// </summary>
		/// <param name="ArtifactIds">The list of artifact Ids</param>
		/// <returns>List of artifact documents</returns>
		public async Task<List<Artifact>> GetArtifactsAsync(IEnumerable<ObjectId> ArtifactIds)
		{
			FilterDefinitionBuilder<Artifact> Builder = Builders<Artifact>.Filter;

			FilterDefinition<Artifact> Filter = FilterDefinition<Artifact>.Empty;
			Filter &= Builder.In(x => x.Id, ArtifactIds);
			
			return await Artifacts.Find(Filter).ToListAsync();
		}

		/// <summary>
		/// Gets an artifact by ID
		/// </summary>
		/// <param name="ArtifactId">Unique id of the artifact</param>
		/// <returns>The artifact document</returns>
		public async Task<Artifact?> GetArtifactAsync(ObjectId ArtifactId)
		{
			return await Artifacts.Find<Artifact>(x => x.Id == ArtifactId).FirstOrDefaultAsync();
		}

		/// <summary>
		/// Attempts to update a single artifact document, if the update counter is valid
		/// </summary>
		/// <param name="Current">The current artifact document</param>
		/// <param name="Update">The update definition</param>
		/// <returns>True if the document was updated, false if another writer updated the document first</returns>
		private async Task<bool> TryUpdateArtifactAsync(Artifact Current, UpdateDefinition<Artifact> Update)
		{
			UpdateResult Result = await Artifacts.UpdateOneAsync<Artifact>(x => x.Id == Current.Id && x.UpdateIndex == Current.UpdateIndex, Update.Set(x => x.UpdateIndex, Current.UpdateIndex + 1));
			return Result.ModifiedCount == 1;
		}

		/// <summary>
		/// Updates an artifact
		/// </summary>
		/// <param name="Artifact">The artifact</param>
		/// <param name="NewMimeType">New mime type</param>
		/// <param name="NewData">New data</param>
		/// <returns>Async task</returns>
		public async Task<bool> UpdateArtifactAsync(Artifact? Artifact, string NewMimeType, System.IO.Stream NewData)
		{
			while (Artifact != null)
			{
				UpdateDefinitionBuilder<Artifact> UpdateBuilder = Builders<Artifact>.Update;

				List<UpdateDefinition<Artifact>> Updates = new List<UpdateDefinition<Artifact>>();
				Updates.Add(UpdateBuilder.Set(x => x.MimeType, NewMimeType));
				Updates.Add(UpdateBuilder.Set(x => x.Length, NewData.Length));

				// re-upload the data to external
				string ArtifactName = ValidateName(Artifact.Name);
				await ExternalStorageProvider.WriteAsync(Artifact.JobId, Artifact.StepId, ArtifactName, NewData);

				if (await TryUpdateArtifactAsync(Artifact, UpdateBuilder.Combine(Updates)))
				{
					return true;
				}

				Artifact = await GetArtifactAsync(Artifact.Id);
			}
			return false;
		}

		/// <summary>
		/// gets artifact data
		/// </summary>
		/// <param name="Artifact">The artifact</param>
		/// <returns>The chunk data</returns>
		public System.IO.Stream OpenArtifactReadStream(Artifact Artifact)
		{
			return ExternalStorageProvider.OpenReadStream(Artifact.JobId, Artifact.StepId, Artifact.Name);
		}

		/// <summary>
		/// Checks that the name given is valid for an artifact
		/// </summary>
		/// <param name="Name"></param>
		private static string ValidateName(string Name)
		{
			string NewName = Name.Replace('\\', '/');
			if (NewName.Length == 0)
			{
				throw new ArgumentException("Artifact name is empty");
			}
			else if (NewName.StartsWith('/'))
			{
				throw new ArgumentException($"Artifact has an absolute path ({NewName})");
			}
			else if (NewName.EndsWith('/'))
			{
				throw new ArgumentException($"Artifact does not have a file name ({NewName})");
			}
			else if (NewName.Contains("//", StringComparison.Ordinal))
			{
				throw new ArgumentException($"Artifact name contains an invalid directory ({NewName})");
			}

			string InvalidChars = ":<>|\"";
			for (int Idx = 0; Idx < NewName.Length; Idx++)
			{
				int MinIdx = Idx;
				for (; Idx < NewName.Length && NewName[Idx] != '/'; Idx++)
				{
					if (InvalidChars.Contains(NewName[Idx], StringComparison.Ordinal))
					{
						throw new ArgumentException($"Invalid character in artifact name ({NewName})");
					}
				}
				if ((Idx == MinIdx + 1 && NewName[MinIdx] == '.') || (Idx == MinIdx + 2 && NewName[MinIdx] == '.' && NewName[MinIdx + 1] == '.'))
				{
					throw new ArgumentException($"Artifact may not contain symbolic directory names ({NewName})");
				}
			}
			return NewName;
		}
	}
}