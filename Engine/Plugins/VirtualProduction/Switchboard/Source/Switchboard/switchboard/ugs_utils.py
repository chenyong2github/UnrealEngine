# Copyright Epic Games, Inc. All Rights Reserved.

import logging
import subprocess
import sys
import shutil
import re
from os import path
from pathlib import Path
from typing import Optional, List, Dict, Callable


def find_bin(
    dir_to_look_under: Optional[Path] = None
) -> Optional[Path]:
    ugs_dir = dir_to_look_under

    if not ugs_dir:
        env_ugs_path = shutil.which('ugs')
        if env_ugs_path:
            ugs_dir = Path(env_ugs_path).parent
        elif sys.platform.startswith('win'):
            ugs_dir = path.join('${LOCALAPPDATA}', 'UnrealGameSync', 'Latest')
        #elif: for other platforms we don't have a default install location (the dll is usually installed from perforce, which could go anywhere)

    if not ugs_dir:
        logging.error('Could not determine where to find the UnrealGameSync library (ugs.dll). Ensure that UGS is installed on the target device.'
                      'If it already is installed, then either make sure the `ugs` command is globally available, or specify the directory explicitly.')
        return None

    ugs_bin_path = path.join(path.expandvars(ugs_dir), 'ugs.dll')
    if not path.exists(ugs_bin_path):
        logging.error(f"Failed to find '{ugs_bin_path}'. Ensure that UGS is installed on the target device."
                       'If it already is installed, then either make sure the `ugs` command is globally available, or specify the directory explicitly.')
        return None

    return ugs_bin_path

def setup_dependencies(
    ue_engine_dir:Path
) -> int:
    logging.debug('Verifying UnrealGameSync dependencies.')
    
    if sys.platform.startswith('win'):
        script_name = 'GetDotnetPath.bat'
        dotnet_setup_script = path.join(ue_engine_dir, 'Build', 'BatchFiles', script_name)
        dotnet_setup_args = [dotnet_setup_script]
    else:
        platform_dirname = 'Mac' if sys.platform.startswith('darwin') else 'Linux'
        platform_scripts_dir = path.join(ue_engine_dir, 'Build', 'BatchFiles', f'{platform_dirname}')

        script_name = 'SetupEnvironment.sh'
        dotnet_setup_script = path.join(platform_scripts_dir, script_name)
        dotnet_setup_args = [dotnet_setup_script, '-dotnet', f'{platform_scripts_dir}']

    try:
        with subprocess.Popen(dotnet_setup_args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE) as proc:
            for line in proc.stdout:
                logging.debug(f'{script_name}> {line.decode().rstrip()}')
    except Exception as exc:
        logging.error(f'setup_ugs_dependencies(): exception running Popen: {dotnet_setup_args}', exc_info=exc)

    if proc.returncode != 0:
        logging.error('Unable to find a install of Dotnet SDK. Please make sure you have it installed and that `dotnet` is a globally available command.')
    
    return proc.returncode

def _find_engine_dir(
    uproj_path: Path
) -> Optional[Path]:
    iter_dir = uproj_path.parent
    while not iter_dir.samefile(iter_dir.anchor):
        candidate = iter_dir / 'Engine'
        if candidate.is_dir():
            return candidate
        iter_dir = iter_dir.parent

    # TODO: Consider looking for Engine dir as ancestor of this script?
    return None

def _get_active_ugs_context(
    ugs_dll_path: Path,
    cwd: Path = None
) -> Optional[Dict[str, str]]:
    # Assumes UGS dependencies have been already setup (this assumption prevents us from redundently running `setup_dependencies()`)

    ugs_state = {}
    try:
        status_args = ['dotnet', ugs_dll_path, 'status']
        with subprocess.Popen(status_args, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE) as status_proc:
            for staus_line in status_proc.stdout:
                staus_line_str =  staus_line.decode().rstrip()
                logging.debug(f'ugs> {staus_line_str}')
                if 'Project:' in staus_line_str:
                        match = re.match("Project:\s*//(\S+?)/(\S+)", staus_line_str)
                        if match:
                            ugs_state['client'] = match.group(1)
                            ugs_state['project'] = match.group(2)
                if 'User:' in staus_line_str:
                        match = re.match("User:\s*(\S+)", staus_line_str)
                        if match:
                            ugs_state['user'] = match.group(1)
    except Exception as exc:
        logging.error(f'_get_ugs_state(): exception running Popen: {status_args}', exc_info=exc)
        return None

    if not ugs_state:
        return None
    return ugs_state

def _set_active_ugs_context(
    ugs_dll_path: Path,
    ugs_settings: Dict[str, str],
    cwd: Path = None
) -> int:
    # Assumes UGS dependencies have been already setup (this assumption prevents us from redundently running `setup_dependencies()`)

    try:
        init_args = ['dotnet', ugs_dll_path, 'init']
        for key,val in ugs_settings.items():
            init_args.append(f'-{key}={val}')

        with subprocess.Popen(init_args, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE) as init_proc:
            for init_line in init_proc.stdout:
                logging.info(f'ugs> {init_line.decode().rstrip()}')
    except Exception as exc:
        logging.error(f'_set_active_ugs_context(): exception running Popen: {init_args}', exc_info=exc)
        return -1

    return 0

