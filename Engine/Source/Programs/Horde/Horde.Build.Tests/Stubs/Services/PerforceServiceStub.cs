// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce;
using Horde.Build.Perforce;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;

namespace Horde.Build.Tests.Stubs.Services
{
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	class PerforceServiceStub : IPerforceService
	{
		class User : IUser
		{
			public UserId Id { get; }
			public string Name { get; set; }
			public string Email { get; set; }
			public string Login { get; set; }

			public User(string login)
			{
				Id = UserId.GenerateNewId();
				Name = login.ToUpperInvariant();
				Email = $"{login}@server";
				Login = login;
			}
		}

		public class Commit : ICommit
		{
			public StreamId StreamId { get; }
			public int Number { get; }
			public int OriginalChange { get; }
			public UserId AuthorId { get; }
			public UserId OwnerId { get; }
			public string Description { get; }
			public string BasePath => throw new NotImplementedException();
			public DateTime DateUtc => throw new NotImplementedException();
			public List<string> Files { get; }

			public Commit(StreamId streamId, int number, int originalChange, UserId authorId, UserId ownerId, string description, List<string> files)
			{
				StreamId = streamId;
				Number = number;
				OriginalChange = originalChange;
				AuthorId = authorId;
				OwnerId = ownerId;
				Description = description;
				Files = files;
			}

			public ValueTask<IReadOnlyList<string>> GetFilesAsync(CancellationToken cancellationToken)
			{
				return new ValueTask<IReadOnlyList<string>>(Files);
			}
		}

		class ChangeComparer : IComparer<int>
		{
			public int Compare(int x, int y) => y.CompareTo(x);
		}

		public Dictionary<StreamId, SortedDictionary<int, Commit>> Changes { get; } = new Dictionary<StreamId, SortedDictionary<int, Commit>>();

		readonly IUserCollection _userCollection;

		public PerforceServiceStub(IUserCollection userCollection)
		{
			_userCollection = userCollection;
		}

		public Task<IPerforceConnection> ConnectAsync(string? clusterName, string? userName, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}

		public async ValueTask<IUser> FindOrAddUserAsync(string clusterName, string userName, CancellationToken cancellationToken)
		{
			return await _userCollection.FindOrAddUserByLoginAsync(userName);
		}

		public void AddChange(StreamId streamId, int number, IUser author, string description, IEnumerable<string> files)
		{
			AddChange(streamId, number, number, author, author, description, files);
		}

		public void AddChange(StreamId streamId, int number, int originalNumber, IUser author, IUser owner, string description, IEnumerable<string> files)
		{
			SortedDictionary<int, Commit>? streamChanges;
			if (!Changes.TryGetValue(streamId, out streamChanges))
			{
				streamChanges = new SortedDictionary<int, Commit>(new ChangeComparer());
				Changes[streamId] = streamChanges;
			}
			streamChanges.Add(number, new Commit(streamId, number, originalNumber, author.Id, owner.Id, description, files.ToList()));
		}

		public Task<List<ICommit>> GetChangesAsync(IStream stream, int? minChange, int? maxChange, int? numResults, CancellationToken cancellationToken)
		{
			List<ICommit> results = new List<ICommit>();

			SortedDictionary<int, Commit>? streamChanges;
			if (Changes.TryGetValue(stream.Id, out streamChanges) && streamChanges.Count > 0)
			{
				foreach (Commit details in streamChanges.Values)
				{
					if (minChange.HasValue && details.Number < minChange)
					{
						break;
					}
					if (!maxChange.HasValue || details.Number <= maxChange.Value)
					{
						results.Add(details);
					}
					if (numResults != null && numResults > 0 && results.Count >= numResults)
					{
						break;
					}
				}
			}

			return Task.FromResult(results);
		}

		public Task<(CheckShelfResult, string?)> CheckShelfAsync(string clusterName, string streamName, int changeNumber, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}

		public Task<List<ICommit>> GetChangeDetailsAsync(IStream stream, IReadOnlyList<int> changeNumbers, CancellationToken cancellationToken)
		{
			List<ICommit> results = new List<ICommit>();
			foreach (int changeNumber in changeNumbers)
			{
				results.Add(Changes[stream.Id][changeNumber]);
			}
			return Task.FromResult(results);
		}

		public static Task<string> CreateTicket()
		{
			return Task.FromResult("bogus-ticket");
		}

		public Task<int> GetCodeChangeAsync(IStream stream, int change, CancellationToken cancellationToken)
		{
			int codeChange = 0;

			SortedDictionary<int, Commit>? streamChanges;
			if (Changes.TryGetValue(stream.Id, out streamChanges))
			{
				foreach (Commit details in streamChanges.Values)
				{
					if (details.Number <= change && details.Files.Any(x => x.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || x.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase)))
					{
						codeChange = details.Number;
						break;
					}
				}
			}

			return Task.FromResult(codeChange);
		}

		public Task<int> CreateNewChangeAsync(string clusterName, string streamName, string path, string description, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}

		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string clusterName, int shelvedChange, int originalChange, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}

		public Task<int> DuplicateShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken)
		{
			return Task.FromResult(shelvedChange);
		}

		public Task DeleteShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken)
		{
			return Task.CompletedTask;
		}

		public Task UpdateChangelistDescription(string clusterName, int change, string description, CancellationToken cancellationToken)
		{
			return Task.CompletedTask;
		}

		public Task<ICommit> GetChangeDetailsAsync(string clusterName, int changeNumber, CancellationToken cancellationToken)
		{
			throw new NotImplementedException();
		}

		public Task<ICommit> GetChangeDetailsAsync(IStream stream, int changeNumber, CancellationToken cancellationToken)
		{
			return Task.FromResult<ICommit>(Changes[stream.Id][changeNumber]);
		}
	}
}
