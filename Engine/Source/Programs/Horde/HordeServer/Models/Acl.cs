// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Security.Claims;

namespace HordeServer.Models
{
	/// <summary>
	/// Set of actions that can be performed by a user. NOTE: This enum is sensitive to ordering. Do not change values.
	/// </summary>
	public enum AclAction
	{
		//// PROJECTS ////

		/// <summary>
		/// Allows the creation of new projects
		/// </summary>
		CreateProject,

		/// <summary>
		/// Allows deletion of projects.
		/// </summary>
		DeleteProject,
		
		/// <summary>
		/// Modify attributes of a project (name, categories, etc...)
		/// </summary>
		UpdateProject,

		/// <summary>
		/// View information about a project
		/// </summary>
		ViewProject,




		//// STREAMS ////

		/// <summary>
		/// Allows the creation of new streams within a project
		/// </summary>
		CreateStream,

		/// <summary>
		/// Allows updating a stream (agent types, templates, schedules)
		/// </summary>
		UpdateStream,

		/// <summary>
		/// Allows deleting a stream
		/// </summary>
		DeleteStream,

		/// <summary>
		/// Ability to view a stream
		/// </summary>
		ViewStream,

		/// <summary>
		/// View changes submitted to a stream. NOTE: this returns responses from the server's Perforce account, which may be a priviledged user.
		/// </summary>
		ViewChanges,

		/// <summary>
		/// Update records in commit queues for a stream
		/// </summary>
		UpdateCommitQueues,

		/// <summary>
		/// View commit queues for a stream
		/// </summary>
		ViewCommitQueues,










		//// JOBS ////

		/// <summary>
		/// Ability to start new jobs
		/// </summary>
		CreateJob,

		/// <summary>
		/// Rename a job, modify its priority, etc...
		/// </summary>
		UpdateJob,

		/// <summary>
		/// Delete a job properties
		/// </summary>
		DeleteJob,

		/// <summary>
		/// Allows updating a job metadata (name, changelist number, step properties, new groups, job states, etc...). Typically granted to agents. Not user facing.
		/// </summary>
		ExecuteJob,

		/// <summary>
		/// Ability to retry a failed job step
		/// </summary>
		RetryJobStep,

		/// <summary>
		/// Ability to view a job
		/// </summary>
		ViewJob,






		//// EVENTS ////
		
		/// <summary>
		/// Ability to create events
		/// </summary>
		CreateEvent,

		/// <summary>
		/// Ability to view events
		/// </summary>
		ViewEvent,



		//// AGENTS ////

		/// <summary>
		/// Ability to create an agent. This may be done explicitly, or granted to agents to allow them to self-register.
		/// </summary>
		CreateAgent,

		/// <summary>
		/// Update an agent's name, pools, etc...
		/// </summary>
		UpdateAgent,

		/// <summary>
		/// Soft-delete an agent
		/// </summary>
		DeleteAgent,

		/// <summary>
		/// View an available agents
		/// </summary>
		ViewAgent,

		/// <summary>
		/// List the available agents
		/// </summary>
		ListAgents,




		//// POOLS ////

		/// <summary>
		/// Create a global pool of agents
		/// </summary>
		CreatePool,

		/// <summary>
		/// Modify an agent pool
		/// </summary>
		UpdatePool,

		/// <summary>
		/// Delete an agent pool
		/// </summary>
		DeletePool,

		/// <summary>
		/// Ability to view a pool
		/// </summary>
		ViewPool,

		/// <summary>
		/// View all the available agent pools
		/// </summary>
		ListPools,




		//// SESSIONS ////

		/// <summary>
		/// Granted to agents to call CreateSession, which returns a bearer token identifying themselves valid to call UpdateSesssion via gRPC.
		/// </summary>
		CreateSession,

		/// <summary>
		/// Allows viewing information about an agent session
		/// </summary>
		ViewSession,




		//// CREDENTIALS ////

		/// <summary>
		/// Create a new credential
		/// </summary>
		CreateCredential,

		/// <summary>
		/// Delete a credential
		/// </summary>
		DeleteCredential,

		/// <summary>
		/// Modify an existing credential
		/// </summary>
		UpdateCredential,

