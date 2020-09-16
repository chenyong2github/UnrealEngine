#!/bin/sh

python_src_dest_dir="`dirname \"$0\"`/Mac"
python_bin_dest_dir="`dirname \"$0\"`/../../../Binaries/ThirdParty/Python3/Mac"
python_bin_lib_dest_dir="$python_bin_dest_dir"/lib

use_system_python=0
# --------------------------------------------------------------------------------
if [ $use_system_python -eq "1" ]
then
	# the following is the Mac system path
	# however, (and this is normally updated via the installers from Python.org) the binary found here
	# have been found to contain hard coded paths to the... "system path"...
	# meaning, UE4's bundled Mac python executable would get ignored (UE-75005)
python_src_dir="/Library/Frameworks/Python.framework/Versions/3.7"

else
	# the following is the homebrew install path - this fixes the hard coded issue (see above, UE-75005)
	python_src_dir="$HOME/.pyenv/versions/3.7.7"

	# this was installed to the developer's local environment and then copied over to UE4 via this script
	# the local python version is built as follows:
	#
	#		brew update
	#		brew install openssl
	#		brew install pyenv
	#		export PYENV_ROOT="$HOME/.pyenv"
	#		export PATH="$PYENV_ROOT/bin:$PATH"
	#		eval "$(pyenv init -)"
	#		PYTHON_CONFIGURE_OPTS="--enable-shared --without-pymalloc" \
	#			CFLAGS="-I$(brew --prefix openssl)/include" \
	#			LDFLAGS="-L$(brew --prefix openssl)/lib" \
	#			pyenv install 3.7.7
	#
	# much of this was from: https://fman.io/blog/battling-with-macos/
	# then, proceed with this UE4 copy script
	# finally, use "Reconcile Offline Work..." to figure out the proper perforce changelist to submit
fi
# --------------------------------------------------------------------------------

