// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Perforce.Fixture;

using FileSet = HashSet<(string clientFile, long size, string digest)>;

public class DepotFileFixture
{
	public string DepotFile { get; }
	public string ClientFile { get; }
	public long Size { get; }
	public string Digest { get; }
	public string Content { get; }
	
	public DepotFileFixture(string depotFile, string clientFile, string content)
	{
		DepotFile = depotFile;
		ClientFile = clientFile;
		Content = content;
		Size = content.Length;
		Digest = PerforceFixture.CalcMd5(content);
	}
}

public class ChangelistFixture
{
	public int Number { get; }
	public string Description { get; }
	
	/// <summary>
	/// List of files in stream as how they would appear locally on disk, when synced to this changelist
	/// (after any view maps have been applied)
	/// </summary>
	public IReadOnlyList<DepotFileFixture> StreamFiles { get; }

	public ChangelistFixture(int number, string description, List<DepotFileFixture> streamFiles)
	{
		Number = number;
		Description = description;
		StreamFiles = streamFiles;
	}

	/// <summary>
	/// Assert directory contains exactly the files described by stream
	/// </summary>
	/// <param name="clientRoot">Client/workspace root directory</param>
	public void AssertDepotFiles(string clientRoot)
	{
		(FileSet actual, FileSet expected) = GetFileSets(clientRoot);

		if (!expected.SetEquals(actual))
		{
			List<(string clientFile, long size, string digest)> expectedList = new(expected);
			List<(string clientFile, long size, string digest)> actualList = new(actual);
			
			expectedList.Sort();
			actualList.Sort();
			
			Console.WriteLine("Expected ------------------------------------------------------");
			foreach ((string clientFile, long size, string digest) in expectedList)
			{
				Console.WriteLine($"{clientFile,-20} | {size,5} | {digest}");
			}
			
			Console.WriteLine("");
			Console.WriteLine("Actual --------------------------------------------------------");
			foreach ((string clientFile, long size, string digest) in actualList)
			{
				Console.WriteLine($"{clientFile,-20} | {size,5} | {digest}");
			}

			Assert.Fail("Files on disk does not match files in stream at given CL");
		}
	}
	
	private (FileSet localFiles, FileSet streamFiles) GetFileSets(string clientRoot)
	{
		EnumerationOptions options = new () { RecurseSubdirectories = true };

		FileSet streamFiles = new(StreamFiles.Select(x => (x.ClientFile, x.Size, x.Digest)));
		FileSet localFiles = new(Directory.EnumerateFiles(clientRoot, "*", options)
			.Select(x => Path.GetRelativePath(clientRoot, x))
			.Select(x => x.Replace("\\", "/", StringComparison.Ordinal))
			.Select(clientFile =>
			{
				string absPath = Path.Join(clientRoot, clientFile);
				string content = File.ReadAllText(absPath);
				
				// Since content of files in fixture are only single lines, the trick below works to workaround
				// differences in line endings after sync (client vs server).
				content = content.Replace("\r\n", "", StringComparison.Ordinal);
				long size = content.Length;
				return (clientFile, size, PerforceFixture.CalcMd5(content));
			}));
		
		return (localFiles, streamFiles);
	}

	public (List<string> localFiles, List<string> streamFiles) GetFiles(string clientRoot)
	{
		EnumerationOptions options = new () { RecurseSubdirectories = true };
		List<string> localFiles = Directory.EnumerateFiles(clientRoot, "*", options)
			.Select(x => Path.GetRelativePath(clientRoot, x))
			.Select(x => x.Replace("\\", "/", StringComparison.Ordinal))
			.ToList();
		List<string> streamFiles = new(StreamFiles.Select(x => x.ClientFile));
		
		localFiles.Sort();
		streamFiles.Sort();
		
		return (localFiles, streamFiles);
	}
}

public class StreamFixture
{
	public string Root { get; }
	public IReadOnlyList<ChangelistFixture> Changelists { get; }

	public StreamFixture(string root, IReadOnlyList<ChangelistFixture> changelists)
	{
		Root = root;
		Changelists = changelists;
	}
}

public class PerforceFixture
{
	public StreamFixture StreamFooMain { get; } = new ("//Foo/Main",
		new List<ChangelistFixture>
		{
			new(0, "<placeholder>", new List<DepotFileFixture>()),
			new(1, "<placeholder>", new List<DepotFileFixture>()),
			new(2, "Initial import",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #1"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #1"),
					new("//Foo/Main/common.h", "common.h", "This is change common.h #1"),
					new("//Foo/Main/unused.cpp", "unused.cpp", "This is change unused.cpp #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
				}),
			new(3, "Improvement to main.cpp",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #2"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #1"),
					new("//Foo/Main/common.h", "common.h", "This is change common.h #1"),
					new("//Foo/Main/unused.cpp", "unused.cpp", "This is change unused.cpp #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
				}),
			new(4, "Delete an unused file",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #2"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #1"),
					new("//Foo/Main/common.h", "common.h", "This is change common.h #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
				}),
			new(5, "Rename common.h to shared.h",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #2"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #1"),
					new("//Foo/Main/shared.h", "shared.h", "This is change common.h #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
				}),
			new(6, "Some updates to main",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #3"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #2"),
					new("//Foo/Main/shared.h", "shared.h", "This is change common.h #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
				}),
			new(7, "Add more data",
				new List<DepotFileFixture>()
				{
					new("//Foo/Main/main.cpp", "main.cpp", "This is change main.cpp #3"),
					new("//Foo/Main/main.h", "main.h", "This is change main.h #2"),
					new("//Foo/Main/shared.h", "shared.h", "This is change common.h #1"),
					new("//Foo/Main/Data/data.txt", "Data/data.txt", "This is change data.txt #1"),
					new("//Foo/Main/Data/moredata.txt", "Data/moredata.txt", "This is change moredata.txt #1"),
				}),
		});

	public static string CalcMd5(string content)
	{
		byte[] data = Encoding.ASCII.GetBytes(content);
		return Md5Hash.Compute(data).ToString();
	}
}