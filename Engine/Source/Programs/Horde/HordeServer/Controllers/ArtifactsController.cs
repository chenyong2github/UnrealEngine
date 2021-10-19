// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.S3.Transfer;
using HordeServer.Api;
using HordeServer.Jobs;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.StaticFiles;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IdentityModel.Tokens.Jwt;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http.Headers;
using System.Net.Mime;
using System.Security.Claims;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Controller for the /api/artifacts endpoint
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class ArtifactsController : ControllerBase
	{
		/// <summary>
		/// Instance of the database service
		/// </summary>
		private readonly DatabaseService DatabaseService;

		/// <summary>
		/// Instance of the artifact collection
		/// </summary>
		private readonly IArtifactCollection ArtifactCollection;

		/// <summary>
		/// Instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Instance of the Job service
		/// </summary>
		private readonly JobService JobService;

		/// <summary>
		/// Constructor
		/// </summary>
		public ArtifactsController(DatabaseService DatabaseSerivce, IArtifactCollection ArtifactCollection, AclService AclService, JobService JobService)
		{
			this.DatabaseService = DatabaseSerivce;
			this.ArtifactCollection = ArtifactCollection;
			this.AclService = AclService;
			this.JobService = JobService;
		}

		/// <summary>
		/// Creates an artifact
		/// </summary>
		/// <param name="JobId">BatchId</param>
		/// <param name="StepId">StepId</param>
		/// <param name="File">The file contents</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/artifacts")]
		public async Task<ActionResult<CreateArtifactResponse>> CreateArtifact([FromQuery] JobId JobId, [FromQuery]string? StepId, IFormFile File)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if(Job == null)
			{
				return NotFound();
			}

			if (!await JobService.AuthorizeAsync(Job, AclAction.UploadArtifact, User, null))
			{
				return Forbid();
			}

			IJobStep? Step = null;
			if(StepId != null)
			{
				foreach(IJobStepBatch Batch in Job.Batches)
				{
					if(Batch.TryGetStep(StepId.ToSubResourceId(), out Step))
					{
						break;
					}
				}
				if(Step == null)
				{
					// if the step doesn't exist in any of the batches, not found
					return NotFound();
				}
			}

			IArtifact NewArtifact = await ArtifactCollection.CreateArtifactAsync(Job.Id, Step?.Id, File.FileName, File.ContentType ?? "horde-mime/unknown", File.OpenReadStream());
			return new CreateArtifactResponse(NewArtifact.Id.ToString());
		}

		/// <summary>
		/// Updates an artifact
		/// </summary>
		/// <param name="ArtifactId">JobId</param>
		/// <param name="File">The file contents</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Authorize]
		[Route("/api/v1/artifacts/{ArtifactId}")]
		public async Task<ActionResult<CreateArtifactResponse>> UpdateArtifact(string ArtifactId, IFormFile File)
		{
			IArtifact? Artifact = await ArtifactCollection.GetArtifactAsync(ArtifactId.ToObjectId());
			if (Artifact == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Artifact.JobId, AclAction.UploadArtifact, User, null))
			{
				return Forbid();
			}

			await ArtifactCollection.UpdateArtifactAsync(Artifact, File.ContentType ?? "horde-mime/unknown", File.OpenReadStream());
			return Ok();
		}

		/// <summary>
		/// Query artifacts for a job step
		/// </summary>
		/// <param name="JobId">Optional JobId to filter by</param>
		/// <param name="StepId">Optional StepId to filter by</param>
		/// <param name="Code">Whether to generate a direct download code</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the artifacts</returns>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/artifacts")]
		[ProducesResponseType(typeof(List<GetArtifactResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetArtifacts([FromQuery] JobId JobId, [FromQuery] string? StepId = null, [FromQuery] bool Code = false, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await JobService.AuthorizeAsync(JobId, AclAction.DownloadArtifact, User, null))
			{
				return Forbid();
			}

			string? DownloadCode = Code ? (string?)GetDirectDownloadCodeForJob(JobId) : null;

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepId?.ToSubResourceId(), null);
			return Artifacts.ConvertAll(x => new GetArtifactResponse(x, DownloadCode).ApplyFilter(Filter));
		}

		/// <summary>
		/// Gets the claim required to download artifacts for a particular job
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <returns>The required claim</returns>
		static Claim GetDirectDownloadClaim(JobId JobId)
		{
			return new Claim(HordeClaimTypes.JobArtifacts, JobId.ToString());
		}

		/// <summary>
		/// Get a download code for the artifacts of a job
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <returns>The download code</returns>
		string GetDirectDownloadCodeForJob(JobId JobId)
		{
			Claim DownloadClaim = GetDirectDownloadClaim(JobId);
			return AclService.IssueBearerToken(new[] { DownloadClaim }, TimeSpan.FromHours(4.0));
		}

		/// <summary>
		/// Retrieve metadata about a specific artifact
		/// </summary>
		/// <param name="ArtifactId">Id of the artifact to get information about</param>
		/// <param name="Code">Whether to generate a direct download code</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/artifacts/{ArtifactId}")]
		[ProducesResponseType(typeof(GetArtifactResponse), 200)]
		public async Task<ActionResult<object>> GetArtifact(string ArtifactId, bool Code = false, [FromQuery] PropertyFilter? Filter = null)
		{
			IArtifact? Artifact = await ArtifactCollection.GetArtifactAsync(ArtifactId.ToObjectId());
			if (Artifact == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Artifact.JobId, AclAction.DownloadArtifact, User, null))
			{
				return Forbid();
			}

			string? DownloadCode = Code? (string?)GetDirectDownloadCodeForJob(Artifact.JobId) : null;
			return new GetArtifactResponse(Artifact, DownloadCode).ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieve raw data for an artifact
		/// </summary>
		/// <param name="ArtifactId">Id of the artifact to get information about</param>
		/// <returns>Raw artifact data</returns>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/artifacts/{ArtifactId}/data")]
		public async Task<ActionResult> GetArtifactData(string ArtifactId)
		{
			IArtifact? Artifact = await ArtifactCollection.GetArtifactAsync(ArtifactId.ToObjectId());
			if (Artifact == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Artifact.JobId, AclAction.DownloadArtifact, User, null))
			{
				return Forbid();
			}

			// Fun, filestream result automatically closes the stream!
			return new FileStreamResult(await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact), Artifact.MimeType);
		}
		
		/// <summary>
		/// Retrieve raw data for an artifact by filename
		/// </summary>
		/// <param name="JobId">Unique id for the job</param>
		/// <param name="StepId">Unique id for the step</param>
		/// <param name="Filename">Filename of artifact from step</param>
		/// <returns>Raw artifact data</returns>
		[HttpGet]
		[Route("/api/v1/jobs/{JobId}/steps/{StepId}/artifacts/{FileName}/data")]
		public async Task<ActionResult<object>> GetArtifactDataByFilename(JobId JobId, string StepId, string Filename)
		{
			SubResourceId StepIdValue = StepId.ToSubResourceId();

			if (!await JobService.AuthorizeAsync(JobId, AclAction.DownloadArtifact, User, null))
			{
				return Forbid();
			}
			
			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(JobId, StepIdValue, Filename);
			if (Artifacts.Count == 0)
			{
				return NotFound();
			}

			IArtifact Artifact = Artifacts[0];
			return new FileStreamResult(await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact), Artifact.MimeType);
		}

		/// <summary>
		/// Class to return a file stream without the "content-disposition: attachment" header
		/// </summary>
		class InlineFileStreamResult : FileStreamResult
		{
			/// <summary>
			/// The suggested download filename
			/// </summary>
			string FileName;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Stream"></param>
			/// <param name="MimeType"></param>
			/// <param name="FileName"></param>
			public InlineFileStreamResult(System.IO.Stream Stream, string MimeType, string FileName)
				: base(Stream, MimeType)
			{
				this.FileName = FileName;
			}

			/// <inheritdoc/>
			public override Task ExecuteResultAsync(ActionContext Context)
			{
				ContentDisposition ContentDisposition = new ContentDisposition();
				ContentDisposition.Inline = true;
				ContentDisposition.FileName = FileName;
				Context.HttpContext.Response.Headers.Add("Content-Disposition", ContentDisposition.ToString());
				
				return base.ExecuteResultAsync(Context);
			}
		}

		/// <summary>
		/// Retrieve raw data for an artifact
		/// </summary>
		/// <param name="ArtifactId">Id of the artifact to get information about</param>
		/// <param name="Code">The authorization code for this resource</param>
		/// <returns>Raw artifact data</returns>
		[HttpGet]
		[AllowAnonymous]
		[Route("/api/v1/artifacts/{ArtifactId}/download")]
		public async Task<ActionResult> DownloadArtifact(string ArtifactId, [FromQuery] string Code)
		{
			TokenValidationParameters Parameters = new TokenValidationParameters();
			Parameters.ValidateAudience = false;
			Parameters.RequireExpirationTime = true;
			Parameters.ValidateLifetime = true;
			Parameters.ValidIssuer = DatabaseService.JwtIssuer;
			Parameters.ValidateIssuer = true;
			Parameters.ValidateIssuerSigningKey = true;
			Parameters.IssuerSigningKey = DatabaseService.JwtSigningKey;

			SecurityToken Token;
			JwtSecurityTokenHandler Handler = new JwtSecurityTokenHandler();
			ClaimsPrincipal Principal = Handler.ValidateToken(Code, Parameters, out Token);

			IArtifact? Artifact = await ArtifactCollection.GetArtifactAsync(ArtifactId.ToObjectId());
			if (Artifact == null)
			{
				return NotFound();
			}

			Claim DirectDownloadClaim = GetDirectDownloadClaim(Artifact.JobId);
			if (!Principal.HasClaim(DirectDownloadClaim.Type, DirectDownloadClaim.Value))
			{
				return Forbid();
			}

			return new InlineFileStreamResult(await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact), Artifact.MimeType, Path.GetFileName(Artifact.Name));
		}

		/// <summary>
		/// Returns a zip archive of many artifacts
		/// </summary>
		/// <param name="ArtifactZipRequest">Artifact request params</param>
		/// <returns>Zip of many artifacts</returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v1/artifacts/zip")]
		public async Task<ActionResult> ZipArtifacts(GetArtifactZipRequest ArtifactZipRequest)
		{
			if (ArtifactZipRequest.JobId == null)
			{
				return BadRequest("Must specify a JobId");
			}

			IJob? Job = await JobService.GetJobAsync(new JobId(ArtifactZipRequest.JobId!));
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.DownloadArtifact, User, null))
			{
				return Forbid();
			}

			List<IArtifact> Artifacts = await ArtifactCollection.GetArtifactsAsync(Job.Id, ArtifactZipRequest.StepId?.ToSubResourceId(), null);

			Dictionary<ObjectId, IArtifact> IdToArtifact = Artifacts.ToDictionary(x => x.Id, x => x);

			List<IArtifact> ZipArtifacts;
			if (ArtifactZipRequest.ArtifactIds == null)
			{
				ZipArtifacts = Artifacts;
			}
			else
			{
				ZipArtifacts = new List<IArtifact>();
				foreach (string ArtifactId in ArtifactZipRequest.ArtifactIds)
				{
					IArtifact? Artifact;
					if (IdToArtifact.TryGetValue(ArtifactId.ToObjectId(), out Artifact))
					{
						ZipArtifacts.Add(Artifact);
					}
					else
					{
						return NotFound();
					}
				}
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);

			return new CustomFileCallbackResult("Artifacts.zip", "application/octet-stream", false, async (OutputStream, Context) =>
			{
				// Make an unseekable MemoryStream for the ZipArchive. We have to do this because the ZipEntry stream falls back to a synchronous write to it's own stream wrappers.
				using (CustomBufferStream ZipOutputStream = new CustomBufferStream())
				{
					// Keep the stream open after dispose so we can write the EOF bits.
					using (ZipArchive ZipArchive = new ZipArchive(ZipOutputStream, ZipArchiveMode.Create, true))
					{
						foreach (IArtifact Artifact in ZipArtifacts)
						{
							await using (System.IO.Stream ArtifactStream = await ArtifactCollection.OpenArtifactReadStreamAsync(Artifact))
							{
								// tack on the step name into the directory if it exists
								string StepName = string.Empty;
								if (Artifact.StepId.HasValue)
								{
									foreach (IJobStepBatch Batch in Job.Batches)
									{
										IJobStep? Step;
										if (Batch.TryGetStep(Artifact.StepId.Value, out Step))
										{
											StepName = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx].Name;
											break;
										}
									}
								}

								ZipArchiveEntry ZipEntry = ZipArchive.CreateEntry(Artifact.Name);
								using (System.IO.Stream EntryStream = ZipEntry.Open())
								{
									byte[] Buffer = new byte[4096];
									int TotalBytesRead = 0;
									while (TotalBytesRead < Artifact.Length)
									{
										int BytesRead = await ArtifactStream.ReadAsync(Buffer, 0, Buffer.Length);

										// Write bytes to the entry stream.  Also advances the MemStream pos.
										await EntryStream.WriteAsync(Buffer, 0, BytesRead);
										// Dump what we have to the output stream
										await OutputStream.WriteAsync(ZipOutputStream.GetBuffer(), 0, (int)ZipOutputStream.Position);

										// Reset everything.
										ZipOutputStream.Position = 0;
										ZipOutputStream.SetLength(0);
										TotalBytesRead += BytesRead;
									}
								}
							}
						}
					}
					// Write out the EOF stuff
					ZipOutputStream.Position = 0;
					await ZipOutputStream.CopyToAsync(OutputStream);
				}
			});
		}
	}
}
