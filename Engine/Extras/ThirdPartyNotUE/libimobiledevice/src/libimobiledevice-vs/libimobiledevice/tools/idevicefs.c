/*
 * idevicefs.c
 * Simple command line utility to interact with the filesystem without using ifuse.
 *
 * Copyright (c) 2014 Martin Szulecki. All Rights Reserved.
 * Copyright (c) 2014 Nikias Bassen. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define TOOL_NAME "idevicefs"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <inttypes.h>
#ifdef _MSC_VER
#include <direct.h>
#else
#include <unistd.h>
#endif
#ifndef WIN32
#include <signal.h>
#endif
#include "common/utils.h"

#define COPY_BUFFER_SIZE 8192
#define MAX_REMOTE_PATH 256
#define AFC_SERVICE_NAME "com.apple.afc"
#define AFC2_SERVICE_NAME "com.apple.afc2"
#define HOUSE_ARREST_SERVICE_NAME "com.apple.mobile.house_arrest"

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/service.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/house_arrest.h>

#ifdef WIN32
#include <windows.h>
#endif

static void print_commands()
{
	printf("COMMANDS:\n");
	printf("  ls <pathname>\t\t\tList files on the device in path specified.\n");
	printf("  mkdir [-p] <pathname>\t\tCreate the directory in the path specified.\n"
		"  \t\t\t\t-p : create parent directories if they do not exist\n");
	printf("  rm <pathname>\t\t\tRemove the path specified.\n");
	printf("  push [-p] <local> <remote>\tPush the local file or directory to the remote pathname. \n"
		"  \t\t\t\t-p : create parent directories if they do not exist\n");
	printf("  pull <remote> [local]\t\tPull the remote file or directory and save it in the specified local\n"
		"  \t\t\t\tpathname, or in current directory if not specified.\n");
}


static void print_usage()
{
	printf("Usage: %s [OPTIONS] [COMMAND]\n", TOOL_NAME);
	printf("\n");
	printf("Interact with filesystem on device.\n");
	printf("\n");
	printf("OPTIONS:\n");
	printf("  -u, --udid UDID\t\ttarget specific device by UDID\n");
	printf("  -b, --bundle app_id\t\tthe App's bundle name to operate on\n");
	printf("  -c, --container\t\toperate on files in the app's private container (default)\n");
	printf("  -f, --files\t\t\toperate on files accessible in the Files app\n");
	printf("  -n, --network\t\t\tconnect to network device\n");
	printf("  -d, --debug\t\t\tenable communication debugging\n");
	printf("  -h, --help\t\t\tprints usage information\n");
	printf("  -v, --version\t\t\tprints version information\n");
	printf("  -x, --command-file filename\tprocess commands from file instead of the command line, use - for stdin\n");
	printf("\n");
	print_commands();
}

static int command_ls(afc_client_t afc, char* path)
{
	char **dirs = NULL;

	afc_error_t result = afc_read_directory(afc, path, &dirs);
	if (result != AFC_E_SUCCESS || !dirs)
	{
		fprintf(stderr, "ls: Could not list the remote path %s (%d).\n", path, result);
		return -result;
	}

	for (int i = 0; dirs[i]; i++) {
		printf("%s\n", dirs[i]);
	}

	afc_dictionary_free(dirs);

	return 0;
}

static int command_rm(afc_client_t afc, char* path)
{
	afc_error_t result = afc_remove_path(afc, path);
	if (result != AFC_E_SUCCESS) {
		fprintf(stderr, "rm: Failed to remove remote path %s (%d).\n", path, result);
		return -result;
	}

	return 0;
}

static int command_mkdir(afc_client_t afc, char* remote, int makeparents)
{
	afc_error_t result = afc_make_directory(afc, remote);

	if (makeparents && result != AFC_E_SUCCESS) {
		char parent[1024];
		for (char* p = remote; p; p = strchr(p, '/')) {
			strncpy(parent, remote, p - remote);
			parent[p - remote] = 0;
			p++;
			if (*parent)
			{
				afc_make_directory(afc, parent);
			}
		}
		result = afc_make_directory(afc, remote);
	}

	if (result != AFC_E_SUCCESS) {
		fprintf(stderr, "mkdir: Failed to make directory %s (%d).\n", remote, result);
		return -result;
	}

	return 0;
}

static int command_pull_file(afc_client_t afc, char* remote, char* local)
{
	uint64_t handle = 0;
	char buffer[COPY_BUFFER_SIZE];

	afc_error_t  result = afc_file_open(afc, remote, AFC_FOPEN_RDONLY, &handle);
	if (result != AFC_E_SUCCESS) {
		fprintf(stderr, "pull: Failed to open remote file %s (%d).\n", remote, result);
		return -result;
	}

	FILE* fp = fopen(local, "wb");
	if (!fp) {
		fprintf(stderr, "pull: Failed to open local file %s for writing.\n", local);
		afc_file_close(afc, handle);
		return -1;
	}

	uint64_t file_size = 0;
	uint64_t total_bytes_written = 0;

	afc_file_seek(afc, handle, 0, SEEK_END);
	afc_file_tell(afc, handle, &file_size);
	afc_file_seek(afc, handle, 0, SEEK_SET);

	printf("Pulling %s to %s (%"PRId64" bytes)\n", remote, local, file_size);

	uint32_t  bytes_read = 0;
	do
	{
		result = afc_file_read(afc, handle, buffer, COPY_BUFFER_SIZE, &bytes_read);
		if (result != AFC_E_SUCCESS)
		{
			fprintf(stderr, "pull: Error reading from remote file %s (%d).\n", remote, result);
			fclose(fp);
			afc_file_close(afc, handle);
			return -1;
		}

		if (bytes_read > 0) {
			if (fwrite(buffer, 1, bytes_read, fp) != bytes_read) {
				fprintf(stderr, "pull: Error writing to local file %s.\n", local);
				fclose(fp);
				afc_file_close(afc, handle);
				return -1;
			}
			total_bytes_written += bytes_read;
		}
	} while(bytes_read == COPY_BUFFER_SIZE);

	fclose(fp);
	afc_file_close(afc, handle);

	if (total_bytes_written != file_size)
	{
		fprintf(stderr, "pull: File size mismatch downloading %s (only %"PRId64" of %"PRId64" downloaded).\n", remote, total_bytes_written, file_size);
		return -1;
	}

	return 0;
}

static int afc_stat(afc_client_t afc, char* pathname)
{
	int result = 0;
	char** file_information;
	result = afc_get_file_info(afc, pathname, &file_information);
	if (result != AFC_E_SUCCESS) {
		return -result;
	}

	for (int i = 0; file_information[i]; i+=2) {
		if (!strcmp(file_information[i], "st_ifmt")) {
			if (!strcmp(file_information[i+1], "S_IFDIR")) {
				result = S_IFDIR;
			}
			else
			if (!strcmp(file_information[i + 1], "S_IFREG")) {
				result = S_IFREG;
			}

			break;
		}
	}

	afc_dictionary_free(file_information);

	return result;
}

static int command_pull(afc_client_t afc, char* remote, char* local);

static int command_pull_dir(afc_client_t afc, char* remote, char* local)
{
#ifdef WIN32
	mkdir(local);
#else
	mkdir(local, 0755);
#endif

	afc_error_t result = 0;

	char** dirs = NULL;
	result = afc_read_directory(afc, remote, &dirs);
	if (result != AFC_E_SUCCESS || !dirs) {
		fprintf(stderr, "pull: Could not list the remote path %s (%d).\n", remote, result);
		return -result;
	}

	for (int i = 0; dirs[i]; i++) {
		if (strcmp(dirs[i], ".") && strcmp(dirs[i], "..")) {
			char newremote[MAX_REMOTE_PATH], newlocal[MAX_REMOTE_PATH];
			strcpy(newremote, remote);
			strcat(newremote, "/");
			strcat(newremote, dirs[i]);
			strcpy(newlocal, local);
			strcat(newlocal, "/");
			strcat(newlocal, dirs[i]);

			result = command_pull(afc, newremote, newlocal);

			if(result < 0)
			{
				break;
			}
		}
	}

	afc_dictionary_free(dirs);
	return result;
}

static int command_pull(afc_client_t afc, char* remote, char* local)
{
	if (local == NULL) {
		// strip off path
		local = remote;
		for (char* p = local; p; p = strchr(p, '/'))
			local = ++p;
	}

	int statret = afc_stat(afc, remote);
	switch (statret) {
	case S_IFDIR:
		return command_pull_dir(afc, remote, local);
		break;
	case S_IFREG:
		return command_pull_file(afc, remote, local);
		break;
	default:
		fprintf(stderr, "pull: Failed to get file info for %s (%d).\n", remote, statret);
		return statret;
	}
}

static int command_push(afc_client_t afc, char* local, char* remote, int makedirs);

static int command_push_dir(afc_client_t afc, char* local, char* remote)
{
	int result = 0;
	DIR* dir = opendir(local);
	if (dir == NULL) {
		fprintf(stderr, "pull: Failed to read directory %s.\n", local);
		return -1;
	}

	for(;;)
	{
		struct dirent* dirent = readdir(dir);
		if (dirent == NULL)	{
			break;
		}

		if (strcmp(dirent->d_name, ".") && strcmp(dirent->d_name, "..")) {
			char newremote[MAX_REMOTE_PATH], newlocal[MAX_REMOTE_PATH];
			strcpy(newremote, remote);
			strcat(newremote, "/");
			strcat(newremote, dirent->d_name);
			strcpy(newlocal, local);
			strcat(newlocal, "/");
			strcat(newlocal, dirent->d_name);

			result = command_push(afc, newlocal, newremote, 1);

			if (result < 0)	{
				break;
			}
		}
	}
	closedir(dir);
	return 0;
}

static int command_push_file(afc_client_t afc, char* local, char* remote, int makedirs)
{
	uint64_t handle = 0;
	char buffer[COPY_BUFFER_SIZE];

	FILE* fp = fopen(local, "rb");
	if (!fp) {
		fprintf(stderr, "pull: Failed open to local file %s for reading.\n", local);
		return -1;
	}

	afc_error_t  result = afc_file_open(afc, remote, AFC_FOPEN_WRONLY, &handle);
	if (makedirs && result != AFC_E_SUCCESS) {
		char path[1024];
		for (char* p = remote; p; p = strchr(p, '/')) { 
			strncpy(path, remote, p-remote);
			path[p - remote]=0;
			p++;
			if (*path) {
				afc_make_directory(afc, path);
			}
		}
		result = afc_file_open(afc, remote, AFC_FOPEN_WRONLY, &handle);
	}

	if (result != AFC_E_SUCCESS) {
		fprintf(stderr, "push: Failed to open remote file %s for writing (%d).\n", remote, result);
		fclose(fp);
		return -result;
	}

	uint32_t file_size = 0;
	uint32_t total_bytes_written = 0;
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	printf("Pushing %s to %s (%u bytes)\n", local, remote, file_size);

	size_t  bytes_read = 0;
	do
	{
		bytes_read = fread(buffer, 1, COPY_BUFFER_SIZE, fp);

		if (bytes_read > 0) {

			uint32_t bytes_written = 0;
			result = afc_file_write(afc, handle, buffer, (uint32_t)bytes_read, &bytes_written);
			if (result != AFC_E_SUCCESS || bytes_written != bytes_read)
			{
				fprintf(stderr, "push: Error writing to remote file %s (%d).\n", remote, result);
				fclose(fp);
				afc_file_close(afc, handle);
				return -1;
			}

			total_bytes_written += bytes_written;
		}
	} while (bytes_read == COPY_BUFFER_SIZE);

	fclose(fp);
	afc_file_close(afc, handle);

	if (total_bytes_written != file_size)
	{
		fprintf(stderr, "push: File size mismatch downloading %s (only %u of %u downloaded).\n", remote, total_bytes_written, file_size);
		return -1;
	}

	return 0;
}

static int command_push(afc_client_t afc, char* local, char* remote, int makedirs)
{
	struct stat buf;
	int result = stat(local, &buf);
	if (result < 0)
	{
		fprintf(stderr, "push: Failed to get file info for %s (%d).\n", local, result);
		return result;
	}

	if (buf.st_mode & S_IFDIR)
	{
		return command_push_dir(afc, local, remote);
	}
	else
	if (buf.st_mode & S_IFREG)
	{
		return command_push_file(afc, local, remote, makedirs);
	}

	return 0;
}

static int process_command(afc_client_t afc, int argc, char* argv[])
{
	if (argc > 0 && !strcmp(argv[0], "ls")) {
		if (!argv[1] || !*argv[1]) {
			fprintf(stderr, "ls: missing path parameter\n");
			return -1;
		}
		command_ls(afc, argv[1]);
	}
	else if (argc > 0 && !strcmp(argv[0], "rm")) {
		if (!argv[1] || !*argv[1]) {
			fprintf(stderr, "rm: missing path parameter\n");
			return -1;
		}
		command_rm(afc, argv[1]);
	}
	else if (argc > 0 && !strcmp(argv[0], "mkdir")) {
		int arg = 1;
		int makeparents = 0;
		if (argv[1] && !strcmp(argv[1], "-p"))
		{
			makeparents = 1;
			arg++;
		}

		if (!argv[arg] || !*argv[arg]) {
			fprintf(stderr, "mkdir: missing path parameter\n");
			return -1;
		}
		command_mkdir(afc, argv[arg], makeparents);
	}
	else if (argc > 0 && !strcmp(argv[0], "pull")) {
		if (!argv[1] || !*argv[1]) {
			fprintf(stderr, "pull: missing remote filename parameter\n");
			return -1;
		}
		command_pull(afc, argv[1], (argv[2] && *argv[2]) ? argv[2] : NULL);
	}	
	else if (argc > 0 && !strcmp(argv[0], "push")) {
		int argoffset = 0;
		int makedirs = 0;
		if (argv[1] && !strcmp(argv[1], "-p"))
		{
			makedirs = 1;
			argoffset++;
		}

		if (!argv[argoffset + 1] || !*argv[argoffset + 1]) {
			fprintf(stderr, "push: missing local filename parameter\n");
			return -1;
		}
		if (!argv[argoffset + 2] || !*argv[argoffset + 2]) {
			fprintf(stderr, "push: missing remote filename parameter\n");
			return -1;
		}
		command_push(afc, argv[argoffset + 1], argv[argoffset + 2], makedirs);
	}
	else {
		print_commands();
		return -1;
	}
	return 0;
}


int main(int argc, char* argv[])
{
	idevice_t device = NULL;
	lockdownd_client_t lockdownd = NULL;
	afc_client_t afc = NULL;

	idevice_error_t device_error = IDEVICE_E_SUCCESS;
	lockdownd_error_t lockdownd_error = LOCKDOWN_E_SUCCESS;
	house_arrest_client_t house_arrest = NULL;

	lockdownd_service_descriptor_t service = NULL;

	int arg;
	const char* udid = NULL;
	int use_network = 0;
	int use_container = 1;
	char* app_id = NULL;
	char* command_file = NULL;

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	/* parse cmdline args */
	if (argc == 1)
	{
		print_usage();
		return EXIT_SUCCESS;
	}

	for (arg = 1; arg < argc; arg++) {
		if (!strcmp(argv[arg], "-d") || !strcmp(argv[arg], "--debug")) {
			idevice_set_debug_level(1);
			continue;
		}
		else if (!strcmp(argv[arg], "-c") || !strcmp(argv[arg], "--container")) {
			use_container = 1;
			continue;
		}
		else if (!strcmp(argv[arg], "-f") || !strcmp(argv[arg], "--files")) {
			use_container = 0;
			continue;
		}
		else if (!strcmp(argv[arg], "-u") || !strcmp(argv[arg], "--udid")) {
			arg++;
			if (!argv[arg] || !*argv[arg]) {
				print_usage();
				return EXIT_FAILURE;
			}
			udid = argv[arg];
			continue;
		}
		else if (!strcmp(argv[arg], "-n") || !strcmp(argv[arg], "--network")) {
			use_network = 1;
			continue;
		}
		else if (!strcmp(argv[arg], "-h") || !strcmp(argv[arg], "--help")) {
			print_usage();
			return EXIT_SUCCESS;
		}
		else if (!strcmp(argv[arg], "-v") || !strcmp(argv[arg], "--version")) {
			printf("%s %s\n", TOOL_NAME, PACKAGE_VERSION);
			return EXIT_SUCCESS;
		}
		else if (!strcmp(argv[arg], "-x") || !strcmp(argv[arg], "--command-file")) {
			arg++;
			if (!argv[arg] || !*argv[arg]) {
				print_usage();
				return EXIT_FAILURE;
			}
			command_file = argv[arg];
			continue;
		}
		else if (!strcmp(argv[arg], "-b") || !strcmp(argv[arg], "--bundle")) {
			arg++;
			if (!argv[arg] || !*argv[arg] || *argv[arg] == '-') {
				fprintf(stderr, "%s must specify a bundle name\n", argv[arg-1]);
				return EXIT_FAILURE;
			}
			app_id = argv[arg];
			continue;
		}
		else if (*argv[arg] != '-' && command_file == NULL) {		
			// last switch, remaining arguments are the command
			break;
		}
		else {
			print_usage();
			return EXIT_SUCCESS;
		}
	}


	device_error = idevice_new_with_options(&device, udid, (use_network) ? IDEVICE_LOOKUP_NETWORK : IDEVICE_LOOKUP_USBMUX);
	if (device_error != IDEVICE_E_SUCCESS) {
		if (udid) {
			printf("No device found with udid %s.\n", udid);
		} else {
			printf("No device found.\n");
		}
		return EXIT_FAILURE;
	}

	lockdownd_error = lockdownd_client_new_with_handshake(device, &lockdownd, TOOL_NAME);
	if (lockdownd_error != LOCKDOWN_E_SUCCESS) {
		idevice_free(device);
		if (lockdownd_error == LOCKDOWN_E_PASSWORD_PROTECTED) {
			fprintf(stderr, "Please disable the password protection on your device and try again.\n");
			fprintf(stderr, "The device does not allow pairing as long as a password has been set.\n");
			fprintf(stderr, "You can enable it again after the connection succeeded.\n");
#ifdef LOCKDOWN_E_PAIRING_DIALOG_PENDING
		}
		else if (lockdownd_error == LOCKDOWN_E_PAIRING_DIALOG_PENDING) {
			fprintf(stderr, "Please dismiss the trust dialog on your device and try again.\n");
			fprintf(stderr, "The device does not allow pairing as long as the dialog has not been accepted.\n");
#endif
		}
		else {
			fprintf(stderr, "Failed to connect to lockdownd service on the device.\n");
			fprintf(stderr, "Try again. If it still fails try rebooting your device.\n");
		}
		return EXIT_FAILURE;
	}

	if ((lockdownd_start_service(lockdownd, HOUSE_ARREST_SERVICE_NAME, &service) != LOCKDOWN_E_SUCCESS) || !service) {
		lockdownd_client_free(lockdownd);
		idevice_free(device);
		fprintf(stderr, "Failed to start AFC service '%s' on the device.\n", HOUSE_ARREST_SERVICE_NAME);
		return EXIT_FAILURE;
	}

	house_arrest_client_new(device, service, &house_arrest);
	if (!house_arrest) {
		fprintf(stderr, "Could not start document sharing service!\n");
		return EXIT_FAILURE;
	}

	if (!app_id)
	{
		fprintf(stderr, "You must specify the App's bundle name with --bundle or -b\n");
		house_arrest_client_free(house_arrest);
		return EXIT_FAILURE;
	}

	if (house_arrest_send_command(house_arrest, use_container ? "VendContainer" : "VendDocuments", app_id) != HOUSE_ARREST_E_SUCCESS) {
		fprintf(stderr, "Could not send document sharing service command for App '%s'!\n", app_id);
		house_arrest_client_free(house_arrest);
		return EXIT_FAILURE;
	}

	plist_t dict = NULL;
	if (house_arrest_get_result(house_arrest, &dict) != HOUSE_ARREST_E_SUCCESS) {
		fprintf(stderr, "Could not get result from document sharing service for App '%s'!\n", app_id);
		house_arrest_client_free(house_arrest);
		return EXIT_FAILURE;
	}
	plist_t node = plist_dict_get_item(dict, "Error");
	if (node) {
		char* str = NULL;
		plist_get_string_val(node, &str);
		fprintf(stderr, "ERROR: %s\n", str);
		if (str && !strcmp(str, "InstallationLookupFailed")) {
			if (use_container)
				fprintf(stderr, "The App '%s' is not present on the device.\n", app_id);
			else
				fprintf(stderr, "The App '%s' is either not present on the device, or the 'UIFileSharingEnabled' key is not set in its Info.plist. Starting with iOS 8.3 this key is mandatory to allow access to an app's Documents folder.\n", app_id);
		}
		free(str);
		plist_free(dict);
		house_arrest_client_free(house_arrest);
		return EXIT_FAILURE;
	}
	plist_free(dict);

	afc_client_new_from_house_arrest_client(house_arrest, &afc);
	lockdownd_client_free(lockdownd);
	lockdownd = NULL;

	int return_code = EXIT_SUCCESS;

	if (command_file == NULL)
	{
		return_code = process_command(afc, argc - arg, &argv[arg]) < 0 ? EXIT_FAILURE : 0;
	}
	else
	{
		FILE* stream = stdin;
		if (strcmp(command_file, "-"))
			stream = fopen(command_file, "r");
		if (stream == NULL)
		{
			fprintf(stderr, "Could not open command file '%s'\n", command_file);
		}
		else
		{
			#define COMMANDFILE_MAX_LINE 1024
			#define COMMANDFILE_MAX_ARGS 20
			char line[COMMANDFILE_MAX_LINE];

			if (stream == stdin) {
				printf("%% ");
				fflush(stream);
			}

			while (fgets(line, COMMANDFILE_MAX_LINE, stream))
			{
				char* eol = strchr(line, '\n');
				if (eol)
					*eol = '\0';
				if (stream != stdin) {
					printf("%% %s\n", line);
				}

				char* command_argv[COMMANDFILE_MAX_ARGS+1];
				int command_argc = 0;

				char* p = line;
				do {
					if (*p == '\"' || *p == '\'') {
						char* endquote = strchr(p+1, *p);
						if (endquote == NULL || (*(endquote + 1) != ' ' && *(endquote + 1) != '\0')) {
							fprintf(stderr, "Mismatched quotes\n");
							return_code = EXIT_FAILURE;
							goto nextline;
						}
						*endquote = '\0';				
						command_argv[command_argc++] = p+1;
						p = endquote + 1;
						if (*p == ' ')
							p++;
					}
					else {
						char* endarg = strchr(p + 1, ' ');
						if (endarg)
						{
							*endarg = '\0';
							command_argv[command_argc++] = p;
							p = endarg + 1;
						}
						else
						{
							command_argv[command_argc++] = p;
							break;
						}
					}
				} while (*p != '\0' && command_argc < COMMANDFILE_MAX_ARGS);
				command_argv[command_argc] = 0;

				if (command_argc == 1 && !strcmp(command_argv[0], "exit"))
				{
					break;
				}

				if (process_command(afc, command_argc, command_argv) < 0)
				{
					return_code = EXIT_FAILURE;
				}
nextline:;
				if (stream == stdin) {
					printf("%% ");
					fflush(stream);
				}
			}

			if (stream != stdin)
				fclose(stream);
		}
	}

	printf("\nDone.\n");

	afc_client_free(afc);
	house_arrest_client_free(house_arrest);
	idevice_free(device);

	return return_code;
}

