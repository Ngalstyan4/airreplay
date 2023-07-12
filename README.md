# AirReplay - Application Integrated Record-Replay

AirReplay provides a simple API to record/replay application state.
To use AirReplay in a new application, first obtain and build AirReplay with:
```
git clone https://github.com/Ngalstyan4/airreplay
mkdir build && cd build
cmake ..
make install
```
This will compile the library place it at `./build/install`. You should see something like:
```
install/
├── include
│   └── airreplay
│       ├── airreplay.h
│       ├── airreplay.pb.h
│       └── ...
└── lib
    └── libairreplay.a
```
You can then used the API exposed via the airreplay headers to instrument your application and link your application to libairreplay.a

## AirReplay's Protobuf dependency
AirReplay depends on `protobuf` and compiles with whatever versioned `protobuf` library is available on the system.
This works well if your application depends on system's `protobuf` as well. Otherwise, you need to make sure that AirReplay and your project are built with the same version of `protobuf`.

Kudu and Kuduraft do not depend on the system's `protobuf`. When using AirReplay to record-replay kudu, run `cmake` with
```
cmake -DKUDU_HOME=[PATH_TO_KUDU_PROJECT_ROOT_DIR] ...
```
 and AirReplay will use the `protobuf` library compiled for kudu.
