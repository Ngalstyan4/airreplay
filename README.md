# AirReplay - Application Integrated Record-Replay


AirReplay depends on `protobuf` and compiles with whatever versioned `protobuf` library is available on the system.
This works well if your application depends on system's `protobuf` as well. Otherwise, you need to make sure that AirReplay and your project are built with the same version of `protobuf`.

Kudu and Kuduraft do not depend on the system's `protobuf`. When using AirReplay to record-replay kudu, run `cmake` with
```
cmake -DKUDU_HOME=[PATH_TO_KUDU_PROJECT_ROOT_DIR] ...
```
 and AirReplay will use the `protobuf` library compiled for kudu.
