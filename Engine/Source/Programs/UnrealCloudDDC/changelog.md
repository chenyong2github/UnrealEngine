# Unreleased
* Improved handling of cassandra timeouts during ref GC.
* Added deletion of invalid references during blob GC - this reduces the size for entries in the blob_index table.
* Using OpenTelemetry for tracing - still supports datadog traces but this can now be forwarded to any OpenTelemetry compatible service.

# 0.1.0
* First public release of UnrealCloudDDC

# Older releases
No detailed changelog provided for older releases.