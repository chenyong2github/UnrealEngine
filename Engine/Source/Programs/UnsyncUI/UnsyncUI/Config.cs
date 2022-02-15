// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;
using System.Xml.Linq;

namespace UnsyncUI
{
	public sealed class Config
	{
		public sealed class Proxy
		{
			public string Name { get; set; }
			public string Path { get; set; }
		}

		public struct BuildTemplate
		{
			public string Stream;
			public string CL;
			public string Suffix;
			public string Platform;

			public bool IsStreamClFound => Stream != null && CL != null;
		}

		public sealed class Directory
		{
			public Regex Regex { get; }
			public string Stream { get; }
			public string Suffix { get; }
			public string CL { get; }
			public string Platform { get; }
			public List<Directory> SubDirectories { get; }

			public Directory(XElement node)
			{
				Regex = new Regex($@"^{node.Attribute("regex")?.Value}$", RegexOptions.IgnoreCase);
				Stream = node.Attribute("stream")?.Value;
				Suffix = node.Attribute("suffix")?.Value;
				CL = node.Attribute("cl")?.Value;
				Platform = node.Attribute("platform")?.Value;
				SubDirectories = node.Elements("dir").Select(d => new Directory(d)).ToList();
			}

			public bool Parse(string path, ref BuildTemplate template)
			{
				var match = Regex.Match(Path.GetFileName(path));
				if (!match.Success)
					return false;

				string RegexReplace(string field) => Regex.Replace(field, @"\$([0-9]+)", m => match.Groups[int.Parse(m.Groups[1].Value)].Value);

				if (Stream != null)
				{
					template.Stream = RegexReplace(Stream);
				}
				if (CL != null)
				{
					template.CL = RegexReplace(CL);
				}
				if (Suffix != null)
				{
					template.Suffix = RegexReplace(Suffix);
				}
				if (Platform != null)
				{
					template.Platform = RegexReplace(Platform);
				}

				return true;
			}
		}

		public sealed class Project
		{
			public string Name { get; set; }
			public string Root { get; set; }
			public string Destination { get; set; }
			public List<string> Exclusions { get; set; }
			public List<Directory> Children { get; set; }

			private Task EnumerateBuilds(ITargetBlock<BuildModel> pipe, Directory d, string path, BuildTemplate template, CancellationToken cancelToken)
			{
				return Task.Run(async () =>
				{
					if (!d.Parse(path, ref template))
						return;

					if (template.IsStreamClFound)
					{
						pipe.Post(new BuildModel(path, d, template));
					}
					else
					{
						var jobs = new List<Task>();
						foreach (var childDir in await AsyncIO.EnumerateDirectoriesAsync(path, cancelToken))
						{
							jobs.AddRange(d.SubDirectories.Select(s => EnumerateBuilds(pipe, s, childDir, template, cancelToken)));
						}

						await Task.WhenAll(jobs);
					}
				});
			}

			public ISourceBlock<BuildModel> EnumerateBuilds(CancellationToken cancelToken)
			{
				var pipe = new BufferBlock<BuildModel>();
				Task.Run(async () =>
				{
					try
					{
						var allTasks = new List<Task>();
						foreach (var dir in await AsyncIO.EnumerateDirectoriesAsync(Root, cancelToken))
						{
							allTasks.AddRange(Children.Select(c => EnumerateBuilds(pipe, c, dir, default(BuildTemplate), cancelToken)));
						}

						await Task.WhenAll(allTasks);
					}
					finally
					{
						pipe.Complete();
					}
				});

				return pipe;
			}
		}

		public List<Proxy> Proxies { get; set; } = new List<Proxy>();
		public List<Project> Projects { get; set; }

		public string UnsyncPath { get; set; }
		public string DFS { get; set; }

		public Config(string filename)
		{
			var rootNode = XDocument.Load(filename).Root;

			UnsyncPath = rootNode.Attribute("path")?.Value;

			if (UnsyncPath != null && !File.Exists(UnsyncPath))
			{
				throw new Exception("Unable to find unsync.exe binary specified in config file.");
			}

			DFS = rootNode.Attribute("dfs")?.Value;

			Proxies.Add(new Proxy()
			{
				Name = "(none)",
				Path = null
			});
			Proxies.AddRange(rootNode.Element("proxies").Elements("proxy").Select(p => new Proxy()
			{
				Name = p.Attribute("name")?.Value,
				Path = p.Attribute("path")?.Value
			}));

			Projects = rootNode.Element("projects").Elements("project").Select(p => new Project()
			{
				Name = p.Attribute("name")?.Value,
				Root = p.Attribute("root")?.Value,
				Destination = p.Attribute("dest")?.Value,
				Children = p.Elements("dir").Select(d => new Directory(d)).ToList(),
				Exclusions = p.Elements("exclude").Select(d => d.Value).ToList()
			}).ToList();
		}
	}
}