		/// <summary>
		/// Enumerates all the available credentials
		/// </summary>
		ListCredentials,

		/// <summary>
		/// View a credential
		/// </summary>
		ViewCredential,







		//// LEASES ////

		/// <summary>
		/// View all the leases that an agent has worked on
		/// </summary>
		ViewLeases,





		//// COMMITS ////

		/// <summary>
		/// Add a new commit
		/// </summary>
		AddCommit,

		/// <summary>
		/// List all the commits that have been added
		/// </summary>
		FindCommits,

		/// <summary>
		/// View the commits for a particular stream
		/// </summary>
		ViewCommits,






		/// <summary>
		/// View a build health issue
		/// </summary>
		ViewIssue,





		//// TEMPLATES ////

		/// <summary>
		/// View template associated with a stream
		/// </summary>
		ViewTemplate,






		//// LOGS ////

		/// <summary>
		/// Ability to create a log. Implicitly granted to agents.
		/// </summary>
		CreateLog,

		/// <summary>
		/// Ability to update log metadata
		/// </summary>
		UpdateLog,

		/// <summary>
		/// Ability to view a log contents
		/// </summary>
		ViewLog,

		/// <summary>
		/// Ability to write log data
		/// </summary>
		WriteLogData,




		//// ARTIFACTS ////

		/// <summary>
		/// Ability to create an artifact. Typically just for debugging; agents have this access for a particular session.
		/// </summary>
		UploadArtifact,

		/// <summary>
		/// Ability to download an artifact
		/// </summary>
		DownloadArtifact,




		//// SOFTWARE ////

		/// <summary>
		/// Ability to upload new versions of the agent software
		/// </summary>
		UploadSoftware,

		/// <summary>
		/// Ability to download the agent software
		/// </summary>
		DownloadSoftware,

		/// <summary>
		/// Ability to delete agent software
		/// </summary>
		DeleteSoftware,




		//// ADMIN ////

		/// <summary>
		/// Ability to read any data from the server. Always inherited.
		/// </summary>
		AdminRead,

		/// <summary>
		/// Ability to write any data to the server.
		/// </summary>
		AdminWrite,

		/// <summary>
		/// Ability to impersonate another user
		/// </summary>
		Impersonate,





		//// PERMISSIONS ////
		
		/// <summary>
		/// Ability to view permissions on an object
		/// </summary>
		ViewPermissions,

		/// <summary>
		/// Ability to change permissions on an object
		/// </summary>
		ChangePermissions,

		/// <summary>
		/// Issue bearer token for the current user
		/// </summary>
		IssueBearerToken,





		//// NOTIFICATIONS ////

		/// <summary>
		/// Ability to subscribe to notifications
		/// </summary>
		CreateSubscription,





		//// DEVICES ////
		
		/// <summary>
		/// Ability to read devices
		/// </summary>
		DeviceRead,

		/// <summary>
		/// Ability to write devices
		/// </summary>
		DeviceWrite,





		//// STORAGE ////

		/// <summary>
		/// Ability to read blobs from the storage service
		/// </summary>
		ReadBlobs,

		/// <summary>
		/// Ability to write blobs to the storage service
		/// </summary>
		WriteBlobs,

		/// <summary>
		/// Ability to read refs from the storage service
		/// </summary>
		ReadRefs,

		/// <summary>
		/// Ability to write refs to the storage service
		/// </summary>
		WriteRefs, 

		/// <summary>
		/// Ability to delete refs
		/// </summary>
		DeleteRefs,
	}

	/// <summary>
	/// Stores information about a claim
	/// </summary>
	public class AclClaim
	{
		/// <summary>
		/// The claim type, typically a URI
		/// </summary>
		public string Type { get; set; }

		/// <summary>
		/// The claim value
		/// </summary>
		public string Value { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Claim">The claim object</param>
		public AclClaim(Claim Claim)
			: this(Claim.Type, Claim.Value)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">The claim type</param>
		/// <param name="Value">The claim value</param>
		public AclClaim(string Type, string Value)
		{
			this.Type = Type;
			this.Value = Value;
		}

		/// <summary>
		/// Constructs a claim from a request object
		/// </summary>
		/// <param name="Request">The request object</param>
		public AclClaim(CreateAclClaimRequest Request)
			: this(Request.Type, Request.Value)
		{
		}
	}