def run(
    ugs_args: List[str],
    uproj_path: Path,
    ugs_bin_dir: Optional[Path] = None,
    user: Optional[str] = None,
    client: Optional[str] = None,
    output_handling_fn: Optional[Callable[[str], int]] = None,
) -> int:

    # Even though we specify this should be a list, handle the simplified case of users passing a single word command
    if type(ugs_args) == str:
        ugs_args = ugs_args.split()
    args_str = ' '.join(ugs_args)

    ugs_bin_path = find_bin(ugs_bin_dir)
    if not ugs_bin_path:
        logging.error(f"Failed to find 'ugs.dll'. Aborting UnrealGameSync command: `ugs {args_str}`")
        return -1

    ue_engine_dir = _find_engine_dir(uproj_path)
    if not ue_engine_dir:
        logging.warning("Failed to locate the Unreal '/Engine/' directory. As a result we are unable to validate UGS dependencies. Ignoring and continuing on as if they were setup.")
    # Make sure the needed dependencies (dotnet, etc.) are installed upfront
    elif setup_dependencies(ue_engine_dir) != 0:
        logging.warning('Failed to validate UnrealGameSync dependencies. Ignoring and continuing on as if they were setup.')

    cwd = uproj_path.parent

    logging.info("Capturing UnrealGameSync's current state.")
    ugs_state_to_restore = _get_active_ugs_context(ugs_bin_path, cwd=cwd)
    if not ugs_state_to_restore:
        logging.warning('Failed to capture the current state of UnrealGameSync. Will not be able to restore it after completing our operation.')

    logging.info('Setting UnrealGameSync context.')
    # UGS expects a project path relative to the repo's root
    sanitized_proj_path = path.relpath(uproj_path, ue_engine_dir.parent) if ue_engine_dir else uproj_path
    # and only accepts paths with delimited by forward-slashes
    sanitized_proj_path = sanitized_proj_path.replace('\\','/')
    op_context_params = { 'project' : sanitized_proj_path }
    if user:
        op_context_params['user'] = user
    if client:
        op_context_params['client'] = client

    init_result = _set_active_ugs_context(ugs_bin_path, op_context_params, cwd=cwd)
    if init_result != 0:
        logging.error(f"Failed to initialize UnrealGameSync. Aborting UnrealGameSync command: `ugs {args_str}`")
        return init_result

    logging.info(f'Executing UnrealGameSync command: `ugs {args_str}`.')
    ugs_cmd = ['dotnet', ugs_bin_path] + ugs_args

    try:
        with subprocess.Popen(ugs_cmd, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE) as ugs_proc:
            for line in ugs_proc.stdout:
                line_str = f'{line.decode().rstrip()}'

                invalid_arg_match = re.match("Invalid argument:\s*(\S+)", line_str)
                if invalid_arg_match:
                    logging.error(f'ugs> {line_str}')
                    logging.error(f'The current version of UnrealGameSync does not support the `{invalid_arg_match.match.group(1)}` argument. Please make sure UGS is up to date.')
                    ugs_proc.terminate()
                elif output_handling_fn:
                    if output_handling_fn(line_str) != 0:
                        ugs_proc.terminate()
                else:
                    logging.info(f'ugs> {line_str}')
    except Exception as exc:
        logging.error(f'ugs.run(): exception running Popen: {ugs_cmd}', exc_info=exc)
        return -1

    if ugs_state_to_restore:
        logging.info('Restoring previous UnrealGameSync state.')
        if _set_active_ugs_context(ugs_bin_path, ugs_state_to_restore, cwd=cwd) != 0:
            logging.warn("Failed to resotre UnrealGameSync's state prior to operation.")

    return ugs_proc.returncode

def sync(
    uproj_path: Path,
    sync_cl: Optional[int] = None,
    sync_pcbs: Optional[bool] = False,
    ugs_bin_dir: Optional[Path] = None,
    user: Optional[str] = None,
    client: Optional[str] = None
) -> Optional[int]:

    sync_args = ['sync']
    if sync_cl:
        sync_args.append(f'{sync_cl}')
    else:
        sync_args.append('latest')

    if sync_pcbs:
        sync_args.append('-binaries')

    return run(
        sync_args,
        uproj_path,
        ugs_bin_dir=ugs_bin_dir,
        user=user,
        client=client
    )

def latest_chagelists(
    uproj_path: Path,
    ugs_bin_dir: Optional[Path] = None,
    user: Optional[str] = None,
    client: Optional[str] = None
) -> Optional[List[int]]:
    
    changelist_list = []

    def changes_output_handler(output_str:str, changes_out=changelist_list) -> int:
        cl_desc_match = re.match("^\s*(\d+).*", output_str)
        if cl_desc_match:
            changes_out.append(cl_desc_match.group(1))
        return 0

    run_result = run(
        'changes',
        uproj_path,
        ugs_bin_dir =ugs_bin_dir,
        user = user,
        client=client,
        output_handling_fn = changes_output_handler
    )

    if run_result != 0:
        return None

    return changelist_list