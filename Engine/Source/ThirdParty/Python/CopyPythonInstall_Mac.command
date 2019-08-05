#!/bin/sh

python_src_dest_dir="`dirname \"$0\"`/Mac"
python_bin_dest_dir="`dirname \"$0\"`/../../../Binaries/ThirdParty/Python/Mac"
python_src_dir="/Library/Frameworks/Python.framework/Versions/2.7"
python_bin_lib_dest_dir="$python_bin_dest_dir"/lib

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
	cp -R "$python_src_dir"/include/python2.7/* "$python_src_dest_dir"/include
	cp -R "$python_src_dir"/bin/* "$python_bin_dest_dir"/bin
	cp -R "$python_src_dir"/lib/* "$python_bin_lib_dest_dir"
	cp -R "$python_src_dir"/Python "$python_bin_dest_dir"/libpython2.7.dylib
	chmod 755 "$python_bin_dest_dir"/libpython2.7.dylib
	install_name_tool -id @rpath/libpython2.7.dylib "$python_bin_dest_dir"/libpython2.7.dylib
	cp -R "$python_src_dest_dir"/../NoRedist/TPS/PythonMacBin.tps "$python_bin_dest_dir"/

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
				echo "  Removing symlink: $trimmed_file -> $trimmed_resolved_file"
				rm -f "$file"
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
				echo "  Processing symlink: $trimmed_file -> $trimmed_resolved_file"
				rm -f "$file"
				cp "$resolved_file" "$file"
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
		# this was needed when using latest python2.7, their latest hashlib
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
		install_name_tool -change "$openssl_lib_dir/libssl.1.0.0.dylib" "@executable_path/../libssl.1.0.0.dylib" "$python_bin_dest_dir"/lib/python2.7/lib-dynload/_hashlib.so
		install_name_tool -change "$openssl_lib_dir/libcrypto.1.0.0.dylib" "@executable_path/../libcrypto.1.0.0.dylib" "$python_bin_dest_dir"/lib/python2.7/lib-dynload/_hashlib.so
	}
	# disabling this - again, for future reference
	#copy_openssl_libs

else
	echo "Python Source Directory Missing: $python_src_dir"
fi