if [ -d "$python_src_dir" ]
then
	if [ -d "$python_src_dest_dir" ]
	then
		echo "Removing Existing Target Directory: $python_src_dest_dir"
		rm -rf "$python_src_dest_dir"
	fi

	if [ -d "$python_bin_dest_dir" ]
	then
		echo "Removing Existing Target Directory: $python_bin_dest_dir"
		rm -rf "$python_bin_dest_dir"
	fi

	echo "Copying Python: $python_src_dir"

	mkdir -p "$python_src_dest_dir"/include
	mkdir -p "$python_bin_dest_dir"/bin
	mkdir -p "$python_bin_lib_dest_dir"
	cp -R "$python_src_dir"/include/python3.7/* "$python_src_dest_dir"/include
	cp -R "$python_src_dir"/bin/* "$python_bin_dest_dir"/bin
	cp -R "$python_src_dir"/lib/* "$python_bin_lib_dest_dir"
	if [ $use_system_python -eq "1" ]
	then
	cp -R "$python_src_dir"/Python "$python_bin_dest_dir"/libpython3.7.dylib
	else
		cp -R "$python_src_dir"/lib/libpython3.7.dylib "$python_bin_dest_dir"
	fi
	chmod 755 "$python_bin_dest_dir"/libpython3.7.dylib
	install_name_tool -id @rpath/libpython3.7.dylib "$python_bin_dest_dir"/libpython3.7.dylib
	if [ -f "$python_src_dest_dir"/../../../../Restricted/NoRedist/Source/ThirdParty/Python3/TPS/PythonMacBin.tps ]
	then
	cp -R "$python_src_dest_dir"/../../../../Restricted/NoRedist/Source/ThirdParty/Python3/TPS/PythonMacBin.tps "$python_bin_dest_dir"/
	fi


	echo "Processing Python symlinks: $python_dest_dir"
	function remove_symlinks()
	{
		for file in $1/*
		do
			if [ -L "$file" ]
			then
				resolved_file="$1/`readlink \"$file\"`"
				trimmed_file=".${file:$2}"
				trimmed_resolved_file=".${resolved_file:$2}"
				if [ -f "$resolved_file" ]
				then
# for debugging
#					echo "  Removing symlink: $file -> $resolved_file"
				echo "  Removing symlink: $trimmed_file -> $trimmed_resolved_file"
				rm -f "$file"
					cp -R "$resolved_file" "$file"
				else
					echo "WARNING NOT FOUND: $resolved_file:"
				fi
			fi

			if [ -d "$file" ]
			then
				remove_symlinks "$file" $2
			fi
		done
	}
	remove_symlinks "$python_bin_lib_dest_dir" ${#python_bin_lib_dest_dir}

	function process_symlinks()
	{
		for file in $1/*
		do
			if [ -L "$file" ]
			then
				resolved_file="$1/`readlink \"$file\"`"
				trimmed_file=".${file:$2}"
				trimmed_resolved_file=".${resolved_file:$2}"
				if [ -f "$resolved_file" ]
				then
# for debugging
#					echo "  Processing symlink: $file -> $resolved_file"
				echo "  Processing symlink: $trimmed_file -> $trimmed_resolved_file"
				rm -f "$file"
				cp "$resolved_file" "$file"
				else
					echo "WARNING NOT FOUND: $resolved_file:"
				fi
			fi

			if [ -d "$file" ]
			then
				process_symlinks "$file" $2
			fi
		done
	}
	process_symlinks "$python_bin_dest_dir" ${#python_bin_dest_dir}

	function remove_obj_files()
	{
		for file in $1/*
		do
			if [ "${file}" != "${file%.pyc}" ] || [ "${file}" != "${file%.pyo}" ]
			then
				trimmed_file=".${file:$2}"
				echo "  Removing: $trimmed_file"
				rm -f "$file"
			fi

			if [ -d "$file" ]
			then
				remove_obj_files "$file" $2
			fi
		done
	}
	remove_obj_files "$python_bin_lib_dest_dir" ${#python_bin_lib_dest_dir}

	function copy_openssl_libs()
	{
		# this was needed when using latest python3.7, their latest hashlib
		# uses some of libssl and libcrypto functions (instead of their own anymore)
		# TODO: see if this can be statically linked next time

		# might need a peek at lib*.x.y.z.dylib for the actual hard coded path
		openssl_lib_dir=/usr/local/opt/openssl/lib

		# saving these instructions here on how to do this for future reference
		cp "$openssl_lib_dir"/libssl.1.0.0.dylib "$python_bin_dest_dir"
		cp "$openssl_lib_dir"/libcrypto.1.0.0.dylib "$python_bin_dest_dir"

		install_name_tool -id "@rpath/libssl.1.0.0.dylib" "$python_bin_dest_dir"/libssl.1.0.0.dylib
		install_name_tool -change "$openssl_lib_dir/libcrypto.1.0.0.dylib" "@executable_path/../libcrypto.1.0.0.dylib" "$python_bin_dest_dir"/libssl.1.0.0.dylib

		install_name_tool -id "@rpath/libcrypto.1.0.0.dylib" "$python_bin_dest_dir"/libcrypto.1.0.0.dylib
		
		# finally:
		install_name_tool -change "$openssl_lib_dir/libssl.1.0.0.dylib" "@executable_path/../libssl.1.0.0.dylib" "$python_bin_dest_dir"/lib/python3.7/lib-dynload/_hashlib.so
		install_name_tool -change "$openssl_lib_dir/libcrypto.1.0.0.dylib" "@executable_path/../libcrypto.1.0.0.dylib" "$python_bin_dest_dir"/lib/python3.7/lib-dynload/_hashlib.so
	}
	# disabling this - again, for future reference
	#copy_openssl_libs

else
	echo "Python Source Directory Missing: $python_src_dir"
fi

if [ ! -f "$python_src_dest_dir"/../../../../Restricted/NoRedist/Source/ThirdParty/Python/TPS/PythonMacBin.tps ]
then
	echo "."
	echo "WARNING: restore (i.e. revert) deleted $python_bin_dest_dir/PythonMacBin.tps before checking in"
	echo "."
fi
