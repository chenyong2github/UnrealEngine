current_version=$(yq e '.version' ./version.yaml)
cat ./changelog.md | grep -q $current_version && ( echo 'Version found' && exit 0 ) || (echo "No changelog section found for release $current_version" >&2 && exit 1)
       