	/// <summary>
	/// Describes an entry in the ACL for a particular claim
	/// </summary>
	public class AclEntry
	{
		/// <summary>
		/// Claim for this entry
		/// </summary>
		public AclClaim Claim { get; set; }

		/// <summary>
		/// List of allowed operations
		/// </summary>
		[BsonSerializer(typeof(AclActionSetSerializer))]
		public HashSet<AclAction> Actions { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		private AclEntry()
		{
			Claim = new AclClaim(String.Empty, String.Empty);
			Actions = new HashSet<AclAction>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Claim">The claim this entry applies to</param>
		/// <param name="Actions">List of allowed operations</param>
		internal AclEntry(AclClaim Claim, IEnumerable<AclAction> Actions)
		{
			this.Claim = Claim;
			this.Actions = new HashSet<AclAction>(Actions);
		}

		/// <summary>
		/// Constructs an ACL entry from a request
		/// </summary>
		/// <param name="Request">Request instance</param>
		/// <returns>New ACL entry</returns>
		public static AclEntry FromRequest(CreateAclEntryRequest Request)
		{
			return new AclEntry(new AclClaim(Request.Claim.Type, Request.Claim.Value), ParseActionNames(Request.Actions).ToArray());
		}

		/// <summary>
		/// Parses a list of names as an AclAction bitmask
		/// </summary>
		/// <param name="ActionNames">Array of names</param>
		/// <returns>Action bitmask</returns>
		public static List<AclAction> ParseActionNames(string[]? ActionNames)
		{
			List<AclAction> Actions = new List<AclAction>();
			if (ActionNames != null)
			{
				foreach (string Name in ActionNames)
				{
					Actions.Add(Enum.Parse<AclAction>(Name, true));
				}
			}
			return Actions;
		}

		/// <summary>
		/// Build a list of action names from an array of flags
		/// </summary>
		/// <param name="Actions"></param>
		/// <returns></returns>
		public static List<string> GetActionNames(IEnumerable<AclAction> Actions)
		{
			List<string> ActionNames = new List<string>();
			foreach(AclAction Action in Actions)
			{
				string Name = Enum.GetName(typeof(AclAction), Action)!;
				ActionNames.Add(Name);
			}
			return ActionNames;
		}
	}

	/// <summary>
	/// Represents an access control list for an object in the database
	/// </summary>
	public class Acl
	{
		/// <summary>
		/// List of entries for this ACL
		/// </summary>
		public List<AclEntry> Entries { get; set; }

		/// <summary>
		/// Whether to inherit permissions from the parent ACL by default
		/// </summary>
		public bool Inherit { get; set; } = true;

		/// <summary>
		/// Specifies a list of exceptions to the inheritance setting
		/// </summary>
		public List<AclAction>? Exceptions { get; set; }


		/// <summary>
		/// Default constructor
		/// </summary>
		public Acl()
		{
			Entries = new List<AclEntry>();
			Inherit = true;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Entries">List of entries for this ACL</param>
		/// <param name="bInheritPermissions">Whether to inherit permissions from the parent object by default</param>
		public Acl(List<AclEntry> Entries, bool bInheritPermissions)
		{
			this.Entries = Entries;
			this.Inherit = bInheritPermissions;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Entries">List of entries for this ACL</param>
		public Acl(params AclEntry[] Entries)
			: this(Entries.ToList(), true)
		{
		}

		/// <summary>
		/// Tests whether a user is authorized to perform the given actions
		/// </summary>
		/// <param name="Action">Action that is being performed. This should be a single flag.</param>
		/// <param name="User">The principal to authorize</param>
		/// <returns>True/false if the action is allowed or denied, null if there is no specific setting for this user</returns>
		public bool? Authorize(AclAction Action, ClaimsPrincipal User)
		{
			// Check if there's a specific entry for this action
			foreach (AclEntry Entry in Entries)
			{
				if(Entry.Actions.Contains(Action) && User.HasClaim(Entry.Claim.Type, Entry.Claim.Value))
				{
					return true;
				}
			}

			// Otherwise check if we're prevented from inheriting permissions
			if(Inherit)
			{
				if(Exceptions != null && Exceptions.Contains(Action))
				{
					return false;
				}
			}
			else
			{
				if (Exceptions == null || !Exceptions.Contains(Action))
				{
					return false;
				}
			}

			// Otherwise allow to propagate up the hierarchy
			return null;
		}

		/// <summary>
		/// Merge new settings into an ACL
		/// </summary>
		/// <param name="BaseAcl">The current acl</param>
		/// <param name="Update">The update to apply</param>
		/// <returns>The new ACL value. Null if the ACL has all default settings.</returns>
		public static Acl? Merge(Acl? BaseAcl, UpdateAclRequest? Update)
		{
			Acl? NewAcl = null;
			if(Update != null)
			{
				NewAcl = new Acl();

				if(Update.Entries != null)
				{
					NewAcl.Entries = Update.Entries.ConvertAll(x => AclEntry.FromRequest(x));
				}
				else if(BaseAcl != null)
				{
					NewAcl.Entries = BaseAcl.Entries;
				}

				if(Update.Inherit != null)
				{
					NewAcl.Inherit = Update.Inherit.Value;
				}
				else if(BaseAcl != null)
				{
					NewAcl.Inherit = BaseAcl.Inherit;
				}
			}
			return NewAcl;
		}

		/// <summary>
		/// Creates an update definition for the given ACL. Clears the ACL property if it's null.
		/// </summary>
		/// <typeparam name="T">Type of document containing the ACL</typeparam>
		/// <param name="Field">Selector for the ACL property</param>
		/// <param name="NewAcl">The new ACL value</param>
		/// <returns>Update definition for the document</returns>
		public static UpdateDefinition<T> CreateUpdate<T>(Expression<Func<T, object>> Field, Acl NewAcl)
		{
			if (NewAcl.Entries.Count == 0 && NewAcl.Inherit)
			{
				return Builders<T>.Update.Unset(Field);
			}
			else
			{
				return Builders<T>.Update.Set(Field, NewAcl);
			}
		}
	}

	/// <summary>
	/// Serializer for JobStepRefId objects
	/// </summary>
	public sealed class AclActionSetSerializer : IBsonSerializer<HashSet<AclAction>>
	{
		/// <inheritdoc/>
		public Type ValueType
		{
			get { return typeof(HashSet<AclAction>); }
		}

		/// <inheritdoc/>
		void IBsonSerializer.Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, object Value)
		{
			Serialize(Context, Args, (HashSet<AclAction>)Value);
		}

		/// <inheritdoc/>
		object IBsonSerializer.Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return ((IBsonSerializer<HashSet<AclAction>>)this).Deserialize(Context, Args);
		}

		/// <inheritdoc/>
		public HashSet<AclAction> Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			HashSet<AclAction> Values = new HashSet<AclAction>();

			Context.Reader.ReadStartArray();
			for(; ;)
			{
				BsonType Type = Context.Reader.ReadBsonType();
				if(Type == BsonType.EndOfDocument)
				{
					break;
				}
				else if (Type == BsonType.Int32)
				{
					Values.Add((AclAction)Context.Reader.ReadInt32());
				}
				else
				{
					Values.Add((AclAction)Enum.Parse(typeof(AclAction), Context.Reader.ReadString()));
				}
			}
			Context.Reader.ReadEndArray();

			return Values;
		}

		/// <inheritdoc/>
		public void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, HashSet<AclAction> Values)
		{
			Context.Writer.WriteStartArray();
			foreach (AclAction Value in Values)
			{
				Context.Writer.WriteString(Value.ToString());
			}
			Context.Writer.WriteEndArray();
		}
	}

	/// <summary>
	/// Represents an access control list for an object in the database
	/// </summary>
	[SingletonDocument("5e39b51941b875626e600193")]
	public class GlobalPermissions : SingletonBase
	{
		/// <summary>
		/// The global ACL object
		/// </summary>
		public Acl Acl { get; set; } = new Acl();
	}
}
