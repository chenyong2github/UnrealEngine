import os
import re
import argparse
from pathlib import Path

#-------------------------------------------------------------------------------
def _spam_header(*args, **kwargs):
	_spam("\x1b[96m##", *args, "\x1b[0m", **kwargs)

def _spam(*args, **kwargs):
	print(*args, **kwargs)

#-------------------------------------------------------------------------------
class _SourceFile(object):
	def __init__(self, path):
		self.path = path
		self.is_include = (path.suffix == ".h")
		self.lines = []
		self.deps = []

	def __eq__(self, rhs):
		return rhs.samefile(self.path)

	def __hash__(self):
		return hash(self.path)

#-------------------------------------------------------------------------------
def _parse_include(line):
	m = re.match(r'\s*#\s*include\s+\"([^"]+)\"', line)
	return None if not m else Path(m.group(1))

def _exclude_line(line):
	line = line.strip()

	if "#pragma once" in line:	return True
	if line.startswith("//"):	return True
	if not line:				return True

#-------------------------------------------------------------------------------
def _main(src_dir, dest_dir):
	dest_dir = dest_dir.resolve()
	os.chdir(Path(__file__).parent)

	_spam_header("Context")
	_spam("Source dir:", os.getcwd())
	_spam("Dest dir:", dest_dir)

	_spam_header("Gathering source files")
	files  = [x for x in src_dir.glob("Public/**/*")]
	files += [x for x in src_dir.glob("Private/**/*")]
	files  = [x for x in files if x.suffix in (".h", ".cpp", ".inl")]
	_spam("Found %d files" % len(files))

	# Collect lines of each source file, filtering local includes
	source_files = []
	for file in files:
		source_file = _SourceFile(file)
		source_files.append(source_file)

		for line in file.open("rt"):
			include = _parse_include(line)
			if not include:
				if not _exclude_line(line):
					source_file.lines.append(line)
				continue

			is_local = False

			for include_dir in (file.parent, src_dir / "Public"):
				candidate = include_dir / include
				if candidate.is_file():
					is_local = True
					break

			if is_local:
				source_file.deps.append(candidate)

	_spam_header("Topological sort")

	# Hook up dependencies
	_spam("Resolving include dependencies")
	for source_file in source_files:
		new_deps = []
		for dep in source_file.deps:
			index = source_files.index(dep)
			dep = source_files[index]
			new_deps.append(dep)
		source_file.deps = new_deps

	# Topologically sort by dependencies
	_spam("Sorting")
	visited = set()
	topo_sorted = []
	def visit(source_file):
		if source_file in visited:
			return

		for x in source_file.deps:
			visit(x)

		topo_sorted.append(source_file)
		visited.add(source_file)

	for x in source_files:
		visit(x)

	# Stable sort to put .cpps last
	ext_key = lambda x: 1 if x.path.suffix == ".cpp" else 0
	source_files = sorted(topo_sorted, key=ext_key)

	# Exclude LZ4
	source_files = [x for x in source_files if "lz4" not in x.path.name]

	# Add prologue and epilogue files
	prologue = _SourceFile(Path("standalone_prologue.h"))
	prologue.lines = [x for x in prologue.path.open("rt") if x]
	source_files.insert(0, prologue)

	epilogue = _SourceFile(Path("standalone_epilogue.h"))
	epilogue.lines = [x for x in epilogue.path.open("rt") if x]
	source_files.append(epilogue)

	# Write the output file
	_spam_header("Output")
	_spam(dest_dir / "trace.h")
	cpp_started = False
	with (dest_dir / "trace.h").open("wt") as out:
		print("// Copyright Epic Games, Inc. All Rights Reserved.", file=out)
		print("#pragma once", file=out)
		for source_file in source_files:
			if not cpp_started:
				if source_file.path.suffix == ".cpp":
					print("#if TRACE_IMPLEMENT", file=out)
					cpp_started = True
			elif source_file.path.suffix != ".cpp":
				print("#endif // TRACE_IMPLEMENT", file=out)
				cpp_started = False

			print("/* {{{1", source_file.path.name, "*/", file=out)
			print(file=out)
			for line in source_file.lines:
				out.write(line)

	_spam("...done!")

def main():
	desc = "Amalgamate TraceLog into a standalone single-file library"
	parser = argparse.ArgumentParser(description=desc)
	parser.add_argument("outdir", help="Directory to write output file(s) to")
	args = parser.parse_args()
	return _main(Path(__file__).parent, Path(args.outdir))

if __name__ == "__main__":
	raise SystemExit(main())
