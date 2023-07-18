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
    └── libairreplay.so
```
You can then used the API exposed via the airreplay headers to instrument your application and link your application to libairreplay.a
## AirReplay API

```cpp
Airreplay(std::string tracename, Mode mode); // mode = RECORD|REPLAY

int RecordReplay(const std::string &connection_info
                const google::protobuf::Message &message, int kind = 0, const std::string &debug_info = "");
bool isReplay(); // true if in REPLAY mode

int SaveRestore(const std::string &key, google::protobuf::Message &message);
int SaveRestore(const std::string &key, std::string &message);
int SaveRestore(const std::string &key, uint64 &message);
...

void RegisterReproducers(std::map<int, ReproducerFunction> reproduers);

```


## AirReplay's Protobuf dependency
AirReplay depends on `protobuf` and compiles with whatever versioned `protobuf` library is available on the system.
This works well if your application depends on system's `protobuf` as well. Otherwise, you need to make sure that AirReplay and your project are built with the same version of `protobuf`.

Kudu and Kuduraft do not depend on the system's `protobuf`. When using AirReplay to record-replay kudu, run `cmake` with
```
cmake -DKUDU_HOME=[PATH_TO_KUDU_PROJECT_ROOT_DIR] ...
```
 and AirReplay will use the `protobuf` library compiled for kudu.


## Integrating AIrReplay into a new application

Example integrations:
 - [kuduraft (leader election only)](https://github.com/facebook/kuduraft/compare/1.8.raft...Ngalstyan4:kuduraft:airreplay?expand=1)
 - [kudu (WIP, replay still fails)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1)

 Integration steps
 1. Build AirReplay as outlined above to obtain the shared library
 1. Modify your application build scripts to link to AirReplay [kudu (CMakeLists.txt)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-20ff7a6c6cd70212e1413303ebd974ee5745be9c02ae55ae34017a7f9a85a6ecR114-R121)
 1. Initialize `airreplay::airr` global object at the start of your application. Note: this has to be __before__ any point in time where you want to record/replay something [kudu (kserver.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-b843607bdc0af2f903cbf75e924ab230d7b4506fb83e23b27853611c8f04553aR148-R181)
 1. Record startup non determinism. E.g. randomly generated uuids [kudu (fs_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-d99e64e9df4b9729be50977e7fc57f6b9d2c184d10b905345fe089fa1fd256c5R838)
 1. ~~Modify any protocol buffers that have ambiguous payload with a per-destination unique key [kudu (consensus.proto)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-7d7d8ed941658533d9cadc39ff2075a2caad979fec15377e0d6364ce291fa88aR434-R452)~~
 1. Record outbound requests and inbound responses (responses to outbound requests) with `RecordReplay` 
 1. ~~Record inbound responses with RROutboundCall()~~
 1. Record inbound requests and outbound responses (responses to inbound requests) [kudu request part(connection.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-7a43ab0a4611f187f672845c106ae903eb81350fbf9b5b9aabeecfbcf12123e6R704-R707) [kudu response part (inbound_call.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-5a4f04732c39584b145034490dcc2602ed0b896a30c68089e7293584f1ac2c1bR206-R212)
 1. Register inbound message reproducers with AirReplay 
    1. Inbound request reproducer(s) [kudu (kserver.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-b843607bdc0af2f903cbf75e924ab230d7b4506fb83e23b27853611c8f04553aR148-R181)
    1. Inbdound response reproducer(s)
    1. Note:

        If your application relies on an internal event loop to call the callbacks triggering incoming requests/responses, you need to choose one of the options below:
        1. The event loop runs in replay mode as well
        1. You create a mock-event loop
   
    In both cases the reproducers registered with AirReplay should trigger the event loop to call a (connection,messageId) specific callback

1. Convert internal non-deterministic events into incoming messages that can be recorded and then be retriggered with a custom registered reproducer (<ins>kudu WIP examples below</ins>)
    1. Timer expiration for heartbeats
    1. Lock acquization order of key locks
 ---
 1. Record enough information about all objects that will not exist in replay so replay can successfully mock them (kudu examples below)
    1. Information on incoming socket [kudu (inbound_call.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-5a4f04732c39584b145034490dcc2602ed0b896a30c68089e7293584f1ac2c1bR119-R151)
 1. Record additional points of non-determinism (kudu examples below)
    1. Save/Restore mutual TLS signature [kudu (heartbeater.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-9c8d9c20339579dbeab6e22eb5f14bbfa0aaa06cf2391b1cc8028e735412725cR481-R494)
    1. Assign timestamp to each message [kudu (time_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-1a6ded63b6b1501abc1c2b14bc5f3f0a5fd28e01a04fda2835a6c92805900099L107-R111)
    1. Assign node startup time [kudu (master.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-0693e3530804ab97e66fdfac668582385a0fc81f2ba34c6abf69ba71c72bb3e1L524-R529)
    1. Assign transaction start time [kudu (txn_status_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-430bc88a3a1c8583f71a90167c3b13f2e5b099b4b90263cce013d0bda4445f46R995-R1014)
    1. Assign heartbeat time [kudu (heartbeater.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-9c8d9c20339579dbeab6e22eb5f14bbfa0aaa06cf2391b1cc8028e735412725cL391-R396)


After an initial round of instrumentation you can start recording traces of your application (perhaps by running your test suits) and replaying the generated traces.

If you encounter repeating non-changing log message like the following:

```
RecordReplay@42: right kind and entry key. wrong proto message. Field: tablets.cstate.current_term - Value Mismatch f1value:12 f2value:11
```
it means that the replay has diverged.
The log message says that divergence was detecting at log position `42`, when handling a `RecordReplay` API call. The call expected the protobuf field `tablets.cstate.current_term` to have value `12` according to trace, but it has value `11`
