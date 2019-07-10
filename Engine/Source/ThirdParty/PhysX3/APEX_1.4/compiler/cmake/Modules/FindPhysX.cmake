#
# Find PhysX
#
#  This is a really dumb finder - it finds the PhysX install we're going to use add_subdirectory on.
#  PHYSX_FOUND - System has PhysX
#  PHYSX_ROOT_DIR - The PhysX root directory

# First look for the feature branch, then for the trunk

FIND_PATH(		PHYSX_ROOT_DIR 
                NAMES "Include/PxActor.h"
				PATHS "${GW_DEPS_ROOT}/PhysX_3.4")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PhysX DEFAULT_MSG PHYSX_ROOT_DIR)

mark_as_advanced(PHYSX_ROOT_DIR)
