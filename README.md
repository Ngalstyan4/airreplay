# AirReplay - Application Integrated Record-Replay

AirReplay is a library that allows low-overhead recording of all non-determinism in a distributed system and enables bug reproduction.
It provides an API to record inter-node RPC communication and intra-node non-determinism.

## AirReplay API
```cpp
Airreplay(std::string tracename, Mode mode); // mode = RECORD|REPLAY

int RecordReplay(const std::string &connection_info
                const google::protobuf::Message &message, 
                int kind = 0, 
                const std::string &debug_info = "");
bool isReplay(); // true if in REPLAY mode

int SaveRestore(const std::string &key, google::protobuf::Message &message);
int SaveRestore(const std::string &key, std::string &message);
int SaveRestore(const std::string &key, uint64 &message);
...

void RegisterReproducers(std::map<int, ReproducerFunction> reproducers);

```
The API above helps record each distributed system node into a separate per-node trace. The recorded trace enables replay of the distributed system node in isolation.

`RecordReplay` records its arguments during recording into a totally ordered trace.
During replay, `RecordReplay` enforces that concurrent messages are processed in the same order they were processed during recording[^1].
It uses the recorded trace to know which message to expect next. When it receives the expected message, it advances the tracked position on the trace. If `RecordReplay` receives a call with an unexpected message, it blocks that call and continues waiting for the recorded message.

Messages sent by the recorded node will be sent again during replay as replay runs the same application code as the recording.
Since the replay of a node happens in isolation, incoming messages (messages that originate on other nodes) will not be sent to the application automatically.  However, those messages are still recorded on the trace, and the application developer just needs to provide a reproducer function via the `RegisterReproducers` API that will redeliver the recorded incoming messages to the application.


The above allows the application developer to record inter-node communication and intra-node message processing related non-determinism. The application may have other sources of non-determinism such as message timestamps, random IDs, initial snapshot (stating state) of the application etc. `SaveRestore` interface allows recording those with AirReplay trace. 
`SaveRestore(key, msg)` checks for an entry at the current position of the trace with a matching `key`. If found, `SaveRestore` populates `msg` reference.

[^1]: Even when `RecordReplay` is called by a single main control loop in a dedicated thread, messages can arrive out of order in replay as the main control loop may receive messages concurrently from various sources and determine a total processing order internally


## Integrating AirReplay into a new application

