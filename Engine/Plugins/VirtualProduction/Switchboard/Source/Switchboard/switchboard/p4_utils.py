# Copyright Epic Games, Inc. All Rights Reserved.
import subprocess
import socket
import re
import os
import marshal
from .switchboard_logging import LOGGER
from functools import wraps
from .config import CONFIG


def p4_login(f):
    @wraps(f)
    def wrapped(*args, **kwargs):
        try:
            return f(*args, **kwargs)
        except Exception as e:
            LOGGER.error(f'{e}')
            LOGGER.error('Error running P4 command. Please make sure you are logged into Perforce and environment variables are set')
            return None

    return wrapped


@p4_login
def p4_stream_root(client):
    """ Returns stream root of client. """
    p4_command = f'p4 -ztag -F "%Stream%" -c {client} stream -o'
    LOGGER.info(f"Executing: {p4_command}")
    p4_result = subprocess.check_output(p4_command).decode()
    if p4_result:
        return p4_result.strip()
    return None


@p4_login
def p4_where(client, local_path):
    """Returns depot path of local file."""
    p4_command = f'p4 -ztag -c {client} -F "%depotFile%" where {local_path}'
    LOGGER.info(f"Executing: {p4_command}")
    p4_result = subprocess.check_output(p4_command).decode()
    if p4_result:
        return p4_result.strip()
    return None


@p4_login
def p4_latest_changelist(p4_path, num_changelists=10):
    """
    Return (num_changelists) latest CLs
    """
    p4_command = f'p4 -ztag -F "%change%" changes -m {num_changelists} {p4_path}/...'
    LOGGER.info(f"Executing: {p4_command}")
    p4_result = subprocess.check_output(p4_command).decode()

    if p4_result:
        return p4_result.split()

    return None


@p4_login
def p4_current_user_name():
    p4_command = f'p4 set P4USER'
    p4_result = subprocess.check_output(p4_command).decode().rstrip()

    p = re.compile("P4USER=(.*)\\(set\\)")
    matches = p.search(p4_result)
    if matches:
        return matches.group(1).rstrip()
    return None


@p4_login
def p4_edit(file_path):
    p4_command = f'p4 edit "{file_path}"'
    p4_result = subprocess.check_output(p4_command).decode()
    LOGGER.debug(p4_result)