Example integrations:
- [kuduraft (leader election only)](https://github.com/facebook/kuduraft/compare/1.8.raft...Ngalstyan4:kuduraft:airreplay?expand=1)
- [kudu (WIP, replay still fails)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1)

 Integration steps
 1. Build AirReplay, as outlined in the next section, to obtain the shared library
 1. Modify your application build scripts to link to AirReplay [kudu (CMakeLists.txt)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-20ff7a6c6cd70212e1413303ebd974ee5745be9c02ae55ae34017a7f9a85a6ecR114-R121)
 1. Initialize `airreplay::airr` global object at the start of your application.  
 Note: this has to be __before__ any point in time when you want to record/replay something [kudu (kserver.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-b843607bdc0af2f903cbf75e924ab230d7b4506fb83e23b27853611c8f04553aR148-R181)
 1. Record the initial state of the application via `SaveRestore`.  
 E.g. database snapshot at startup, randomly generated uuids [kudu (fs_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-d99e64e9df4b9729be50977e7fc57f6b9d2c184d10b905345fe089fa1fd256c5R838) todo:: had to change sth here  
 1. Record with `RecordReplay`:
    1. Outbound requests [kudu (proxy.cc)](https://github.com/Ngalstyan4/kuduraft/pull/1/files#diff-aa0d48ba6d10b66bba7262f72d15c15105429ce3dab097725f7c7f0b6df57530R205-R210)
    1. Inbound responses (responses to outbound requests) [kudu (outbound_call.cc)](https://github.com/Ngalstyan4/kuduraft/pull/1/files#diff-2cbb0e21633e6a2becdd084b1e7ad756fb6363b47f5141309f18c10d45727a1bR309-R320)
    1. Inbound requests[ kudu (connection.cc)](https://github.com/Ngalstyan4/kuduraft/pull/1/files#diff-7a43ab0a4611f187f672845c106ae903eb81350fbf9b5b9aabeecfbcf12123e6R711-R718) 
    1. Outbound responses (responses to inbound requests) [kudu (inbound_call.cc)](https://github.com/Ngalstyan4/kuduraft/pull/1/files#diff-5a4f04732c39584b145034490dcc2602ed0b896a30c68089e7293584f1ac2c1bR207-R217)   
 1. Register inbound message reproducers with AirReplay 
    1. If your application has incoming message handlers that take RPC messages, you can register these handlers with AirReplay (etcd example to be linked), [kudu inbound requests (kserver.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-b843607bdc0af2f903cbf75e924ab230d7b4506fb83e23b27853611c8f04553aR196-R221)
    1. Some applications do not have handlers that directly consume an incoming RPC message. When sending a request, these applications register a callback with an internal event loop. The event loop calls app-provided callbacks and informs the application about the new message. The callback is specific to that one request and is called by an internal event loop when the response arrives. 
    So during replay, the callback provided by the application’s request must be called in order to reproduce the response
    So during replay, the callback provided by the application’s request must be called in order to reproduce the response.  
    To integrate these applications with AirReplay, you must ensure the callbacks provided by the application are called during replay.  
    So, you can do one of the following:
        1. Run the same event loop in replay mode as well
        1. Create a mock-event loop that stores callbacks and calls them when triggered by AirReplay via a message reproducer
1. Convert internal non-deterministic events into incoming messages that can be recorded and retriggered with a custom registered reproducer (<ins>kudu WIP examples below</ins>)
    1. Timer expiration for heartbeats
    1. Lock acquisition order of key locks
 ---
 1. Save (via `SaveRestore`) enough information about all objects that will not exist in replay so replay can successfully mock them (kudu examples below)
    1. Information on incoming socket [kudu (inbound_call.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-5a4f04732c39584b145034490dcc2602ed0b896a30c68089e7293584f1ac2c1bR119-R151)
 1. `SaveRestore` additional points of non-determinism (kudu examples below)
    1. Save/Restore mutual TLS signature [kudu (heartbeater.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-9c8d9c20339579dbeab6e22eb5f14bbfa0aaa06cf2391b1cc8028e735412725cR481-R494)
    1. Assign timestamp to each message [kudu (time_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-1a6ded63b6b1501abc1c2b14bc5f3f0a5fd28e01a04fda2835a6c92805900099L107-R111)
    1. Assign node startup time [kudu (master.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-0693e3530804ab97e66fdfac668582385a0fc81f2ba34c6abf69ba71c72bb3e1L524-R529)
    1. Assign transaction start time [kudu (txn_status_manager.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-430bc88a3a1c8583f71a90167c3b13f2e5b099b4b90263cce013d0bda4445f46R995-R1014)
    1. Assign heartbeat time [kudu (heartbeater.cc)](https://github.com/Ngalstyan4/kuduraft/compare/kudu...Ngalstyan4:kuduraft:kudu_airreplay?expand=1#diff-9c8d9c20339579dbeab6e22eb5f14bbfa0aaa06cf2391b1cc8028e735412725cL391-R396)


After an initial round of instrumentation, you can start recording traces of your application (perhaps by running your test suits) and replaying the generated traces.

If you encounter repeating non-changing log messages like the following:

```
RecordReplay@42: right kind and entry key. wrong proto message. Field: tablets.cstate.current_term - Value Mismatch f1value:12 f2value:11
```
it means that the replay has diverged.
The log message says that divergence was detected at log position `42`, when handling a `RecordReplay` API call. The call expected the protobuf field `tablets.cstate.current_term` to have value `12` according to trace, but it has value `11`

## Integrating Your Application with AirReplay

To use AirReplay in a new application, first obtain and build AirReplay with:
```
git clone https://github.com/Ngalstyan4/airreplay
mkdir build && cd build
cmake ..
make install
```
This will compile the library and install it at `./build/install`. You should see something like:
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
You can then use the API exposed via the airreplay headers to instrument your application and link your application to libairreplay.a


## AirReplay's Protobuf dependency
AirReplay depends on `protobuf` and compiles with whatever versioned `protobuf` library is available on the system.
This works well if your application depends on system's `protobuf` as well. 
If your application builds its own `protobuf` library, you need to make sure that AirReplay and your project are built with the same version of `protobuf`. 
See an example of this done for [kudu integration.](https://github.com/Ngalstyan4/airreplay/blob/3c7ade1a21980e51231522d79a26840e435de99f/CMakeLists.txt#L27-L41)

Kudu and Kuduraft do not depend on the system's `protobuf`. When using AirReplay to record-replay kudu, run `cmake` with
```
cmake -DKUDU_HOME=[PATH_TO_KUDU_PROJECT_ROOT_DIR] ...
```
 and AirReplay will use the `protobuf` library compiled for kudu.

